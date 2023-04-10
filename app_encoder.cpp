#include "app_encoder.h"
#include "app_config.h"
#include "app_ffmpeg.h"
#include "app_pithy_print.h"
#include "app_utility.h"
#include "consts.h"
#include "protocol_rtmp_stack.h"
#include "protocol_st.h"
#include "utility.h"
#include <algorithm>
#include <inttypes.h>

// for encoder to detect the dead loop
static std::vector<std::string> _transcoded_url;

Encoder::Encoder()
{
    m_trd = new DummyCoroutine();
    m_pprint = PithyPrint::CreateEncoder();
}

Encoder::~Encoder()
{
    OnUnpublish();

    Freep(m_trd);
    Freep(m_pprint);
}

error Encoder::OnPublish(Request *req)
{
    error err = SUCCESS;

    // parse the transcode engines for vhost and app and stream.
    err = ParseScopeEngines(req);

    // ignore the loop encoder
    // if got a loop, donot transcode the whole stream.
    if (ERRORCODE(err) == ERROR_ENCODER_LOOP) {
        ClearEngines();
        ERRORRESET(err);
    }

    // return for error or no engine.
    if (err != SUCCESS || m_ffmpegs.empty()) {
        return err;
    }

    // start thread to run all encoding engines.
    Freep(m_trd);
    m_trd = new STCoroutine("encoder", this, Context->GetId());
    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start encoder");
    }

    return err;
}

void Encoder::OnUnpublish()
{
    m_trd->Stop();
    ClearEngines();
}

// when error, encoder sleep for a while and retry.
#define RTMP_ENCODER_CIMS (3 * UTIME_SECONDS)

error Encoder::Cycle()
{
    error err = SUCCESS;

    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = m_trd->Pull()) != SUCCESS) {
            err = ERRORWRAP(err, "encoder");
            break;
        }

        if ((err = DoCycle()) != SUCCESS) {
            warn("Encoder: Ignore error, %s", ERRORDESC(err).c_str());
            ERRORRESET(err);
        }

        Usleep(RTMP_ENCODER_CIMS);
    }

    // kill ffmpeg when finished and it alive
    std::vector<FFMPEG*>::iterator it;

    for (it = m_ffmpegs.begin(); it != m_ffmpegs.end(); ++it) {
        FFMPEG* ffmpeg = *it;
        ffmpeg->Stop();
    }

    return err;
}

error Encoder::DoCycle()
{
    error err = SUCCESS;

    std::vector<FFMPEG*>::iterator it;
    for (it = m_ffmpegs.begin(); it != m_ffmpegs.end(); ++it) {
        FFMPEG* ffmpeg = *it;

        // start all ffmpegs.
        if ((err = ffmpeg->Start()) != SUCCESS) {
            return ERRORWRAP(err, "ffmpeg start");
        }

        // check ffmpeg status.
        if ((err = ffmpeg->Cycle()) != SUCCESS) {
            return ERRORWRAP(err, "ffmpeg cycle");
        }
    }

    // pithy print
    ShowEncodeLogMessage();

    return err;
}

void Encoder::ClearEngines()
{
    std::vector<FFMPEG*>::iterator it;

    for (it = m_ffmpegs.begin(); it != m_ffmpegs.end(); ++it) {
        FFMPEG* ffmpeg = *it;

        std::string output = ffmpeg->Output();

        std::vector<std::string>::iterator tu_it;
        tu_it = std::find(_transcoded_url.begin(), _transcoded_url.end(), output);
        if (tu_it != _transcoded_url.end()) {
            _transcoded_url.erase(tu_it);
        }

        Freep(ffmpeg);
    }

    m_ffmpegs.clear();
}

FFMPEG *Encoder::At(int index)
{
    return m_ffmpegs[index];
}

error Encoder::ParseScopeEngines(Request *req)
{
    error err = SUCCESS;

    // parse all transcode engines.
    ConfDirective* conf = NULL;

    // parse vhost scope engines
    std::string scope = "";
    if ((conf = config->GetTranscode(req->m_vhost, scope)) != NULL) {
        if ((err = ParseFfmpeg(req, conf)) != SUCCESS) {
            return ERRORWRAP(err, "parse ffmpeg");
        }
    }
    // parse app scope engines
    scope = req->m_app;
    if ((conf = config->GetTranscode(req->m_vhost, scope)) != NULL) {
        if ((err = ParseFfmpeg(req, conf)) != SUCCESS) {
            return ERRORWRAP(err, "parse ffmpeg");
        }
    }
    // parse stream scope engines
    scope += "/";
    scope += req->m_stream;
    if ((conf = config->GetTranscode(req->m_vhost, scope)) != NULL) {
        if ((err = ParseFfmpeg(req, conf)) != SUCCESS) {
            return ERRORWRAP(err, "parse ffmpeg");
        }
    }

    return err;
}

error Encoder::ParseFfmpeg(Request *req, ConfDirective *conf)
{
    error err = SUCCESS;

    Assert(conf);

    // enabled
    if (!config->GetTranscodeEnabled(conf)) {
        trace("ignore the disabled transcode: %s", conf->Arg0().c_str());
        return err;
    }

    // ffmpeg
    std::string ffmpeg_bin = config->GetTranscodeFfmpeg(conf);
    if (ffmpeg_bin.empty()) {
        trace("ignore the empty ffmpeg transcode: %s", conf->Arg0().c_str());
        return err;
    }

    // get all engines.
    std::vector<ConfDirective*> engines = config->GetTranscodeEngines(conf);
    if (engines.empty()) {
        trace("ignore the empty transcode engine: %s", conf->Arg0().c_str());
        return err;
    }

    // create engine
    for (int i = 0; i < (int)engines.size(); i++) {
        ConfDirective* engine = engines[i];
        if (!config->GetEngineEnabled(engine)) {
            trace("ignore the diabled transcode engine: %s %s", conf->Arg0().c_str(), engine->Arg0().c_str());
            continue;
        }

        FFMPEG* ffmpeg = new FFMPEG(ffmpeg_bin);
        if ((err = InitializeFfmpeg(ffmpeg, req, engine)) != SUCCESS) {
            Freep(ffmpeg);
            return ERRORWRAP(err, "init ffmpeg");
        }

        m_ffmpegs.push_back(ffmpeg);
    }

    return err;
}

error Encoder::InitializeFfmpeg(FFMPEG *ffmpeg, Request *req, ConfDirective *engine)
{
    error err = SUCCESS;

    std::string input;
    // input stream, from local.
    // ie. rtmp://localhost:1935/live/livestream
    input = "rtmp://";
    input += CONSTS_LOCALHOST;
    input += ":";
    input += Int2Str(req->m_port);
    input += "/";
    input += req->m_app;
    input += "?vhost=";
    input += req->m_vhost;
    input += "/";
    input += req->m_stream;

    // stream name: vhost/app/stream for print
    m_inputStreamName = req->m_vhost;
    m_inputStreamName += "/";
    m_inputStreamName += req->m_app;
    m_inputStreamName += "/";
    m_inputStreamName += req->m_stream;

    std::string output = config->GetEngineOutput(engine);
    // output stream, to other/self server
    // ie. rtmp://localhost:1935/live/livestream_sd
    output = StringReplace(output, "[vhost]", req->m_vhost);
    output = StringReplace(output, "[port]", Int2Str(req->m_port));
    output = StringReplace(output, "[app]", req->m_app);
    output = StringReplace(output, "[stream]", req->m_stream);
    output = StringReplace(output, "[param]", req->m_param);
    output = StringReplace(output, "[engine]", engine->Arg0());
    output = PathBuildTimestamp(output);

    std::string log_file = CONSTS_NULL_FILE; // disabled
    // write ffmpeg info to log file.
    if (config->GetFfLogEnabled()) {
        log_file = config->GetFfLogDir();
        log_file += "/";
        log_file += "ffmpeg-encoder";
        log_file += "-";
        log_file += req->m_vhost;
        log_file += "-";
        log_file += req->m_app;
        log_file += "-";
        log_file += req->m_stream;
        if (!engine->m_args.empty()) {
            log_file += "-";
            log_file += engine->Arg0();
        }
        log_file += ".log";
    }

    // important: loop check, donot transcode again.
    std::vector<std::string>::iterator it;
    it = std::find(_transcoded_url.begin(), _transcoded_url.end(), input);
    if (it != _transcoded_url.end()) {
        return ERRORNEW(ERROR_ENCODER_LOOP, "detect a loop cycle, input=%s, output=%s", input.c_str(), output.c_str());
    }
    _transcoded_url.push_back(output);

    if ((err = ffmpeg->Initialize(input, output, log_file)) != SUCCESS) {
        return ERRORWRAP(err, "init ffmpeg");
    }
    if ((err = ffmpeg->InitializeTranscode(engine)) != SUCCESS) {
        return ERRORWRAP(err, "init transcode");
    }

    return err;
}

void Encoder::ShowEncodeLogMessage()
{
    m_pprint->Elapse();

    // reportable
    if (m_pprint->CanPrint()) {
        // TODO: FIXME: show more info.
        trace("-> " CONSTS_LOG_ENCODER " time=%" PRId64 ", encoders=%d, input=%s",
                  m_pprint->Age(), (int)m_ffmpegs.size(), m_inputStreamName.c_str());
    }
}

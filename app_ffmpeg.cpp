#include "app_ffmpeg.h"

#include "app_config.h"
#include "app_process.h"
#include "error.h"
#include "utility.h"

#define RTMP_ENCODER_COPY "copy"
#define RTMP_ENCODER_NO_VIDEO "vn"
#define RTMP_ENCODER_NO_AUDIO "an"
// only support libx264 encoder.
#define RTMP_ENCODER_VCODEC_LIBX264 "libx264"
#define RTMP_ENCODER_VCODEC_PNG "png"
// any aac encoder is ok which contains the aac,
// for example, libaacplus, aac, fdkaac
#define RTMP_ENCODER_ACODEC "aac"
#define RTMP_ENCODER_LIBAACPLUS "libaacplus"
#define RTMP_ENCODER_LIBFDKAAC "libfdk_aac"

FFMPEG::FFMPEG(std::string ffmpeg_bin)
{
    m_ffmpeg = ffmpeg_bin;

    m_vbitrate = 0;
    m_vfps = 0;
    m_vwidth = 0;
    m_vheight = 0;
    m_vthreads = 0;
    m_abitrate = 0;
    m_asampleRate = 0;
    m_achannels = 0;

    m_process = new Process();
}

FFMPEG::~FFMPEG()
{
    Stop();

    Freep(m_process);
}

void FFMPEG::AppendIparam(std::string iparam)
{
    m_iparams.push_back(iparam);
}

void FFMPEG::SetOformat(std::string format)
{
    m_oformat = format;
}

std::string FFMPEG::Output()
{
    return m_output;
}

error FFMPEG::Initialize(std::string in, std::string out, std::string log)
{
    error err = SUCCESS;

    m_input = in;
    m_output = out;
    m_logFile = log;

    return err;
}

error FFMPEG::InitializeTranscode(ConfDirective *engine)
{
    error err = SUCCESS;

    m_perfile = config->GetEnginePerfile(engine);
    m_iformat = config->GetEngineIformat(engine);
    m_vfilter = config->GetEngineVfilter(engine);
    m_vcodec = config->GetEngineVcodec(engine);
    m_vbitrate = config->GetEngineVbitrate(engine);
    m_vfps = config->GetEngineVfps(engine);
    m_vwidth = config->GetEngineVwidth(engine);
    m_vheight = config->GetEngineVheight(engine);
    m_vthreads = config->GetEngineVthreads(engine);
    m_vprofile = config->GetEngineVprofile(engine);
    m_vpreset = config->GetEngineVpreset(engine);
    m_vparams = config->GetEngineVparams(engine);
    m_acodec = config->GetEngineAcodec(engine);
    m_abitrate = config->GetEngineAbitrate(engine);
    m_asampleRate = config->GetEngineAsampleRate(engine);
    m_achannels = config->GetEngineAchannels(engine);
    m_aparams = config->GetEngineAparams(engine);
    m_oformat = config->GetEngineOformat(engine);

    // ensure the size is even.
    m_vwidth -= m_vwidth % 2;
    m_vheight -= m_vheight % 2;

    if (m_vcodec == RTMP_ENCODER_NO_VIDEO && m_acodec == RTMP_ENCODER_NO_AUDIO) {
        return ERRORNEW(ERROR_ENCODER_VCODEC, "video and audio disabled");
    }

    if (m_vcodec != RTMP_ENCODER_COPY && m_vcodec != RTMP_ENCODER_NO_VIDEO && m_vcodec != RTMP_ENCODER_VCODEC_PNG) {
        if (m_vcodec != RTMP_ENCODER_VCODEC_LIBX264) {
            return ERRORNEW(ERROR_ENCODER_VCODEC, "invalid vcodec, must be %s, actual %s", RTMP_ENCODER_VCODEC_LIBX264, m_vcodec.c_str());
        }
        if (m_vbitrate < 0) {
            return ERRORNEW(ERROR_ENCODER_VBITRATE, "invalid vbitrate: %d", m_vbitrate);
        }
        if (m_vfps < 0) {
            return ERRORNEW(ERROR_ENCODER_VFPS, "invalid vfps: %.2f", m_vfps);
        }
        if (m_vwidth < 0) {
            return ERRORNEW(ERROR_ENCODER_VWIDTH, "invalid vwidth: %d", m_vwidth);
        }
        if (m_vheight < 0) {
            return ERRORNEW(ERROR_ENCODER_VHEIGHT, "invalid vheight: %d", m_vheight);
        }
        if (m_vthreads < 0) {
            return ERRORNEW(ERROR_ENCODER_VTHREADS, "invalid vthreads: %d", m_vthreads);
        }
        if (m_vprofile.empty()) {
            return ERRORNEW(ERROR_ENCODER_VPROFILE, "invalid vprofile: %s", m_vprofile.c_str());
        }
        if (m_vpreset.empty()) {
            return ERRORNEW(ERROR_ENCODER_VPRESET, "invalid vpreset: %s", m_vpreset.c_str());
        }
    }

    // @see, https://github.com/ossrs/srs/issues/145
    if (m_acodec == RTMP_ENCODER_LIBAACPLUS && m_acodec != RTMP_ENCODER_LIBFDKAAC) {
        if (m_abitrate != 0 && (m_abitrate < 16 || m_abitrate > 72)) {
            return ERRORNEW(ERROR_ENCODER_ABITRATE, "invalid abitrate for aac: %d, must in [16, 72]", m_abitrate);
        }
    }

    if (m_acodec != RTMP_ENCODER_COPY && m_acodec != RTMP_ENCODER_NO_AUDIO) {
        if (m_abitrate < 0) {
            return ERRORNEW(ERROR_ENCODER_ABITRATE, "invalid abitrate: %d", m_abitrate);
        }
        if (m_asampleRate < 0) {
            return ERRORNEW(ERROR_ENCODER_ASAMPLE_RATE, "invalid sample rate: %d", m_asampleRate);
        }
        if (m_achannels != 0 && m_achannels != 1 && m_achannels != 2) {
            return ERRORNEW(ERROR_ENCODER_ACHANNELS, "invalid achannels, must be 1 or 2, actual %d", m_achannels);
        }
    }
    if (m_output.empty()) {
        return ERRORNEW(ERROR_ENCODER_OUTPUT, "invalid empty output");
    }

    // for not rtmp input, donot append the iformat,
    // for example, "-f flv" before "-i udp://192.168.1.252:2222"
    // @see https://github.com/ossrs/srs/issues/290
    if (!StringStartsWith(m_input, "rtmp://")) {
        m_iformat = "";
    }

    return err;
}

error FFMPEG::InitializeCopy()
{
    error err = SUCCESS;

    m_vcodec = RTMP_ENCODER_COPY;
    m_acodec = RTMP_ENCODER_COPY;

    if (m_output.empty()) {
        return ERRORNEW(ERROR_ENCODER_OUTPUT, "invalid empty output");
    }

    return err;
}

error FFMPEG::Start()
{
    error err = SUCCESS;

    if (m_process->Started()) {
        return err;
    }

    // the argv for process.
    m_params.clear();

    // argv[0], set to ffmpeg bin.
    // The  execv()  and  execvp() functions ....
    // The first argument, by convention, should point to
    // the filename associated  with  the file being executed.
    m_params.push_back(m_ffmpeg);

    // input params
    for (int i = 0; i < (int)m_iparams.size(); i++) {
        std::string iparam = m_iparams.at(i);
        if (!iparam.empty()) {
            m_params.push_back(iparam);
        }
    }

    // build the perfile
    if (!m_perfile.empty()) {
        std::vector<std::string>::iterator it;
        for (it = m_perfile.begin(); it != m_perfile.end(); ++it) {
            std::string p = *it;
            if (!p.empty()) {
                m_params.push_back(p);
            }
        }
    }

    // input.
    if (m_iformat != "off" && !m_iformat.empty()) {
        m_params.push_back("-f");
        m_params.push_back(m_iformat);
    }

    m_params.push_back("-i");
    m_params.push_back(m_input);

    // build the filter
    if (!m_vfilter.empty()) {
        std::vector<std::string>::iterator it;
        for (it = m_vfilter.begin(); it != m_vfilter.end(); ++it) {
            std::string p = *it;
            if (!p.empty()) {
                m_params.push_back(p);
            }
        }
    }

    // video specified.
    if (m_vcodec != RTMP_ENCODER_NO_VIDEO) {
        m_params.push_back("-vcodec");
        m_params.push_back(m_vcodec);
    } else {
        m_params.push_back("-vn");
    }

    // the codec params is disabled when copy
    if (m_vcodec != RTMP_ENCODER_COPY && m_vcodec != RTMP_ENCODER_NO_VIDEO) {
        if (m_vbitrate > 0) {
            m_params.push_back("-b:v");
            m_params.push_back(Int2Str(m_vbitrate * 1000));
        }

        if (m_vfps > 0) {
            m_params.push_back("-r");
            m_params.push_back(Float2Str(m_vfps));
        }

        if (m_vwidth > 0 && m_vheight > 0) {
            m_params.push_back("-s");
            m_params.push_back(Int2Str(m_vwidth) + "x" + Int2Str(m_vheight));
        }

        // TODO: add aspect if needed.
        if (m_vwidth > 0 && m_vheight > 0) {
            m_params.push_back("-aspect");
            m_params.push_back(Int2Str(m_vwidth) + ":" + Int2Str(m_vheight));
        }

        if (m_vthreads > 0) {
            m_params.push_back("-threads");
            m_params.push_back(Int2Str(m_vthreads));
        }

        if (!m_vprofile.empty()) {
            m_params.push_back("-profile:v");
            m_params.push_back(m_vprofile);
        }

        if (!m_vpreset.empty()) {
            m_params.push_back("-preset");
            m_params.push_back(m_vpreset);
        }

        // vparams
        if (!m_vparams.empty()) {
            std::vector<std::string>::iterator it;
            for (it = m_vparams.begin(); it != m_vparams.end(); ++it) {
                std::string p = *it;
                if (!p.empty()) {
                    m_params.push_back(p);
                }
            }
        }
    }

    // audio specified.
    if (m_acodec != RTMP_ENCODER_NO_AUDIO) {
        m_params.push_back("-acodec");
        m_params.push_back(m_acodec);
    } else {
        m_params.push_back("-an");
    }

    // the codec params is disabled when copy
    if (m_acodec != RTMP_ENCODER_NO_AUDIO) {
        if (m_acodec != RTMP_ENCODER_COPY) {
            if (m_abitrate > 0) {
                m_params.push_back("-b:a");
                m_params.push_back(Int2Str(m_abitrate * 1000));
            }

            if (m_asampleRate > 0) {
                m_params.push_back("-ar");
                m_params.push_back(Int2Str(m_asampleRate));
            }

            if (m_achannels > 0) {
                m_params.push_back("-ac");
                m_params.push_back(Int2Str(m_achannels));
            }

            // aparams
            std::vector<std::string>::iterator it;
            for (it = m_aparams.begin(); it != m_aparams.end(); ++it) {
                std::string p = *it;
                if (!p.empty()) {
                    m_params.push_back(p);
                }
            }
        } else {
            // for audio copy.
            for (int i = 0; i < (int)m_aparams.size();) {
                std::string pn = m_aparams[i++];

                // aparams, the adts to asc filter "-bsf:a aac_adtstoasc"
                if (pn == "-bsf:a" && i < (int)m_aparams.size()) {
                    std::string pv = m_aparams[i++];
                    if (pv == "aac_adtstoasc") {
                        m_params.push_back(pn);
                        m_params.push_back(pv);
                    }
                }
            }
        }
    }

    // output
    if (m_oformat != "off" && !m_oformat.empty()) {
        m_params.push_back("-f");
        m_params.push_back(m_oformat);
    }

    m_params.push_back("-y");
    m_params.push_back(m_output);

    // when specified the log file.
    if (!m_logFile.empty()) {
        // stdout
        m_params.push_back("1");
        m_params.push_back(">");
        m_params.push_back(m_logFile);
        // stderr
        m_params.push_back("2");
        m_params.push_back(">");
        m_params.push_back(m_logFile);
    }

    // initialize the process.
    if ((err = m_process->Initialize(m_ffmpeg, m_params)) != SUCCESS) {
        return ERRORWRAP(err, "init process");
    }

    return m_process->Start();
}

error FFMPEG::Cycle()
{
    return m_process->Cycle();
}

void FFMPEG::Stop()
{
    m_process->Stop();
}

void FFMPEG::FastStop()
{
    m_process->FastStop();
}

void FFMPEG::FastKill()
{
    m_process->FastKill();
}

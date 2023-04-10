#include "app_dvr.h"
#include "app_config.h"
#include "app_utility.h"
#include "app_http_hooks.h"
#include "buffer.h"
#include "codec.h"
#include "file.h"
#include "app_fragment.h"
#include "protocol_amf0.h"
#include "protocol_rtmp_stack.h"
#include "utility.h"
#include "core_autofree.h"

DvrSegmenter::DvrSegmenter()
{
    m_req = NULL;
    m_jitter = NULL;
    m_plan = NULL;
    m_waitKeyframe = true;

    m_fragment = new Fragment();
    m_fs = new FileWriter();
    m_jitterAlgorithm = RtmpJitterAlgorithmOFF;

    config->Subscribe(this);
}

DvrSegmenter::~DvrSegmenter()
{
    config->Unsubscribe(this);

    Freep(m_fragment);
    Freep(m_jitter);
    Freep(m_fs);
}

error DvrSegmenter::Initialize(DvrPlan *p, Request *r)
{
    m_req = r;
    m_plan = p;

    m_jitterAlgorithm = (RtmpJitterAlgorithm)config->GetDvrTimeJitter(m_req->m_vhost);
    m_waitKeyframe = config->GetDvrWaitKeyframe(m_req->m_vhost);

    return SUCCESS;
}

Fragment *DvrSegmenter::Current()
{
    return m_fragment;
}

error DvrSegmenter::Open()
{
    error err = SUCCESS;

    // ignore when already open.
    if (m_fs->IsOpen()) {
        return err;
    }

    std::string path = GeneratePath();
    if (PathExists(path)) {
        return ERRORNEW(ERROR_DVR_CANNOT_APPEND, "DVR can't append to exists path=%s", path.c_str());
    }
    m_fragment->SetPath(path);

    // create dir first.
    if ((err = m_fragment->CreateDir()) != SUCCESS) {
        return ERRORWRAP(err, "create dir");
    }

    // create jitter.
    Freep(m_jitter);
    m_jitter = new RtmpJitter();

    // open file writer, in append or create mode.
    std::string tmp_dvr_file = m_fragment->Tmppath();
    if ((err = m_fs->Open(tmp_dvr_file)) != SUCCESS) {
        return ERRORWRAP(err, "open file %s", path.c_str());
    }

    // initialize the encoder.
    if ((err = OpenEncoder()) != SUCCESS) {
        return ERRORWRAP(err, "open encoder");
    }

    trace("dvr stream %s to file %s", m_req->m_stream.c_str(), path.c_str());
    return err;
}

error DvrSegmenter::WriteMetadata(SharedPtrMessage *metadata)
{
    return EncodeMetadata(metadata);
}

error DvrSegmenter::WriteAudio(SharedPtrMessage *shared_audio, Format *format)
{
    error err = SUCCESS;

    SharedPtrMessage* audio = shared_audio->Copy();
    AutoFree(SharedPtrMessage, audio);

    if ((err = m_jitter->Correct(audio, m_jitterAlgorithm)) != SUCCESS) {
        return ERRORWRAP(err, "jitter");
    }

    if ((err = OnUpdateDuration(audio)) != SUCCESS) {
        return ERRORWRAP(err, "update duration");
    }

    if ((err = EncodeAudio(audio, format)) != SUCCESS) {
        return ERRORWRAP(err, "encode audio");
    }

    return err;
}

error DvrSegmenter::WriteVideo(SharedPtrMessage *shared_video, Format *format)
{
    error err = SUCCESS;

    SharedPtrMessage* video = shared_video->Copy();
    AutoFree(SharedPtrMessage, video);

    if ((err = m_jitter->Correct(video, m_jitterAlgorithm)) != SUCCESS) {
        return ERRORWRAP(err, "jitter");
    }

    if ((err = EncodeVideo(video, format)) != SUCCESS) {
        return ERRORWRAP(err, "encode video");
    }

    if ((err = OnUpdateDuration(video)) != SUCCESS) {
        return ERRORWRAP(err, "update duration");
    }

    return err;
}

error DvrSegmenter::Close()
{
    error err = SUCCESS;

    // ignore when already closed.
    if (!m_fs->IsOpen()) {
        return err;
    }

    // Close the encoder, then close the fs object.
    err = CloseEncoder();
    m_fs->Close(); // Always close the file.
    if (err != SUCCESS) {
        return ERRORWRAP(err, "close encoder");
    }

    // when tmp flv file exists, reap it.
    if ((err = m_fragment->Rename()) != SUCCESS) {
        return ERRORWRAP(err, "rename fragment");
    }

    // TODO: FIXME: the http callback is async, which will trigger thread switch,
    //          so the on_video maybe invoked during the http callback, and error.
    if ((err = m_plan->OnReapSegment()) != SUCCESS) {
        return ERRORWRAP(err, "reap segment");
    }

    return err;
}

std::string DvrSegmenter::GeneratePath()
{
    // the path in config, for example,
    //      /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv
    std::string path_config = config->GetDvrPath(m_req->m_vhost);

    // add [stream].[timestamp].flv as filename for dir
    if (!StringEndsWith(path_config, ".flv", ".mp4")) {
        path_config += "/[stream].[timestamp].flv";
    }

    // the flv file path
    std::string flv_path = path_config;
    flv_path = PathBuildStream(flv_path, m_req->m_vhost, m_req->m_app, m_req->m_stream);
    flv_path = PathBuildTimestamp(flv_path);

    return flv_path;
}

error DvrSegmenter::OnUpdateDuration(SharedPtrMessage *msg)
{
    m_fragment->Append(msg->m_timestamp);
    return SUCCESS;
}

error DvrSegmenter::OnReloadVhostDvr(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    m_jitterAlgorithm = (RtmpJitterAlgorithm)config->GetDvrTimeJitter(m_req->m_vhost);
    m_waitKeyframe = config->GetDvrWaitKeyframe(m_req->m_vhost);

    return err;
}

DvrFlvSegmenter::DvrFlvSegmenter()
{
    m_enc = new FlvTransmuxer();

    m_durationOffset = 0;
    m_filesizeOffset = 0;

    m_hasKeyframe = false;
}

DvrFlvSegmenter::~DvrFlvSegmenter()
{
    Freep(m_enc);
}

error DvrFlvSegmenter::RefreshMetadata()
{
    error err = SUCCESS;

    // no duration or filesize specified.
    if (!m_durationOffset || !m_filesizeOffset) {
        return err;
    }

    int64_t cur = m_fs->Tellg();

    // buffer to write the size.
    char* buf = new char[Amf0Size::Number()];
    AutoFreeA(char, buf);

    Buffer stream(buf, Amf0Size::Number());

    // filesize to buf.
    Amf0Any* size = Amf0Any::Number((double)cur);
    AutoFree(Amf0Any, size);

    stream.Skip(-1 * stream.Pos());
    if ((err = size->Write(&stream)) != SUCCESS) {
        return ERRORWRAP(err, "write filesize");
    }

    // update the flesize.
    m_fs->Seek2(m_filesizeOffset);
    if ((err = m_fs->Write(buf, Amf0Size::Number(), NULL)) != SUCCESS) {
        return ERRORWRAP(err, "update filesize");
    }

    // duration to buf
    Amf0Any* dur = Amf0Any::Number((double)u2ms(m_fragment->Duration()) / 1000.0);
    AutoFree(Amf0Any, dur);

    stream.Skip(-1 * stream.Pos());
    if ((err = dur->Write(&stream)) != SUCCESS) {
        return ERRORWRAP(err, "write duration");
    }

    // update the duration
    m_fs->Seek2(m_durationOffset);
    if ((err = m_fs->Write(buf, Amf0Size::Number(), NULL)) != SUCCESS) {
        return ERRORWRAP(err, "update duration");
    }

    // reset the offset.
    m_fs->Seek2(cur);

    return err;
}

error DvrFlvSegmenter::OpenEncoder()
{
    error err = SUCCESS;

    m_hasKeyframe = false;

    // update the duration and filesize offset.
    m_durationOffset = 0;
    m_filesizeOffset = 0;

    Freep(m_enc);
    m_enc = new FlvTransmuxer();

    if ((err = m_enc->Initialize(m_fs)) != SUCCESS) {
        return ERRORWRAP(err, "init encoder");
    }

    // write the flv header to writer.
    if ((err = m_enc->WriteHeader()) != SUCCESS) {
        return ERRORWRAP(err, "write flv header");
    }

    return err;
}

error DvrFlvSegmenter::EncodeMetadata(SharedPtrMessage *metadata)
{
    error err = SUCCESS;

    // Ignore when metadata already written.
    if (m_durationOffset || m_filesizeOffset) {
        return err;
    }

    Buffer stream(metadata->m_payload, metadata->m_size);

    Amf0Any* name = Amf0Any::Str();
    AutoFree(Amf0Any, name);
    if ((err = name->Read(&stream)) != SUCCESS) {
        return ERRORWRAP(err, "read name");
    }

    Amf0Object* obj = Amf0Any::Object();
    AutoFree(Amf0Object, obj);
    if ((err = obj->Read(&stream)) != SUCCESS) {
        return ERRORWRAP(err, "read object");
    }

    // remove duration and filesize.
    obj->Set("filesize", NULL);
    obj->Set("duration", NULL);

    // add properties.
    obj->Set("service", Amf0Any::Str(RTMP_SIG_SERVER));
    obj->Set("filesize", Amf0Any::Number(0));
    obj->Set("duration", Amf0Any::Number(0));

    int size = name->TotalSize() + obj->TotalSize();
    char* payload = new char[size];
    AutoFreeA(char, payload);

    // 11B flv header, 3B object EOF, 8B number value, 1B number flag.
    m_durationOffset = m_fs->Tellg() + size + 11 - Amf0Size::ObjectEof() - Amf0Size::Number();
    // 2B string flag, 8B number value, 8B string 'duration', 1B number flag
    m_filesizeOffset = m_durationOffset - Amf0Size::Utf8("duration") - Amf0Size::Number();

    // convert metadata to bytes.
    Buffer buf(payload, size);

    if ((err = name->Write(&buf)) != SUCCESS) {
        return ERRORWRAP(err, "write name");
    }
    if ((err = obj->Write(&buf)) != SUCCESS) {
        return ERRORWRAP(err, "write object");
    }

    // to flv file.
    if ((err = m_enc->WriteMetadata(18, payload, size)) != SUCCESS) {
        return ERRORWRAP(err, "write metadata");
    }

    return err;
}

error DvrFlvSegmenter::EncodeAudio(SharedPtrMessage *audio, Format *format)
{
    error err = SUCCESS;

    char* payload = audio->m_payload;
    int size = audio->m_size;
    if ((err = m_enc->WriteAudio(audio->m_timestamp, payload, size)) != SUCCESS) {
        return ERRORWRAP(err, "write audio");
    }

    return err;
}

error DvrFlvSegmenter::EncodeVideo(SharedPtrMessage *video, Format *format)
{
    error err = SUCCESS;

    char* payload = video->m_payload;
    int size = video->m_size;
    bool sh = (format->m_video->m_avcPacketType == VideoAvcFrameTraitSequenceHeader);
    bool keyframe = (!sh && format->m_video->m_frameType == VideoAvcFrameTypeKeyFrame);

    if (keyframe) {
        m_hasKeyframe = true;
    }

    // accept the sequence header here.
    // when got no keyframe, ignore when should wait keyframe.
    if (!m_hasKeyframe && !sh && m_waitKeyframe) {
        return err;
    }

    if ((err = m_enc->WriteVideo(video->m_timestamp, payload, size)) != SUCCESS) {
        return ERRORWRAP(err, "write video");
    }

    return err;
}

error DvrFlvSegmenter::CloseEncoder()
{
    return RefreshMetadata();
}

DvrMp4Segmenter::DvrMp4Segmenter()
{
//    m_enc = new Mp4Encoder();
}

DvrMp4Segmenter::~DvrMp4Segmenter()
{
    Freep(m_enc);
}

error DvrMp4Segmenter::RefreshMetadata()
{
    return SUCCESS;
}

error DvrMp4Segmenter::OpenEncoder()
{
//    error err = SUCCESS;

//    Freep(m_enc);
//    m_enc = new Mp4Encoder();

//    if ((err = m_enc->Initialize(fs)) != SUCCESS) {
//        return ERRORWRAP(err, "init encoder");
//    }

//    return err;
}

error DvrMp4Segmenter::EncodeMetadata(SharedPtrMessage *metadata)
{
    return SUCCESS;
}

error DvrMp4Segmenter::EncodeAudio(SharedPtrMessage *audio, Format *format)
{
    error err = SUCCESS;

//    AudioCodecId sound_format = format->m_acodec->m_id;
//    AudioSampleRate sound_rate = format->m_acodec->m_soundRate;
//    AudioSampleBits sound_size = format->m_acodec->m_soundSize;
//    AudioChannels channels = format->m_acodec->m_soundType;

//    AudioAacFrameTrait ct = format->m_audio->m_aacPacketType;
//    if (ct == AudioAacFrameTraitSequenceHeader || ct == AudioMp3FrameTrait) {
//        m_enc->m_acodec = sound_format;
//        m_enc->m_sampleRate = sound_rate;
//        m_enc->m_soundBits = sound_size;
//        m_enc->m_channels = channels;
//    }

//    uint8_t* sample = (uint8_t*)format->m_raw;
//    uint32_t nb_sample = (uint32_t)format->m_nbRaw;

//    uint32_t dts = (uint32_t)audio->m_timestamp;
//    if ((err = m_enc->write_sample(format, Mp4HandlerTypeSOUN, 0x00, ct, dts, dts, sample, nb_sample)) != SUCCESS) {
//        return ERRORWRAP(err, "write sample");
//    }

    return err;
}

error DvrMp4Segmenter::EncodeVideo(SharedPtrMessage *video, Format *format)
{
    error err = SUCCESS;

//    VideoAvcFrameType frame_type = format->m_video->m_frameType;
//    VideoCodecId codec_id = format->m_vcodec->m_id;

//    VideoAvcFrameTrait ct = format->m_video->m_avcPacketType;
//    uint32_t cts = (uint32_t)format->m_video->m_cts;

//    if (ct == VideoAvcFrameTraitSequenceHeader) {
//        m_enc->m_vcodec = codec_id;
//    }

//    uint32_t dts = (uint32_t)video->m_timestamp;
//    uint32_t pts = dts + cts;

//    uint8_t* sample = (uint8_t*)format->m_raw;
//    uint32_t nb_sample = (uint32_t)format->m_nbRaw;
//    if ((err = m_enc->write_sample(format, Mp4HandlerTypeVIDE, frame_type, ct, dts, pts, sample, nb_sample)) != SUCCESS) {
//        return ERRORWRAP(err, "write sample");
//    }

    return err;
}

error DvrMp4Segmenter::CloseEncoder()
{
    error err = SUCCESS;

//    if ((err = m_enc->flush()) != SUCCESS) {
//        return ERRORWRAP(err, "flush encoder");
//    }

    return err;
}

DvrAsyncCallOnDvr::DvrAsyncCallOnDvr(ContextId c, Request *r, std::string p)
{
    m_cid = c;
    m_req = r->Copy();
    m_path = p;
}

DvrAsyncCallOnDvr::~DvrAsyncCallOnDvr()
{
    Freep(m_req);
}

error DvrAsyncCallOnDvr::Call()
{
    error err = SUCCESS;

    if (!config->GetVhostHttpHooksEnabled(m_req->m_vhost)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        ConfDirective* conf = config->GetVhostOnDvr(m_req->m_vhost);
        if (conf) {
            hooks = conf->m_args;
        }
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = HttpHooks::OnDvr(m_cid, url, m_req, m_path)) != SUCCESS) {
            return ERRORWRAP(err, "callback on_dvr %s", url.c_str());
        }
    }

    return err;
}

std::string DvrAsyncCallOnDvr::ToString()
{
    std::stringstream ss;
    ss << "vhost=" << m_req->m_vhost << ", file=" << m_path;
    return ss.str();
}

DvrPlan::DvrPlan()
{
    m_req = NULL;
    m_hub = NULL;

    m_dvrEnabled = false;
    m_segment = NULL;
}

DvrPlan::~DvrPlan()
{
    Freep(m_segment);
    Freep(m_req);
}

error DvrPlan::Initialize(OriginHub *h, DvrSegmenter *s, Request *r)
{
    error err = SUCCESS;

    m_hub = h;
    m_req = r->Copy();
    m_segment = s;

    if ((err = m_segment->Initialize(this, r)) != SUCCESS) {
        return ERRORWRAP(err, "segmenter");
    }

    return err;
}

error DvrPlan::OnPublish(Request *r)
{
    // @see https://github.com/ossrs/srs/issues/1613#issuecomment-960623359
    Freep(m_req);
    m_req = r->Copy();

    return SUCCESS;
}

void DvrPlan::OnUnpublish()
{

}

error DvrPlan::OnMetaData(SharedPtrMessage *shared_metadata)
{
    error err = SUCCESS;

    if (!m_dvrEnabled) {
        return err;
    }

    return m_segment->WriteMetadata(shared_metadata);
}

error DvrPlan::OnAudio(SharedPtrMessage *shared_audio, Format *format)
{
    error err = SUCCESS;

    if (!m_dvrEnabled) {
        return err;
    }

    if ((err = m_segment->WriteAudio(shared_audio, format)) != SUCCESS) {
        return ERRORWRAP(err, "write audio");
    }

    return err;
}

error DvrPlan::OnVideo(SharedPtrMessage *shared_video, Format *format)
{
    error err = SUCCESS;

    if (!m_dvrEnabled) {
        return err;
    }

    if ((err = m_segment->WriteVideo(shared_video, format)) != SUCCESS) {
        return ERRORWRAP(err, "write video");
    }

    return err;
}

error DvrPlan::OnReapSegment()
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Fragment* fragment = m_segment->Current();
    std::string fullpath = fragment->Fullpath();

    if ((err = _dvr_async->Execute(new DvrAsyncCallOnDvr(cid, m_req, fullpath))) != SUCCESS) {
        return ERRORWRAP(err, "reap segment");
    }

    return err;
}

error DvrPlan::CreatePlan(std::string vhost, DvrPlan **pplan)
{
    std::string plan = config->GetDvrPlan(vhost);
    if (ConfigDvrIsPlanSegment(plan)) {
        *pplan = new DvrSegmentPlan();
    } else if (ConfigDvrIsPlanSession(plan)) {
        *pplan = new DvrSessionPlan();
    } else {
        return ERRORNEW(ERROR_DVR_ILLEGAL_PLAN, "illegal plan=%s, vhost=%s",
            plan.c_str(), vhost.c_str());
    }

    return SUCCESS;
}

DvrSessionPlan::DvrSessionPlan()
{

}

DvrSessionPlan::~DvrSessionPlan()
{

}

error DvrSessionPlan::OnPublish(Request *r)
{
    error err = SUCCESS;

    if ((err = DvrPlan::OnPublish(r)) != SUCCESS) {
        return err;
    }

    // support multiple publish.
    if (m_dvrEnabled) {
        return err;
    }

    if (!config->GetDvrEnabled(m_req->m_vhost)) {
        return err;
    }

    if ((err = m_segment->Close()) != SUCCESS) {
        return ERRORWRAP(err, "close segment");
    }

    if ((err = m_segment->Open()) != SUCCESS) {
        return ERRORWRAP(err, "open segment");
    }

    m_dvrEnabled = true;

    return err;
}

void DvrSessionPlan::OnUnpublish()
{
    // support multiple publish.
    if (!m_dvrEnabled) {
        return;
    }

    // ignore error.
    error err = m_segment->Close();
    if (err != SUCCESS) {
        warn("ignore flv close error %s", ERRORDESC(err).c_str());
    }

    m_dvrEnabled = false;

    // We should notify the on_dvr, then stop the async.
    // @see https://github.com/ossrs/srs/issues/1601
    DvrPlan::OnUnpublish();
}

DvrSegmentPlan::DvrSegmentPlan()
{
    m_cduration = 0;
    m_waitKeyframe = false;
    m_reopeningSegment = false;
}

DvrSegmentPlan::~DvrSegmentPlan()
{

}

error DvrSegmentPlan::Initialize(OriginHub *h, DvrSegmenter *s, Request *r)
{
    error err = SUCCESS;

    if ((err = DvrPlan::Initialize(h, s, r)) != SUCCESS) {
        return ERRORWRAP(err, "segment plan");
    }

    m_waitKeyframe = config->GetDvrWaitKeyframe(m_req->m_vhost);

    m_cduration = config->GetDvrDuration(m_req->m_vhost);

    return SUCCESS;
}

error DvrSegmentPlan::OnPublish(Request *r)
{
    error err = SUCCESS;

    if ((err = DvrPlan::OnPublish(r)) != SUCCESS) {
        return err;
    }

    // support multiple publish.
    if (m_dvrEnabled) {
        return err;
    }

    if (!config->GetDvrEnabled(m_req->m_vhost)) {
        return err;
    }

    if ((err = m_segment->Close()) != SUCCESS) {
        return ERRORWRAP(err, "segment close");
    }

    if ((err = m_segment->Open()) != SUCCESS) {
        return ERRORWRAP(err, "segment open");
    }

    m_dvrEnabled = true;

    return err;
}

void DvrSegmentPlan::OnUnpublish()
{
    error err = SUCCESS;

    if ((err = m_segment->Close()) != SUCCESS) {
        warn("ignore err %s", ERRORDESC(err).c_str());
        Freep(err);
    }

    m_dvrEnabled = false;

    // We should notify the on_dvr, then stop the async.
    // @see https://github.com/ossrs/srs/issues/1601
    DvrPlan::OnUnpublish();
}

error DvrSegmentPlan::OnAudio(SharedPtrMessage *shared_audio, Format *format)
{
    error err = SUCCESS;

    if ((err = UpdateDuration(shared_audio)) != SUCCESS) {
        return ERRORWRAP(err, "update duration");
    }

    if ((err = DvrPlan::OnAudio(shared_audio, format)) != SUCCESS) {
        return ERRORWRAP(err, "consume audio");
    }

    return err;
}

error DvrSegmentPlan::OnVideo(SharedPtrMessage *shared_video, Format *format)
{
    error err = SUCCESS;

    if ((err = UpdateDuration(shared_video)) != SUCCESS) {
        return ERRORWRAP(err, "update duration");
    }

    if ((err = DvrPlan::OnVideo(shared_video, format)) != SUCCESS) {
        return ERRORWRAP(err, "consume video");
    }

    return err;
}

error DvrSegmentPlan::UpdateDuration(SharedPtrMessage *msg)
{
    error err = SUCCESS;

    // When reopening the segment, never update the duration, because there is actually no media data.
    // @see https://github.com/ossrs/srs/issues/2717
    if (m_reopeningSegment) {
        return err;
    }

    Assert(m_segment);

    // ignore if duration ok.
    Fragment* fragment = m_segment->Current();
    if (m_cduration <= 0 || fragment->Duration() < m_cduration) {
        return err;
    }

    // when wait keyframe, ignore if no frame arrived.
    // @see https://github.com/ossrs/srs/issues/177
    if (m_waitKeyframe) {
        if (!msg->IsVideo()) {
            return err;
        }

        char* payload = msg->m_payload;
        int size = msg->m_size;
        bool is_key_frame = FlvVideo::H264(payload, size) && FlvVideo::Keyframe(payload, size) && !FlvVideo::Sh(payload, size);
        if (!is_key_frame) {
            return err;
        }
    }

    // reap segment
    if ((err = m_segment->Close()) != SUCCESS) {
        return ERRORWRAP(err, "segment close");
    }

    // open new flv file
    if ((err = m_segment->Open()) != SUCCESS) {
        return ERRORWRAP(err, "segment open");
    }

    // When update sequence header, set the reopening state to prevent infinitely recursive call.
    m_reopeningSegment = true;
    err = m_hub->OnDvrRequestSh();
    m_reopeningSegment = false;
    if (err != SUCCESS) {
        return ERRORWRAP(err, "request sh");
    }

    return err;
}

error DvrSegmentPlan::OnReloadVhostDvr(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    m_waitKeyframe = config->GetDvrWaitKeyframe(m_req->m_vhost);

    m_cduration = config->GetDvrDuration(m_req->m_vhost);

    return err;
}

Dvr::Dvr()
{
    m_hub = NULL;
    m_plan = NULL;
    m_req = NULL;
    m_actived = false;

    config->Subscribe(this);
}

Dvr::~Dvr()
{
    config->Unsubscribe(this);

    Freep(m_plan);
    Freep(m_req);
}

error Dvr::Initialize(OriginHub *h, Request *r)
{
    error err = SUCCESS;

    m_req = r->Copy();
    m_hub = h;

    ConfDirective* conf = config->GetDvrApply(r->m_vhost);
    m_actived = ConfigApplyFilter(conf, r);

    Freep(m_plan);
    if ((err = DvrPlan::CreatePlan(r->m_vhost, &m_plan)) != SUCCESS) {
        return ERRORWRAP(err, "create plan");
    }

    std::string path = config->GetDvrPath(r->m_vhost);
    DvrSegmenter* segmenter = NULL;
    if (StringEndsWith(path, ".mp4")) {
        segmenter = new DvrMp4Segmenter();
    } else {
        segmenter = new DvrFlvSegmenter();
    }

    if ((err = m_plan->Initialize(m_hub, segmenter, r)) != SUCCESS) {
        return ERRORWRAP(err, "plan initialize");
    }

    return err;
}

error Dvr::OnPublish(Request *r)
{
    error err = SUCCESS;

    // the dvr for this stream is not actived.
    if (!m_actived) {
        return err;
    }

    if ((err = m_plan->OnPublish(r)) != SUCCESS) {
        return ERRORWRAP(err, "publish");
    }

    Freep(m_req);
    m_req = r->Copy();

    return err;
}

void Dvr::OnUnpublish()
{
    m_plan->OnUnpublish();
}

error Dvr::OnMetaData(SharedPtrMessage *metadata)
{
    error err = SUCCESS;

    // the dvr for this stream is not actived.
    if (!m_actived) {
        return err;
    }

    if ((err = m_plan->OnMetaData(metadata)) != SUCCESS) {
        return ERRORWRAP(err, "metadata");
    }

    return err;
}

error Dvr::OnAudio(SharedPtrMessage *shared_audio, Format *format)
{
    // the dvr for this stream is not actived.
    if (!m_actived) {
        return SUCCESS;
    }

    return m_plan->OnAudio(shared_audio, format);
}

error Dvr::OnVideo(SharedPtrMessage *shared_video, Format *format)
{
    // the dvr for this stream is not actived.
    if (!m_actived) {
        return SUCCESS;
    }

    return m_plan->OnVideo(shared_video, format);
}

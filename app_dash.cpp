#include "app_dash.h"

#include "app_config.h"
#include "app_utility.h"
#include "codec.h"
#include "file.h"
#include "protocol_rtmp_stack.h"
#include "error.h"
#include "utility.h"
#include "core_autofree.h"
#include <string>
#include <sstream>
#include <inttypes.h>

InitMp4::InitMp4()
{
    m_fw = new FileWriter();
    //m_init = new Mp4M2tsInitEncoder();
}

InitMp4::~InitMp4()
{
    Freep(m_init);
    Freep(m_fw);
}

error InitMp4::Write(Format *format, bool video, int tid)
{
    error err = SUCCESS;

    std::string path_tmp = Tmppath();
    if ((err = m_fw->Open(path_tmp)) != SUCCESS) {
        return ERRORWRAP(err, "Open init mp4 failed, path=%s", path_tmp.c_str());
    }

//    if ((err = m_init->initialize(m_fw)) != SUCCESS) {
//        return ERRORWRAP(err, "init");
//    }

//    if ((err = m_init->write(format, video, tid)) != SUCCESS) {
//        return ERRORWRAP(err, "write init");
//    }

    return err;
}

FragmentedMp4::FragmentedMp4()
{
    m_fw = new FileWriter();
//    m_enc = new Mp4M2tsSegmentEncoder();
}

FragmentedMp4::~FragmentedMp4()
{
    Freep(m_enc);
    Freep(m_fw);
}

error FragmentedMp4::Initialize(Request *r, bool video, MpdWriter *mpd, uint32_t tid)
{
    error err = SUCCESS;

    std::string file_home;
    std::string file_name;
    int64_t sequence_number;
    utime_t basetime;
    if ((err = mpd->GetFragment(video, file_home, file_name, sequence_number, basetime)) != SUCCESS) {
        return ERRORWRAP(err, "get fragment");
    }

    std::string home = config->GetDashPath(r->m_vhost);
    SetPath(home + "/" + file_home + "/" + file_name);

    if ((err = CreateDir()) != SUCCESS) {
        return ERRORWRAP(err, "create dir");
    }

    std::string path_tmp = Tmppath();
    if ((err = m_fw->Open(path_tmp)) != SUCCESS) {
        return ERRORWRAP(err, "Open fmp4 failed, path=%s", path_tmp.c_str());
    }

//    if ((err = m_enc->initialize(m_fw, (uint32_t)sequence_number, basetime, tid)) != SUCCESS) {
//        return ERRORWRAP(err, "init encoder");
//    }

    return err;
}

error FragmentedMp4::Write(SharedPtrMessage *shared_msg, Format *format)
{
    error err = SUCCESS;

    if (shared_msg->IsAudio()) {
        uint8_t* sample = (uint8_t*)format->m_raw;
        uint32_t nb_sample = (uint32_t)format->m_nbRaw;

        uint32_t dts = (uint32_t)shared_msg->m_timestamp;
//        err = m_enc->write_sample(Mp4HandlerTypeSOUN, 0x00, dts, dts, sample, nb_sample);
    } else if (shared_msg->IsVideo()) {
        VideoAvcFrameType frame_type = format->m_video->m_frameType;
        uint32_t cts = (uint32_t)format->m_video->m_cts;

        uint32_t dts = (uint32_t)shared_msg->m_timestamp;
        uint32_t pts = dts + cts;

        uint8_t* sample = (uint8_t*)format->m_raw;
        uint32_t nb_sample = (uint32_t)format->m_nbRaw;
//        err = m_enc->write_sample(Mp4HandlerTypeVIDE, frame_type, dts, pts, sample, nb_sample);
    } else {
        return err;
    }

    Append(shared_msg->m_timestamp);

    return err;
}

error FragmentedMp4::Reap(uint64_t &dts)
{
    error err = SUCCESS;

//    if ((err = m_enc->flush(dts)) != SUCCESS) {
//        return ERRORWRAP(err, "Flush encoder failed");
//    }

    Freep(m_fw);

    if ((err = Rename()) != SUCCESS) {
        return ERRORWRAP(err, "rename");
    }

    return err;
}

MpdWriter::MpdWriter()
{
    m_req = NULL;
    m_timeshit = m_updatePeriod = m_fragment = 0;
    m_lastUpdateMpd = 0;
}

MpdWriter::~MpdWriter()
{

}

error MpdWriter::Initialize(Request *r)
{
    m_req = r;
    return SUCCESS;
}

error MpdWriter::OnPublish()
{
    Request* r = m_req;

    m_fragment = config->GetDashFragment(r->m_vhost);
    m_updatePeriod = config->GetDashUpdatePeriod(r->m_vhost);
    m_timeshit = config->GetDashTimeshift(r->m_vhost);
    m_home = config->GetDashPath(r->m_vhost);
    m_mpdFile = config->GetDashMpdFile(r->m_vhost);

    std::string mpd_path = PathBuildStream(m_mpdFile, m_req->m_vhost, m_req->m_app, m_req->m_stream);
    m_fragmentHome = PathDirname(mpd_path) + "/" + m_req->m_stream;

    trace("DASH: Config fragment=%" PRId64 ", period=%" PRId64, m_fragment, m_updatePeriod);

    return SUCCESS;
}

void MpdWriter::OnUnpublish()
{

}

error MpdWriter::Write(Format *format)
{
    error err = SUCCESS;

    // MPD is not expired?
    if (m_lastUpdateMpd != 0 && GetSystemTime() - m_lastUpdateMpd < m_updatePeriod) {
        return err;
    }
    m_lastUpdateMpd = GetSystemTime();

    std::string mpd_path = PathBuildStream(m_mpdFile, m_req->m_vhost, m_req->m_app, m_req->m_stream);
    std::string full_path = m_home + "/" + mpd_path;
    std::string full_home = PathDirname(full_path);

    m_fragmentHome = PathDirname(mpd_path) + "/" + m_req->m_stream;

    if ((err = CreateDirRecursivel(full_home)) != SUCCESS) {
        return ERRORWRAP(err, "Create MPD home failed, home=%s", full_home.c_str());
    }

    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << std::endl
    << "<MPD profiles=\"urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple\" " << std::endl
    << "    ns1:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" " << std::endl
    << "    xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:ns1=\"http://www.w3.org/2001/XMLSchema-instance\" " << std::endl
    << "    type=\"dynamic\" minimumUpdatePeriod=\"PT" << m_updatePeriod / UTIME_SECONDS << "S\" " << std::endl
    << "    timeShiftBufferDepth=\"PT" << m_timeshit / UTIME_SECONDS << "S\" availabilityStartTime=\"1970-01-01T00:00:00Z\" " << std::endl
    << "    maxSegmentDuration=\"PT" << m_fragment / UTIME_SECONDS << "S\" minBufferTime=\"PT" << m_fragment / UTIME_SECONDS << "S\" >" << std::endl
    << "    <BaseURL>" << m_req->m_stream << "/" << "</BaseURL>" << std::endl
    << "    <Period start=\"PT0S\">" << std::endl;
    if (format->m_acodec) {
        ss  << "        <AdaptationSet mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << std::endl;
        ss  << "            <SegmentTemplate duration=\"" << m_fragment / UTIME_SECONDS << "\" "
        << "initialization=\"$RepresentationID$-init.mp4\" "
        << "media=\"$RepresentationID$-$Number$.m4s\" />" << std::endl;
        ss  << "            <Representation id=\"audio\" bandwidth=\"48000\" codecs=\"mp4a.40.2\" />" << std::endl;
        ss  << "        </AdaptationSet>" << std::endl;
    }
    if (format->m_vcodec) {
        int w = format->m_vcodec->m_width;
        int h = format->m_vcodec->m_height;
        ss  << "        <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << std::endl;
        ss  << "            <SegmentTemplate duration=\"" << m_fragment / UTIME_SECONDS << "\" "
        << "initialization=\"$RepresentationID$-init.mp4\" "
        << "media=\"$RepresentationID$-$Number$.m4s\" />" << std::endl;
        ss  << "            <Representation id=\"video\" bandwidth=\"800000\" codecs=\"avc1.64001e\" "
        << "width=\"" << w << "\" height=\"" << h << "\"/>" << std::endl;
        ss  << "        </AdaptationSet>" << std::endl;
    }
    ss  << "    </Period>" << std::endl
    << "</MPD>" << std::endl;

    FileWriter* fw = new FileWriter();
    AutoFree(FileWriter, fw);

    std::string full_path_tmp = full_path + ".tmp";
    if ((err = fw->Open(full_path_tmp)) != SUCCESS) {
        return ERRORWRAP(err, "Open MPD file=%s failed", full_path_tmp.c_str());
    }

    std::string content = ss.str();
    if ((err = fw->Write((void*)content.data(), content.length(), NULL)) != SUCCESS) {
        return ERRORWRAP(err, "Write MPD file=%s failed", full_path.c_str());
    }

    if (::rename(full_path_tmp.c_str(), full_path.c_str()) < 0) {
        return ERRORNEW(ERROR_DASH_WRITE_FAILED, "Rename %s to %s failed", full_path_tmp.c_str(), full_path.c_str());
    }

    trace("DASH: Refresh MPD success, size=%dB, file=%s", content.length(), full_path.c_str());

    return err;
}

error MpdWriter::GetFragment(bool video, std::string &home, std::string &filename, int64_t &sn, utime_t &basetime)
{
    error err = SUCCESS;

    home = m_fragmentHome;

    // We name the segment as advanced N segments, because when we are generating segment at the current time,
    // the player may also request the current segment.
    Assert(m_fragment);
    int64_t number = (UpdateSystemTime() / m_fragment + 1);
    // TOOD: FIXME: Should keep the segments continuous, or player may fail.
    sn = number;

    // The base time aligned with sn.
    basetime = sn * m_fragment;

    if (video) {
        filename = "video-" + Int2Str(sn) + ".m4s";
    } else {
        filename = "audio-" + Int2Str(sn) + ".m4s";
    }

    return err;
}

DashController::DashController()
{
    m_req = NULL;
    m_videoTackId = 0;
    m_audioTrackId = 1;
    m_mpd = new MpdWriter();
    m_vcurrent = m_acurrent = NULL;
    m_vfragments = new FragmentWindow();
    m_afragments = new FragmentWindow();
    m_audioDts = m_videoDts = 0;
    m_fragment = 0;
}

DashController::~DashController()
{
    Freep(m_mpd);
    Freep(m_vcurrent);
    Freep(m_acurrent);
    Freep(m_vfragments);
    Freep(m_afragments);
}

error DashController::Initialize(Request *r)
{
    error err = SUCCESS;

    m_req = r;

    if ((err = m_mpd->Initialize(r)) != SUCCESS) {
        return ERRORWRAP(err, "mpd");
    }

    return err;
}

error DashController::OnPublish()
{
    error err = SUCCESS;

    Request* r = m_req;

    m_fragment = config->GetDashFragment(r->m_vhost);
    m_home = config->GetDashPath(r->m_vhost);

    if ((err = m_mpd->OnPublish()) != SUCCESS) {
        return ERRORWRAP(err, "mpd");
    }

    Freep(m_vcurrent);
    m_vcurrent = new FragmentedMp4();
    if ((err = m_vcurrent->Initialize(m_req, true, m_mpd, m_videoTackId)) != SUCCESS) {
        return ERRORWRAP(err, "video fragment");
    }

    Freep(m_acurrent);
    m_acurrent = new FragmentedMp4();
    if ((err = m_acurrent->Initialize(m_req, false, m_mpd, m_audioTrackId)) != SUCCESS) {
        return ERRORWRAP(err, "audio fragment");
    }

    return err;
}

void DashController::OnUnpublish()
{
    m_mpd->OnUnpublish();

    error err = SUCCESS;

    if ((err = m_vcurrent->Reap(m_videoDts)) != SUCCESS) {
        warn("reap video err %s", ERRORDESC(err).c_str());
        Freep(err);
    }
    Freep(m_vcurrent);

    if ((err = m_acurrent->Reap(m_audioDts)) != SUCCESS) {
        warn("reap audio err %s", ERRORDESC(err).c_str());
        Freep(err);
    }
    Freep(m_acurrent);
}

error DashController::OnAudio(SharedPtrMessage *shared_audio, Format *format)
{
    error err = SUCCESS;

    if (format->IsAacSequenceHeader()) {
        return RefreshInitMp4(shared_audio, format);
    }

    if (m_acurrent->Duration() >= m_fragment) {
        if ((err = m_acurrent->Reap(m_audioDts)) != SUCCESS) {
            return ERRORWRAP(err, "reap current");
        }

        m_afragments->Append(m_acurrent);
        m_acurrent = new FragmentedMp4();

        if ((err = m_acurrent->Initialize(m_req, false, m_mpd, m_audioTrackId)) != SUCCESS) {
            return ERRORWRAP(err, "Initialize the audio fragment failed");
        }
    }

    if ((err = m_acurrent->Write(shared_audio, format)) != SUCCESS) {
        return ERRORWRAP(err, "Write audio to fragment failed");
    }

    if ((err = RefreshMpd(format)) != SUCCESS) {
        return ERRORWRAP(err, "Refresh the MPD failed");
    }

    return err;
}

error DashController::OnVideo(SharedPtrMessage *shared_video, Format *format)
{
    error err = SUCCESS;

    if (format->IsAvcSequenceHeader()) {
        return RefreshInitMp4(shared_video, format);
    }

    bool reopen = format->m_video->m_frameType == VideoAvcFrameTypeKeyFrame && m_vcurrent->Duration() >= m_fragment;
    if (reopen) {
        if ((err = m_vcurrent->Reap(m_videoDts)) != SUCCESS) {
            return ERRORWRAP(err, "reap current");
        }

        m_vfragments->Append(m_vcurrent);
        m_vcurrent = new FragmentedMp4();

        if ((err = m_vcurrent->Initialize(m_req, true, m_mpd, m_videoTackId)) != SUCCESS) {
            return ERRORWRAP(err, "Initialize the video fragment failed");
        }
    }

    if ((err = m_vcurrent->Write(shared_video, format)) != SUCCESS) {
        return ERRORWRAP(err, "Write video to fragment failed");
    }

    if ((err = RefreshMpd(format)) != SUCCESS) {
        return ERRORWRAP(err, "Refresh the MPD failed");
    }

    return err;
}

error DashController::RefreshMpd(Format *format)
{
    error err = SUCCESS;

    // TODO: FIXME: Support pure audio streaming.
    if (!format->m_acodec || !format->m_vcodec) {
        return err;
    }

    if ((err = m_mpd->Write(format)) != SUCCESS) {
        return ERRORWRAP(err, "write mpd");
    }

    return err;
}

error DashController::RefreshInitMp4(SharedPtrMessage *msg, Format *format)
{
    error err = SUCCESS;

    if (msg->m_size <= 0 || (msg->IsVideo() && !format->m_vcodec->IsAvcCodecOk())
        || (msg->IsAudio() && !format->m_acodec->IsAacCodecOk())) {
        warn("DASH: Ignore empty sequence header.");
        return err;
    }

    std::string full_home = m_home + "/" + m_req->m_app + "/" + m_req->m_stream;
    if ((err = CreateDirRecursivel(full_home)) != SUCCESS) {
        return ERRORWRAP(err, "Create media home failed, home=%s", full_home.c_str());
    }

    std::string path = full_home;
    if (msg->IsVideo()) {
        path += "/video-init.mp4";
    } else {
        path += "/audio-init.mp4";
    }

    InitMp4* init_mp4 = new InitMp4();
    AutoFree(InitMp4, init_mp4);

    init_mp4->SetPath(path);

    int tid = msg->IsVideo()? m_videoTackId:m_audioTrackId;
    if ((err = init_mp4->Write(format, msg->IsVideo(), tid)) != SUCCESS) {
        return ERRORWRAP(err, "write init");
    }

    if ((err = init_mp4->Rename()) != SUCCESS) {
        return ERRORWRAP(err, "rename init");
    }

    trace("DASH: Refresh media success, file=%s", path.c_str());

    return err;
}

Dash::Dash()
{
    m_hub = NULL;
    m_req = NULL;
    m_controller = new DashController();

    m_enabled = false;
}

Dash::~Dash()
{
    Freep(m_controller);
}

error Dash::Initialize(OriginHub *h, Request *r)
{
    error err = SUCCESS;

    m_hub = h;
    m_req = r;

    if ((err = m_controller->Initialize(m_req)) != SUCCESS) {
        return ERRORWRAP(err, "controller");
    }

    return err;
}

error Dash::OnPublish()
{
    error err = SUCCESS;

    // Prevent duplicated publish.
    if (m_enabled) {
        return err;
    }

    if (!config->GetDashEnabled(m_req->m_vhost)) {
        return err;
    }
    m_enabled = true;

    if ((err = m_controller->OnPublish()) != SUCCESS) {
        return ERRORWRAP(err, "controller");
    }

    return err;
}

error Dash::OnAudio(SharedPtrMessage *shared_audio, Format *format)
{
    error err = SUCCESS;

    if (!m_enabled) {
        return err;
    }

    if (!format->m_acodec) {
        return err;
    }

    if ((err = m_controller->OnAudio(shared_audio, format)) != SUCCESS) {
        return ERRORWRAP(err, "Consume audio failed");
    }

    return err;
}

error Dash::OnVideo(SharedPtrMessage *shared_video, Format *format)
{
    error err = SUCCESS;

    if (!m_enabled) {
        return err;
    }

    if (!format->m_vcodec) {
        return err;
    }

    if ((err = m_controller->OnVideo(shared_video, format)) != SUCCESS) {
        return ERRORWRAP(err, "Consume video failed");
    }

    return err;
}

void Dash::OnUnpublish()
{
    // Prevent duplicated unpublish.
    if (!m_enabled) {
        return;
    }

    m_enabled = false;

    m_controller->OnUnpublish();
}

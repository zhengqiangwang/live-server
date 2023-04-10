#ifndef APP_DASH_H
#define APP_DASH_H

#include "app_fragment.h"

class Request;
class OriginHub;
class SharedPtrMessage;
class Format;
class FileWriter;
class MpdWriter;
class Mp4M2tsInitEncoder;
class Mp4M2tsSegmentEncoder;

// The init mp4 for FMP4.
class InitMp4 : public Fragment
{
private:
    FileWriter* m_fw;
    Mp4M2tsInitEncoder* m_init;
public:
    InitMp4();
    virtual ~InitMp4();
public:
    // Write the init mp4 file, with the tid(track id).
    virtual error Write(Format* format, bool video, int tid);
};

// The FMP4(Fragmented MP4) for DASH streaming.
class FragmentedMp4 : public Fragment
{
private:
    FileWriter* m_fw;
    Mp4M2tsSegmentEncoder* m_enc;
public:
    FragmentedMp4();
    virtual ~FragmentedMp4();
public:
    // Initialize the fragment, create the home dir, open the file.
    virtual error Initialize(Request* r, bool video, MpdWriter* mpd, uint32_t tid);
    // Write media message to fragment.
    virtual error Write(SharedPtrMessage* shared_msg, Format* format);
    // Reap the fragment, close the fd and rename tmp to official file.
    virtual error Reap(uint64_t& dts);
};

// The writer to write MPD for DASH.
class MpdWriter
{
private:
    Request* m_req;
    utime_t m_lastUpdateMpd;
private:
    // The duration of fragment in utime_t.
    utime_t m_fragment;
    // The period to update the mpd in utime_t.
    utime_t m_updatePeriod;
    // The timeshift buffer depth in utime_t.
    utime_t m_timeshit;
    // The base or home dir for dash to write files.
    std::string m_home;
    // The MPD path template, from which to build the file path.
    std::string m_mpdFile;
private:
    // The home for fragment, relative to home.
    std::string m_fragmentHome;
public:
    MpdWriter();
    virtual ~MpdWriter();
public:
    virtual error Initialize(Request* r);
    virtual error OnPublish();
    virtual void OnUnpublish();
    // Write MPD according to parsed format of stream.
    virtual error Write(Format* format);
public:
    // Get the fragment relative home and filename.
    // The basetime is the absolute time in utime_t, while the sn(sequence number) is basetime/fragment.
    virtual error GetFragment(bool video, std::string& home, std::string& filename, int64_t& sn, utime_t& basetime);
};

// The controller for DASH, control the MPD and FMP4 generating system.
class DashController
{
private:
    Request* m_req;
    MpdWriter* m_mpd;
private:
    FragmentedMp4* m_vcurrent;
    FragmentWindow* m_vfragments;
    FragmentedMp4* m_acurrent;
    FragmentWindow* m_afragments;
    uint64_t m_audioDts;
    uint64_t m_videoDts;
private:
    // The fragment duration in utime_t to reap it.
    utime_t m_fragment;
private:
    std::string m_home;
    int m_videoTackId;
    int m_audioTrackId;
public:
    DashController();
    virtual ~DashController();
public:
    virtual error Initialize(Request* r);
    virtual error OnPublish();
    virtual void OnUnpublish();
    virtual error OnAudio(SharedPtrMessage* shared_audio, Format* format);
    virtual error OnVideo(SharedPtrMessage* shared_video, Format* format);
private:
    virtual error RefreshMpd(Format* format);
    virtual error RefreshInitMp4(SharedPtrMessage* msg, Format* format);
};

// The MPEG-DASH encoder, transmux RTMP to DASH.
class Dash
{
private:
    bool m_enabled;
private:
    Request* m_req;
    OriginHub* m_hub;
    DashController* m_controller;
public:
    Dash();
    virtual ~Dash();
public:
    // Initalize the encoder.
    virtual error Initialize(OriginHub* h, Request* r);
    // When stream start publishing.
    virtual error OnPublish();
    // When got an shared audio message.
    virtual error OnAudio(SharedPtrMessage* shared_audio, Format* format);
    // When got an shared video message.
    virtual error OnVideo(SharedPtrMessage* shared_video, Format* format);
    // When stream stop publishing.
    virtual void OnUnpublish();
};

#endif // APP_DASH_H

#ifndef APP_DVR_H
#define APP_DVR_H


#include "app_async_call.h"
#include "app_source.h"
#include "log.h"

class LiveSource;
class OriginHub;
class Request;
class Buffer;
class RtmpJitter;
class SharedPtrMessage;
class FileWriter;
class FlvTransmuxer;
class DvrPlan;
class JsonAny;
class JsonObject;
class Thread;
class Mp4Encoder;
class Fragment;
class Format;

// The segmenter for DVR, to write a segment file in flv/mp4.
class DvrSegmenter : public IReloadHandler
{
protected:
    // The underlayer file object.
    FileWriter* m_fs;
    // Whether wait keyframe to reap segment.
    bool m_waitKeyframe;
    // The FLV/MP4 fragment file.
    Fragment* m_fragment;
private:
    Request* m_req;
    DvrPlan* m_plan;
private:
    RtmpJitter* m_jitter;
    RtmpJitterAlgorithm m_jitterAlgorithm;
public:
    DvrSegmenter();
    virtual ~DvrSegmenter();
public:
    // Initialize the segment.
    virtual error Initialize(DvrPlan* p, Request* r);
    // Get the current framgnet.
    virtual Fragment* Current();
    // Open new segment file.
    // @param use_tmp_file Whether use tmp file for DVR, and rename when close.
    // @remark Ignore when file is already open.
    virtual error Open();
    // Write the metadata.
    virtual error WriteMetadata(SharedPtrMessage* metadata);
    // Write audio packet.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual error WriteAudio(SharedPtrMessage* shared_audio, Format* format);
    // Write video packet.
    // @param shared_video, directly ptr, copy it if need to save it.
    virtual error WriteVideo(SharedPtrMessage* shared_video, Format* format);
    // Refresh the metadata. For example, there is duration in flv metadata,
    // when DVR in append mode, the duration must be update every some seconds.
    // @remark Maybe ignored by concreate segmenter.
    virtual error RefreshMetadata() = 0;
    // Close current segment.
    // @remark ignore when already closed.
    virtual error Close();
protected:
    virtual error OpenEncoder() = 0;
    virtual error EncodeMetadata(SharedPtrMessage* metadata) = 0;
    virtual error EncodeAudio(SharedPtrMessage* audio, Format* format) = 0;
    virtual error EncodeVideo(SharedPtrMessage* video, Format* format) = 0;
    virtual error CloseEncoder() = 0;
private:
    // Generate the flv segment path.
    virtual std::string GeneratePath();
    // When update the duration of segment by rtmp msg.
    virtual error OnUpdateDuration(SharedPtrMessage* msg);
// Interface IReloadHandler
public:
    virtual error OnReloadVhostDvr(std::string vhost);
};

// The FLV segmenter to use FLV encoder to write file.
class DvrFlvSegmenter : public DvrSegmenter
{
private:
    // The FLV encoder, for FLV target.
    FlvTransmuxer* m_enc;
private:
    // The offset of file for duration value.
    // The next 8 bytes is the double value.
    int64_t m_durationOffset;
    // The offset of file for filesize value.
    // The next 8 bytes is the double value.
    int64_t m_filesizeOffset;
    // Whether current segment has keyframe.
    bool m_hasKeyframe;
public:
    DvrFlvSegmenter();
    virtual ~DvrFlvSegmenter();
public:
    virtual error RefreshMetadata();
protected:
    virtual error OpenEncoder();
    virtual error EncodeMetadata(SharedPtrMessage* metadata);
    virtual error EncodeAudio(SharedPtrMessage* audio, Format* format);
    virtual error EncodeVideo(SharedPtrMessage* video, Format* format);
    virtual error CloseEncoder();
};

// The MP4 segmenter to use MP4 encoder to write file.
class DvrMp4Segmenter : public DvrSegmenter
{
private:
    // The MP4 encoder, for MP4 target.
    Mp4Encoder* m_enc;
public:
    DvrMp4Segmenter();
    virtual ~DvrMp4Segmenter();
public:
    virtual error RefreshMetadata();
protected:
    virtual error OpenEncoder();
    virtual error EncodeMetadata(SharedPtrMessage* metadata);
    virtual error EncodeAudio(SharedPtrMessage* audio, Format* format);
    virtual error EncodeVideo(SharedPtrMessage* video, Format* format);
    virtual error CloseEncoder();
};

// the dvr async call.
class DvrAsyncCallOnDvr : public IAsyncCallTask
{
private:
    ContextId m_cid;
    std::string m_path;
    Request* m_req;
public:
    DvrAsyncCallOnDvr(ContextId c, Request* r, std::string p);
    virtual ~DvrAsyncCallOnDvr();
public:
    virtual error Call();
    virtual std::string ToString();
};

// The DVR plan, when and how to reap segment.
class DvrPlan : public IReloadHandler
{
public:
    Request* m_req;
protected:
    OriginHub* m_hub;
    DvrSegmenter* m_segment;
    bool m_dvrEnabled;
public:
    DvrPlan();
    virtual ~DvrPlan();
public:
    virtual error Initialize(OriginHub* h, DvrSegmenter* s, Request* r);
    virtual error OnPublish(Request* r);
    virtual void OnUnpublish();
    virtual error OnMetaData(SharedPtrMessage* shared_metadata);
    virtual error OnAudio(SharedPtrMessage* shared_audio, Format* format);
    virtual error OnVideo(SharedPtrMessage* shared_video, Format* format);
// Internal interface for segmenter.
public:
    // When segmenter close a segment.
    virtual error OnReapSegment();
public:
    static error CreatePlan(std::string vhost, DvrPlan** pplan);
};

// The DVR session plan: reap flv when session complete(unpublish)
class DvrSessionPlan : public DvrPlan
{
public:
    DvrSessionPlan();
    virtual ~DvrSessionPlan();
public:
    virtual error OnPublish(Request* r);
    virtual void OnUnpublish();
};

// The DVR segment plan: reap flv when duration exceed.
class DvrSegmentPlan : public DvrPlan
{
private:
    // in config, in utime_t
    utime_t m_cduration;
    bool m_waitKeyframe;
    // Whether reopening the DVR file.
    bool m_reopeningSegment;
public:
    DvrSegmentPlan();
    virtual ~DvrSegmentPlan();
public:
    virtual error Initialize(OriginHub* h, DvrSegmenter* s, Request* r);
    virtual error OnPublish(Request* r);
    virtual void OnUnpublish();
    virtual error OnAudio(SharedPtrMessage* shared_audio, Format* format);
    virtual error OnVideo(SharedPtrMessage* shared_video, Format* format);
private:
    virtual error UpdateDuration(SharedPtrMessage* msg);
// Interface IReloadHandler
public:
    virtual error OnReloadVhostDvr(std::string vhost);
};

// DVR(Digital Video Recorder) to record RTMP stream to flv/mp4 file.
class Dvr : public IReloadHandler
{
private:
    OriginHub* m_hub;
    DvrPlan* m_plan;
    Request* m_req;
private:
    // whether the dvr is actived by filter, which is specified by dvr_apply.
    // we always initialize the dvr, which crote plan and segment object,
    // but they never create actual piece of file util the apply active it.
    bool m_actived;
public:
    Dvr();
    virtual ~Dvr();
public:
    // initialize dvr, create dvr plan.
    // when system initialize(encoder publish at first time, or reload),
    // initialize the dvr will reinitialize the plan, the whole dvr framework.
    virtual error Initialize(OriginHub* h, Request* r);
    // publish stream event,
    // when encoder start to publish RTMP stream.
    // @param fetch_sequence_header whether fetch sequence from source.
    virtual error OnPublish(Request* r);
    // the unpublish event.,
    // when encoder stop(unpublish) to publish RTMP stream.
    virtual void OnUnpublish();
    // get some information from metadata, it's optinal.
    virtual error OnMetaData(SharedPtrMessage* metadata);
    // mux the audio packets to dvr.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual error OnAudio(SharedPtrMessage* shared_audio, Format* format);
    // mux the video packets to dvr.
    // @param shared_video, directly ptr, copy it if need to save it.
    virtual error OnVideo(SharedPtrMessage* shared_video, Format* format);
};

extern AsyncCallWorker* _dvr_async;


#endif // APP_DVR_H

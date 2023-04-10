#ifndef APP_SOURCE_H
#define APP_SOURCE_H


#include "app_hourglass.h"
#include "core_time.h"
#include "protocol_st.h"
#include "protocol_utility.h"
#include "app_reload.h"
#include <string>

class Format;
class RtmpFormat;
class LiveConsumer;
class PlayEdge;
class PublishEdge;
class LiveSource;
class CommonMessage;
class OnMetaDataPacket;
class SharedPtrMessage;
class Forwarder;
class Request;
class StSocket;
class RtmpServer;
class EdgeProxyContext;
class MessageArray;
class NgExec;
class MessageHeader;
class Hls;
class Rtc;
class Dvr;
class Dash;
class Encoder;
class Buffer;
#ifdef SRS_HDS
class SrsHds;
#endif
#define PERF_QUEUE_FAST_VECTOR
#define PERF_QUEUE_COND_WAIT

// The time jitter algorithm:
// 1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
// 2. zero, only ensure sttream start at zero, ignore timestamp jitter.
// 3. off, disable the time jitter algorithm, like atc.
enum RtmpJitterAlgorithm
{
    RtmpJitterAlgorithmFULL = 0x01,
    RtmpJitterAlgorithmZERO,
    RtmpJitterAlgorithmOFF
};
int TimeJitterString2int(std::string time_jitter);

// Time jitter detect and correct, to ensure the rtmp stream is monotonically.
class RtmpJitter
{
private:
    int64_t m_lastPktTime;
    int64_t m_lastPktCorrectTime;
public:
    RtmpJitter();
    virtual ~RtmpJitter();
public:
    // detect the time jitter and correct it.
    // @param ag the algorithm to use for time jitter.
    virtual error Correct(SharedPtrMessage* msg, RtmpJitterAlgorithm ag);
    // Get current client time, the last packet time.
    virtual int64_t GetTime();
};

#ifdef PERF_QUEUE_FAST_VECTOR
// To alloc and increase fixed space, fast remove and insert for msgs sender.
class FastVector
{
private:
    SharedPtrMessage** m_msgs;
    int m_nbMsgs;
    int m_count;
public:
    FastVector();
    virtual ~FastVector();
public:
    virtual int Size();
    virtual int Begin();
    virtual int End();
    virtual SharedPtrMessage** Data();
    virtual SharedPtrMessage* At(int index);
    virtual void Clear();
    virtual void Erase(int _begin, int _end);
    virtual void PushBack(SharedPtrMessage* msg);
    virtual void Free();
};
#endif

// The message queue for the consumer(client), forwarder.
// We limit the size in seconds, drop old messages(the whole gop) if full.
class MessageQueue
{
private:
    // The start and end time.
    utime_t m_avStartTime;
    utime_t m_avEndTime;
private:
    // Whether do logging when shrinking.
    bool m_ignoreShrink;
    // The max queue size, shrink if exceed it.
    utime_t m_maxQueueSize;
#ifdef PERF_QUEUE_FAST_VECTOR
    FastVector m_msgs;
#else
    std::vector<SharedPtrMessage*> m_msgs;
#endif
public:
    MessageQueue(bool ignore_shrink = false);
    virtual ~MessageQueue();
public:
    // Get the size of queue.
    virtual int Size();
    // Get the duration of queue.
    virtual utime_t Duration();
    // Set the queue size
    // @param queue_size the queue size in srs_utime_t.
    virtual void SetQueueSize(utime_t queue_size);
public:
    // Enqueue the message, the timestamp always monotonically.
    // @param msg, the msg to enqueue, user never free it whatever the return code.
    // @param is_overflow, whether overflow and shrinked. NULL to ignore.
    virtual error Enqueue(SharedPtrMessage* msg, bool* is_overflow = NULL);
    // Get packets in consumer queue.
    // @pmsgs SrsSharedPtrMessage*[], used to store the msgs, user must alloc it.
    // @count the count in array, output param.
    // @max_count the max count to dequeue, must be positive.
    virtual error DumpPackets(int max_count, SharedPtrMessage** pmsgs, int& count);
    // Dumps packets to consumer, use specified args.
    // @remark the atc/tba/tbv/ag are same to SrsLiveConsumer.enqueue().
    virtual error DumpPackets(LiveConsumer* consumer, bool atc, RtmpJitterAlgorithm ag);
private:
    // Remove a gop from the front.
    // if no iframe found, clear it.
    virtual void Shrink();
public:
    // clear all messages in queue.
    virtual void Clear();
};

// The wakable used for some object
// which is waiting on cond.
class IWakable
{
public:
    IWakable();
    virtual ~IWakable();
public:
    // when the consumer(for player) got msg from recv thread,
    // it must be processed for maybe it's a close msg, so the cond
    // wait must be wakeup.
    virtual void Wakeup() = 0;
};

// The consumer for SrsLiveSource, that is a play client.
class LiveConsumer : public IWakable
{
private:
    RtmpJitter* m_jitter;
    LiveSource* m_source;
    MessageQueue* m_queue;
    bool m_paused;
    // when source id changed, notice all consumers
    bool m_shouldUpdateSourceId;
#ifdef PERF_QUEUE_COND_WAIT
    // The cond wait for mw.
    st_cond_t m_mwWait;
    bool m_mwWaiting;
    int m_mwMinMsgs;
    utime_t m_mwDuration;
#endif
public:
    LiveConsumer(LiveSource* s);
    virtual ~LiveConsumer();
public:
    // Set the size of queue.
    virtual void SetQueueSize(utime_t queue_size);
    // when source id changed, notice client to print.
    virtual void UpdateSourceId();
public:
    // Get current client time, the last packet time.
    virtual int64_t GetTime();
    // Enqueue an shared ptr message.
    // @param shared_msg, directly ptr, copy it if need to save it.
    // @param whether atc, donot use jitter correct if true.
    // @param ag the algorithm of time jitter.
    virtual error Enqueue(SharedPtrMessage* shared_msg, bool atc, RtmpJitterAlgorithm ag);
    // Get packets in consumer queue.
    // @param msgs the msgs array to dump packets to send.
    // @param count the count in array, intput and output param.
    // @remark user can specifies the count to get specified msgs; 0 to get all if possible.
    virtual error DumpPackets(MessageArray* msgs, int& count);
#ifdef PERF_QUEUE_COND_WAIT
    // wait for messages incomming, atleast nb_msgs and in duration.
    // @param nb_msgs the messages count to wait.
    // @param msgs_duration the messages duration to wait.
    virtual void Wait(int nb_msgs, utime_t msgs_duration);
#endif
    // when client send the pause message.
    virtual error OnPlayClientPause(bool is_pause);
// Interface ISrsWakable
public:
    // when the consumer(for player) got msg from recv thread,
    // it must be processed for maybe it's a close msg, so the cond
    // wait must be wakeup.
    virtual void Wakeup();
};

// cache a gop of video/audio data,
// delivery at the connect of flash player,
// To enable it to fast startup.
class GopCache
{
private:
    // if disabled the gop cache,
    // The client will wait for the next keyframe for h264,
    // and will be black-screen.
    bool m_enableGopCache;
    // The video frame count, avoid cache for pure audio stream.
    int m_cachedVideoCount;
    // when user disabled video when publishing, and gop cache enalbed,
    // We will cache the audio/video for we already got video, but we never
    // know when to clear the gop cache, for there is no video in future,
    // so we must guess whether user disabled the video.
    // when we got some audios after laster video, for instance, 600 audio packets,
    // about 3s(26ms per packet) 115 audio packets, clear gop cache.
    //
    // @remark, it is ok for performance, for when we clear the gop cache,
    //       gop cache is disabled for pure audio stream.
    // @see: https://github.com/ossrs/srs/issues/124
    int m_audioAfterLastVideoCount;
    // cached gop.
    std::vector<SharedPtrMessage*> m_gopCache;
public:
    GopCache();
    virtual ~GopCache();
public:
    // cleanup when system quit.
    virtual void Dispose();
    // To enable or disable the gop cache.
    virtual void Set(bool v);
    virtual bool Enabled();
    // only for h264 codec
    // 1. cache the gop when got h264 video packet.
    // 2. clear gop when got keyframe.
    // @param shared_msg, directly ptr, copy it if need to save it.
    virtual error Cache(SharedPtrMessage* shared_msg);
    // clear the gop cache.
    virtual void Clear();
    // dump the cached gop to consumer.
    virtual error Dump(LiveConsumer* consumer, bool atc, RtmpJitterAlgorithm jitter_algorithm);
    // used for atc to get the time of gop cache,
    // The atc will adjust the sequence header timestamp to gop cache.
    virtual bool Empty();
    // Get the start time of gop cache, in srs_utime_t.
    // @return 0 if no packets.
    virtual utime_t StartTime();
    // whether current stream is pure audio,
    // when no video in gop cache, the stream is pure audio right now.
    virtual bool PureAudio();
};

// The handler to handle the event of srs source.
// For example, the http flv streaming module handle the event and
// mount http when rtmp start publishing.
class ILiveSourceHandler
{
public:
    ILiveSourceHandler();
    virtual ~ILiveSourceHandler();
public:
    // when stream start publish, mount stream.
    virtual error OnPublish(LiveSource* s, Request* r) = 0;
    // when stream stop publish, unmount stream.
    virtual void OnUnpublish(LiveSource* s, Request* r) = 0;
};

// The mix queue to correct the timestamp for mix_correct algorithm.
class MixQueue
{
private:
    uint32_t m_nbVideos;
    uint32_t m_nbAudios;
    std::multimap<int64_t, SharedPtrMessage*> msgs;
public:
    MixQueue();
    virtual ~MixQueue();
public:
    virtual void Clear();
    virtual void Push(SharedPtrMessage* msg);
    virtual SharedPtrMessage* Pop();
};

// The hub for origin is a collection of utilities for origin only,
// For example, DVR, HLS, Forward and Transcode are only available for origin,
// they are meanless for edge server.
class OriginHub : public IReloadHandler
{
private:
    LiveSource* m_source;
    Request* m_req;
    bool m_isActive;
private:
    // The format, codec information.
    RtmpFormat* m_format;
    // hls handler.
    Hls* m_hls;
    // The DASH encoder.
    Dash* m_dash;
    // dvr handler.
    Dvr* m_dvr;
    // transcoding handler.
    Encoder* m_encoder;
#ifdef SRS_HDS
    // adobe hds(http dynamic streaming).
    SrsHds *hds;
#endif
    // nginx-rtmp exec feature.
    NgExec* m_ngExec;
    // To forward stream to other servers
    std::vector<Forwarder*> m_forwarders;
public:
    OriginHub();
    virtual ~OriginHub();
public:
    // Initialize the hub with source and request.
    // @param r The request object, managed by source.
    virtual error Initialize(LiveSource* s, Request* r);
    // Dispose the hub, release utilities resource,
    // For example, delete all HLS pieces.
    virtual void Dispose();
    // Cycle the hub, process some regular events,
    // For example, dispose hls in cycle.
    virtual error Cycle();
    // Whether the stream hub is active, or stream is publishing.
    virtual bool Active();
public:
    // When got a parsed metadata.
    virtual error OnMetaData(SharedPtrMessage* shared_metadata, OnMetaDataPacket* packet);
    // When got a parsed audio packet.
    virtual error OnAudio(SharedPtrMessage* shared_audio);
    // When got a parsed video packet.
    virtual error OnVideo(SharedPtrMessage* shared_video, bool is_sequence_header);
public:
    // When start publish stream.
    virtual error OnPublish();
    // When stop publish stream.
    virtual void OnUnpublish();
// Internal callback.
public:
    // For the SrsForwarder to callback to request the sequence headers.
    virtual error OnForwarderStart(Forwarder* forwarder);
    // For the SrsDvr to callback to request the sequence headers.
    virtual error OnDvrRequestSh();
// Interface ISrsReloadHandler
public:
    virtual error OnReloadVhostForward(std::string vhost);
    virtual error OnReloadVhostDash(std::string vhost);
    virtual error OnReloadVhostHls(std::string vhost);
    virtual error OnReloadVhostHds(std::string vhost);
    virtual error OnReloadVhostDvr(std::string vhost);
    virtual error OnReloadVhostTranscode(std::string vhost);
    virtual error OnReloadVhostExec(std::string vhost);
private:
    virtual error CreateForwarders();
    virtual error CreateBackendForwarders(bool& applied);
    virtual void DestroyForwarders();
};

// Each stream have optional meta(sps/pps in sequence header and metadata).
// This class cache and update the meta.
class MetaCache
{
private:
    // The cached metadata, FLV script data tag.
    SharedPtrMessage* m_meta;
    // The cached video sequence header, for example, sps/pps for h.264.
    SharedPtrMessage* m_video;
    SharedPtrMessage* m_previousVideo;
    // The cached audio sequence header, for example, asc for aac.
    SharedPtrMessage* m_audio;
    SharedPtrMessage* m_previousAudio;
    // The format for sequence header.
    RtmpFormat* m_vformat;
    RtmpFormat* m_aformat;
public:
    MetaCache();
    virtual ~MetaCache();
public:
    // Dispose the metadata cache.
    virtual void Dispose();
    // For each publishing, clear the metadata cache.
    virtual void Clear();
public:
    // Get the cached metadata.
    virtual SharedPtrMessage* Data();
    // Get the cached vsh(video sequence header).
    virtual SharedPtrMessage* Vsh();
    virtual Format* VshFormat();
    // Get the cached ash(audio sequence header).
    virtual SharedPtrMessage* Ash();
    virtual Format* AshFormat();
    // Dumps cached metadata to consumer.
    // @param dm Whether dumps the metadata.
    // @param ds Whether dumps the sequence header.
    virtual error Dumps(LiveConsumer* consumer, bool atc, RtmpJitterAlgorithm ag, bool dm, bool ds);
public:
    // Previous exists sequence header.
    virtual SharedPtrMessage* PreviousVsh();
    virtual SharedPtrMessage* PreviousAsh();
    // Update previous sequence header, drop old one, set to new sequence header.
    virtual void UpdatePreviousVsh();
    virtual void UpdatePreviousAsh();
public:
    // Update the cached metadata by packet.
    virtual error UpdateData(MessageHeader* header, OnMetaDataPacket* metadata, bool& updated);
    // Update the cached audio sequence header.
    virtual error UpdateAsh(SharedPtrMessage* msg);
    // Update the cached video sequence header.
    virtual error UpdateVsh(SharedPtrMessage* msg);
};

// The source manager to create and refresh all stream sources.
class LiveSourceManager : public IHourGlass
{
private:
    mutex_t m_lock;
    std::map<std::string, LiveSource*> pool;
    HourGlass* m_timer;
public:
    LiveSourceManager();
    virtual ~LiveSourceManager();
public:
    virtual error Initialize();
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param h the event handler for source.
    // @param pps the matched source, if success never be NULL.
    virtual error FetchOrCreate(Request* r, ILiveSourceHandler* h, LiveSource** pps);
public:
    // Get the exists source, NULL when not exists.
    virtual LiveSource* Fetch(Request* r);
public:
    // dispose and cycle all sources.
    virtual void Dispose();
// interface ISrsHourGlass
private:
    virtual error SetupTicks();
    virtual error Notify(int event, utime_t interval, utime_t tick);
public:
    // when system exit, destroy th`e sources,
    // For gmc to analysis mem leaks.
    virtual void Destroy();
};

// Global singleton instance.
extern LiveSourceManager* sources;

// For RTMP2RTC, bridge SrsLiveSource to SrsRtcSource
class ILiveSourceBridge
{
public:
    ILiveSourceBridge();
    virtual ~ILiveSourceBridge();
public:
    virtual error OnPublish() = 0;
    virtual error OnAudio(SharedPtrMessage* audio) = 0;
    virtual error OnVideo(SharedPtrMessage* video) = 0;
    virtual void OnUnpublish() = 0;
};

// The live streaming source.
class LiveSource : public IReloadHandler
{
    friend class OriginHub;
private:
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_id_changed() to let all clients know.
    ContextId m_sourceId;
    // previous source id.
    ContextId m_preSourceId;
    // deep copy of client request.
    Request* m_req;
    // To delivery stream to clients.
    std::vector<LiveConsumer*> m_consumers;
    // The time jitter algorithm for vhost.
    RtmpJitterAlgorithm m_jitterAlgorithm;
    // For play, whether use interlaced/mixed algorithm to correct timestamp.
    bool m_mixCorrect;
    // The mix queue to implements the mix correct algorithm.
    MixQueue* m_mixQueue;
    // For play, whether enabled atc.
    // The atc(use absolute time and donot adjust time),
    // directly use msg time and donot adjust if atc is true,
    // otherwise, adjust msg time to start from 0 to make flash happy.
    bool m_atc;
    // whether stream is monotonically increase.
    bool m_isMonotonicallyIncrease;
    // The time of the packet we just got.
    int64_t m_lastPacketTime;
    // The event handler.
    ILiveSourceHandler* m_handler;
    // The source bridge for other source.
    ILiveSourceBridge* m_bridge;
    // The edge control service
    PlayEdge* m_playEdge;
    PublishEdge* m_publishEdge;
    // The gop cache for client fast startup.
    GopCache* m_gopCache;
    // The hub for origin server.
    OriginHub* m_hub;
    // The metadata cache.
    MetaCache* m_meta;
private:
    // Whether source is avaiable for publishing.
    bool m_canPublish;
    // The last die time, when all consumers quit and no publisher,
    // We will remove the source when source die.
    utime_t m_dieAt;
public:
    LiveSource();
    virtual ~LiveSource();
public:
    virtual void Dispose();
    virtual error Cycle();
    // Remove source when expired.
    virtual bool Expired();
public:
    // Initialize the hls with handlers.
    virtual error Initialize(Request* r, ILiveSourceHandler* h);
    // Bridge to other source, forward packets to it.
    void SetBridge(ILiveSourceBridge* v);
// Interface ISrsReloadHandler
public:
    virtual error OnReloadVhostPlay(std::string vhost);
public:
    // The source id changed.
    virtual error OnSourceIdChanged(ContextId id);
    // Get current source id.
    virtual ContextId SourceId();
    virtual ContextId PreSourceId();
    // Whether source is inactive, which means there is no publishing stream source.
    // @remark For edge, it's inactive util stream has been pulled from origin.
    virtual bool Inactive();
    // Update the authentication information in request.
    virtual void UpdateAuth(Request* r);
public:
    virtual bool CanPublish(bool is_edge);
    virtual error OnMetaData(CommonMessage* msg, OnMetaDataPacket* metadata);
public:
    // TODO: FIXME: Use SrsSharedPtrMessage instead.
    virtual error OnAudio(CommonMessage* audio);
private:
    virtual error OnAudioImp(SharedPtrMessage* msg);
public:
    // TODO: FIXME: Use SrsSharedPtrMessage instead.
    virtual error OnVideo(CommonMessage* video);
private:
    virtual error OnVideoImp(SharedPtrMessage* msg);
public:
    virtual error OnAggregate(CommonMessage* msg);
    // Publish stream event notify.
    // @param _req the request from client, the source will deep copy it,
    //         for when reload the request of client maybe invalid.
    virtual error OnPublish();
    virtual void OnUnpublish();
public:
    // Create consumer
    // @param consumer, output the create consumer.
    virtual error CreateConsumer(LiveConsumer*& consumer);
    // Dumps packets in cache to consumer.
    // @param ds, whether dumps the sequence header.
    // @param dm, whether dumps the metadata.
    // @param dg, whether dumps the gop cache.
    virtual error ConsumerDumps(LiveConsumer* consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void OnConsumerDestroy(LiveConsumer* consumer);
    virtual void SetCache(bool enabled);
    virtual RtmpJitterAlgorithm Jitter();
public:
    // For edge, when publish edge stream, check the state
    virtual error OnEdgeStartPublish();
    // For edge, proxy the publish
    virtual error OnEdgeProxyPublish(CommonMessage* msg);
    // For edge, proxy stop publish
    virtual void OnEdgeProxyUnpublish();
public:
    virtual std::string GetCurrOrigin();
};

#endif // APP_SOURCE_H

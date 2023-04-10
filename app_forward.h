#ifndef APP_FORWARD_H
#define APP_FORWARD_H


#include "app_st.h"
#include "core_time.h"
#include "log.h"

class IProtocolReadWriter;
class SharedPtrMessage;
class OnMetaDataPacket;
class MessageQueue;
class RtmpJitter;
class RtmpClient;
class Request;
class LiveSource;
class OriginHub;
class Kbps;
class SimpleRtmpClient;

// Forward the stream to other servers.
class Forwarder : public ICoroutineHandler
{
private:
    // The ep to forward, server[:port].
    std::string m_epForward;
    Request* m_req;
private:
    Coroutine* m_trd;
private:
    OriginHub* m_hub;
    SimpleRtmpClient* m_sdk;
    RtmpJitter* m_jitter;
    MessageQueue* m_queue;
    // Cache the sequence header for retry when slave is failed.
    SharedPtrMessage* m_shAudio;
    SharedPtrMessage* m_shVideo;
public:
    Forwarder(OriginHub* h);
    virtual ~Forwarder();
public:
    virtual error Initialize(Request* r, std::string ep);
    virtual void SetQueueSize(utime_t queue_size);
public:
    virtual error OnPublish();
    virtual void OnUnpublish();
    // Forward the audio packet.
    // @param shared_metadata, directly ptr, copy it if need to save it.
    virtual error OnMetaData(SharedPtrMessage* shared_metadata);
    // Forward the audio packet.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual error OnAudio(SharedPtrMessage* shared_audio);
    // Forward the video packet.
    // @param shared_video, directly ptr, copy it if need to save it.
    virtual error OnVideo(SharedPtrMessage* shared_video);
// Interface IReusableThread2Handler.
public:
    virtual error Cycle();
private:
    virtual error DoCycle();
private:
    virtual error Forward();
};

#endif // APP_FORWARD_H

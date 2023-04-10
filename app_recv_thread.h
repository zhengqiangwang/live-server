#ifndef APP_RECV_THREAD_H
#define APP_RECV_THREAD_H


#include "app_st.h"
#include "core.h"
#include "core_time.h"
#include "log.h"
#include "protocol_st.h"
#include "protocol_stream.h"
#include "app_reload.h"
#include "core_performance.h"
#include <vector>

class RtmpServer;
class CommonMessage;
class RtmpConn;
class LiveSource;
class Request;
class LiveConsumer;
class HttpConn;
class HttpxConn;

// The message consumer which consume a message.
class IMessageConsumer
{
public:
    IMessageConsumer();
    virtual ~IMessageConsumer();
public:
    // Consume the received message.
    // @remark user must free this message.
    virtual error Consume(CommonMessage* msg) = 0;
};

// The message pumper to pump messages to processer.
class IMessagePumper : public IMessageConsumer
{
public:
    IMessagePumper();
    virtual ~IMessagePumper();
public:
    // Whether the pumper is interrupted.
    // For example, when pumpter is busy, it's interrupted,
    // please wait for a while then try to feed the pumper.
    virtual bool Interrupted() = 0;
    // Interrupt the pumper for a error.
    virtual void Interrupt(error error) = 0;
    // When start the pumper.
    virtual void OnStart() = 0;
    // When stop the pumper.
    virtual void OnStop() = 0;
};

// The recv thread, use message handler to handle each received message.
class RecvThread : public ICoroutineHandler
{
protected:
    Coroutine* m_trd;
    IMessagePumper* m_pumper;
    RtmpServer* m_rtmp;
    ContextId m_parentCid;
    // The recv timeout in utime_t.
    utime_t m_timeout;
public:
    // Constructor.
    // @param tm The receive timeout in utime_t.
    RecvThread(IMessagePumper* p, RtmpServer* r, utime_t tm, ContextId parent_cid);
    virtual ~RecvThread();
public:
    virtual ContextId Cid();
public:
    virtual error Start();
    virtual void Stop();
    virtual void StopLoop();
// Interface ISrsReusableThread2Handler
public:
    virtual error Cycle();
private:
    virtual error DoCycle();
};

// The recv thread used to replace the timeout recv,
// which hurt performance for the epoll_ctrl is frequently used.
// @see: SrsRtmpConn::playing
// @see: https://github.com/ossrs/srs/issues/217
class QueueRecvThread : public IMessagePumper
{
private:
    std::vector<CommonMessage*> m_queue;
    RecvThread m_trd;
    RtmpServer* m_rtmp;
    // The recv thread error code.
    error m_recvError;
    LiveConsumer* m_consumer;
public:
    // TODO: FIXME: Refine timeout in time unit.
    QueueRecvThread(LiveConsumer* consumer, RtmpServer* rtmp_sdk, utime_t tm, ContextId parent_cid);
    virtual ~QueueRecvThread();
public:
    virtual error Start();
    virtual void Stop();
public:
    virtual bool Empty();
    virtual int Size();
    virtual CommonMessage* Pump();
    virtual error ErrorCode();
// Interface ISrsMessagePumper
public:
    virtual error Consume(CommonMessage* msg);
    virtual bool Interrupted();
    virtual void Interrupt(error err);
    virtual void OnStart();
    virtual void OnStop();
};

// The publish recv thread got message and callback the source method to process message.
// @see: https://github.com/ossrs/srs/issues/237
class PublishRecvThread : public IMessagePumper, public IReloadHandler
#ifdef PERF_MERGED_READ
    , public IMergeReadHandler
#endif
{
private:
    uint32_t m_nnMsgsForYield;
    RecvThread m_trd;
    RtmpServer* m_rtmp;
    Request* m_req;
    // The msgs already got.
    int64_t m_nbMsgs;
    // The video frames we got.
    uint64_t m_videoFrames;
    // For mr(merged read),
    // @see https://github.com/ossrs/srs/issues/241
    bool m_mr;
    int m_mrRd;
    utime_t m_mrSleep;
    // For realtime
    // @see https://github.com/ossrs/srs/issues/257
    bool m_realtime;
    // The recv thread error code.
    error m_recvError;
    RtmpConn* m_conn;
    // The parmams for conn callback.
    LiveSource* m_source;
    // The error timeout cond
    // @see https://github.com/ossrs/srs/issues/244
    cond_t m_error;
    // The merged context id.
    ContextId m_cid;
    ContextId m_ncid;
public:
    PublishRecvThread(RtmpServer* rtmp_sdk, Request* _req,
        int mr_sock_fd, utime_t tm, RtmpConn* conn, LiveSource* source, ContextId parent_cid);
    virtual ~PublishRecvThread();
public:
    // Wait for error for some timeout.
    virtual error Wait(utime_t tm);
    virtual int64_t NbMsgs();
    virtual uint64_t NbVideoFrames();
    virtual error ErrorCode();
    virtual void SetCid(ContextId v);
    virtual ContextId GetCid();
public:
    virtual error Start();
    virtual void Stop();
// Interface ISrsMessagePumper
public:
    virtual error Consume(CommonMessage* msg);
    virtual bool Interrupted();
    virtual void Interrupt(error err);
    virtual void OnStart();
    virtual void OnStop();
// Interface IMergeReadHandler
public:
#ifdef PERF_MERGED_READ
    virtual void OnRead(ssize_t nread);
#endif
// Interface ISrsReloadHandler
public:
    virtual error OnReloadVhostPublish(std::string vhost);
    virtual error OnReloadVhostRealtime(std::string vhost);
private:
    virtual void SetSocketBuffer(utime_t sleep_v);
};

// The HTTP receive thread, try to read messages util EOF.
// For example, the HTTP FLV serving thread will use the receive thread to break
// when client closed the request, to avoid FD leak.
// @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
class HttpRecvThread : public ICoroutineHandler
{
private:
    HttpxConn* m_conn;
    Coroutine* m_trd;
public:
    HttpRecvThread(HttpxConn* c);
    virtual ~HttpRecvThread();
public:
    virtual error Start();
public:
    virtual error Pull();
// Interface ISrsOneCycleThreadHandler
public:
    virtual error Cycle();
};

#endif // APP_RECV_THREAD_H

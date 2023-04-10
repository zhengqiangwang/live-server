#include "app_recv_thread.h"
#include "app_config.h"
#include "app_rtmp_conn.h"
#include "app_source.h"
#include "protocol_rtmp_stack.h"
#include "utility.h"
#include "core_autofree.h"
#include "app_http_conn.h"

#include <inttypes.h>


// the max small bytes to group
#define MR_SMALL_BYTES 4096

IMessageConsumer::IMessageConsumer()
{

}

IMessageConsumer::~IMessageConsumer()
{

}

IMessagePumper::IMessagePumper()
{

}

IMessagePumper::~IMessagePumper()
{

}

RecvThread::RecvThread(IMessagePumper *p, RtmpServer *r, utime_t tm, ContextId parent_cid)
{
    m_rtmp = r;
    m_pumper = p;
    m_timeout = tm;
    m_parentCid = parent_cid;
    m_trd = new DummyCoroutine();
}

RecvThread::~RecvThread()
{
    Freep(m_trd);
}

ContextId RecvThread::Cid()
{
    return m_trd->Cid();
}

error RecvThread::Start()
{
    error err = SUCCESS;

    Freep(m_trd);
    m_trd = new STCoroutine("recv", this, m_parentCid);

    //change stack size to 256K, fix crash when call some 3rd-part api.
    ((STCoroutine*)m_trd)->SetStackSize(1 << 18);

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "recv thread");
    }

    return err;
}

void RecvThread::Stop()
{
    m_trd->Stop();
}

void RecvThread::StopLoop()
{
    m_trd->Interrupt();
}

error RecvThread::Cycle()
{
    error err = SUCCESS;

    // the multiple messages writev improve performance large,
    // but the timeout recv will cause 33% sys call performance,
    // to use isolate thread to recv, can improve about 33% performance.
    m_rtmp->SetRecvTimeout(UTIME_NO_TIMEOUT);

    m_pumper->OnStart();

    if ((err = DoCycle()) != SUCCESS) {
        err = ERRORWRAP(err, "recv thread");
    }

    // reset the timeout to pulse mode.
    m_rtmp->SetRecvTimeout(m_timeout);

    m_pumper->OnStop();

    return err;
}

error RecvThread::DoCycle()
{
    error err = SUCCESS;

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "recv thread");
        }

        // When the pumper is interrupted, wait then retry.
        if (m_pumper->Interrupted()) {
            Usleep(m_timeout);
            continue;
        }

        CommonMessage* msg = NULL;

        // Process the received message.
        if ((err = m_rtmp->RecvMessage(&msg)) == SUCCESS) {
            err = m_pumper->Consume(msg);
        }

        if (err != SUCCESS) {
            // Interrupt the receive thread for any error.
            m_trd->Interrupt();

            // Notify the pumper to quit for error.
            m_pumper->Interrupt(err);

            return ERRORWRAP(err, "recv thread");
        }
    }

    return err;
}

QueueRecvThread::QueueRecvThread(LiveConsumer *consumer, RtmpServer *rtmp_sdk, utime_t tm, ContextId parent_cid) : m_trd(this, rtmp_sdk, tm, parent_cid)
{
    m_consumer = consumer;
    m_rtmp = rtmp_sdk;
    m_recvError = SUCCESS;
}

QueueRecvThread::~QueueRecvThread()
{
    Stop();

    // clear all messages.
    std::vector<CommonMessage*>::iterator it;
    for (it = m_queue.begin(); it != m_queue.end(); ++it) {
        CommonMessage* msg = *it;
        Freep(msg);
    }
    m_queue.clear();

    Freep(m_recvError);
}

error QueueRecvThread::Start()
{
    error err = SUCCESS;

    if ((err = m_trd.Start()) != SUCCESS) {
        return ERRORWRAP(err, "queue recv thread");
    }

    return err;
}

void QueueRecvThread::Stop()
{
    m_trd.Stop();
}

bool QueueRecvThread::Empty()
{
    return m_queue.empty();
}

int QueueRecvThread::Size()
{
    return (int)m_queue.size();
}

CommonMessage *QueueRecvThread::Pump()
{
    Assert(!m_queue.empty());

    CommonMessage* msg = *m_queue.begin();

    m_queue.erase(m_queue.begin());

    return msg;
}

error QueueRecvThread::ErrorCode()
{
    return ERRORCOPY(m_recvError);
}

error QueueRecvThread::Consume(CommonMessage *msg)
{
    // put into queue, the send thread will get and process it,
    // @see SrsRtmpConn::process_play_control_msg
    m_queue.push_back(msg);
#ifdef PERF_QUEUE_COND_WAIT
    if (m_consumer) {
        m_consumer->Wakeup();
    }
#endif
    return SUCCESS;
}

bool QueueRecvThread::Interrupted()
{
    // we only recv one message and then process it,
    // for the message may cause the thread to stop,
    // when stop, the thread is freed, so the messages
    // are dropped.
    return !Empty();
}

void QueueRecvThread::Interrupt(error err)
{
    Freep(m_recvError);
    m_recvError = ERRORCOPY(err);

#ifdef PERF_QUEUE_COND_WAIT
    if (m_consumer) {
        m_consumer->Wakeup();
    }
#endif
}

void QueueRecvThread::OnStart()
{
    // disable the protocol auto response,
    // for the isolate recv thread should never send any messages.
    m_rtmp->SetAutoResponse(false);
}

void QueueRecvThread::OnStop()
{
    // enable the protocol auto response,
    // for the isolate recv thread terminated.
    m_rtmp->SetAutoResponse(true);
}

PublishRecvThread::PublishRecvThread(RtmpServer *rtmp_sdk, Request *_req, int mr_sock_fd, utime_t tm, RtmpConn *conn, LiveSource *source, ContextId parent_cid) : m_trd(this, rtmp_sdk, tm, parent_cid)
{
    m_rtmp = rtmp_sdk;

    m_conn = conn;
    m_source = source;

    m_nnMsgsForYield = 0;
    m_recvError = SUCCESS;
    m_nbMsgs = 0;
    m_videoFrames = 0;
    m_error = CondNew();

    m_req = _req;
    m_mrRd = mr_sock_fd;

    // the mr settings,
    m_mr = config->GetMrEnabled(m_req->m_vhost);
    m_mrSleep = config->GetMrSleep(m_req->m_vhost);

    m_realtime = config->GetRealtimeEnabled(m_req->m_vhost);

    config->Subscribe(this);
}

PublishRecvThread::~PublishRecvThread()
{
    config->Unsubscribe(this);

    m_trd.Stop();
    CondDestroy(m_error);
    Freep(m_recvError);
}

error PublishRecvThread::Wait(utime_t tm)
{
    if (m_recvError != SUCCESS) {
        return ERRORCOPY(m_recvError);
    }

    // ignore any return of cond wait.
    CondTimedwait(m_error, tm);

    return SUCCESS;
}

int64_t PublishRecvThread::NbMsgs()
{
    return m_nbMsgs;
}

uint64_t PublishRecvThread::NbVideoFrames()
{
    return m_videoFrames;
}

error PublishRecvThread::ErrorCode()
{
    return ERRORCOPY(m_recvError);
}

void PublishRecvThread::SetCid(ContextId v)
{
    m_ncid = v;
}

ContextId PublishRecvThread::GetCid()
{
    return m_ncid;
}

error PublishRecvThread::Start()
{
    error err = SUCCESS;

    if ((err = m_trd.Start()) != SUCCESS) {
        err = ERRORWRAP(err, "publish recv thread");
    }

    m_ncid = m_cid = m_trd.Cid();

    return err;
}

void PublishRecvThread::Stop()
{
    m_trd.Stop();
}

error PublishRecvThread::Consume(CommonMessage *msg)
{
    error err = SUCCESS;

    // when cid changed, change it.
    if (m_ncid.Compare(m_cid)) {
        Context->SetId(m_ncid);
        m_cid = m_ncid;
    }

    m_nbMsgs++;

    if (msg->m_header.IsVideo()) {
        m_videoFrames++;
    }

    // log to show the time of recv thread.
    verbose("recv thread now=%" PRId64 "us, got msg time=%" PRId64 "ms, size=%d",
                UpdateSystemTime(), msg->m_header.m_timestamp, msg->m_size);

    // the rtmp connection will handle this message
    err = m_conn->HandlePublishMessage(m_source, msg);

    // must always free it,
    // the source will copy it if need to use.
    Freep(msg);

    if (err != SUCCESS) {
        return ERRORWRAP(err, "handle publish message");
    }

    // Yield to another coroutines.
    // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777463768
    if (++m_nnMsgsForYield >= 15) {
        m_nnMsgsForYield = 0;
        ThreadYield();
    }

    return err;
}

bool PublishRecvThread::Interrupted()
{
    // Never interrupted, always can handle message.
    return false;
}

void PublishRecvThread::Interrupt(error err)
{
    Freep(m_recvError);
    m_recvError = ERRORCOPY(err);

    // when recv thread error, signal the conn thread to process it.
    CondSignal(m_error);
}

void PublishRecvThread::OnStart()
{
    // we donot set the auto response to false,
    // for the main thread never send message.

#ifdef PERF_MERGED_READ
    if (m_mr) {
        // set underlayer buffer size
        SetSocketBuffer(m_mrSleep);

        // disable the merge read
        m_rtmp->SetMergeRead(true, this);
    }
#endif
}

void PublishRecvThread::OnStop()
{
    // we donot set the auto response to true,
    // for we donot set to false yet.

    // when thread stop, signal the conn thread which wait.
    CondSignal(m_error);

#ifdef PERF_MERGED_READ
    if (m_mr) {
        // disable the merge read
        m_rtmp->SetMergeRead(false, NULL);
    }
#endif
}

#ifdef PERF_MERGED_READ
void PublishRecvThread::OnRead(ssize_t nread)
{
    if (!m_mr || m_realtime) {
        return;
    }

    if (nread < 0 || m_mrSleep <= 0) {
        return;
    }

    /**
     * to improve read performance, merge some packets then read,
     * when it on and read small bytes, we sleep to wait more data.,
     * that is, we merge some data to read together.
     */
    if (nread < MR_SMALL_BYTES) {
        Usleep(m_mrSleep);
    }
}
#endif

error PublishRecvThread::OnReloadVhostPublish(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // the mr settings,
    bool mr_enabled = config->GetMrEnabled(m_req->m_vhost);
    utime_t sleep_v = config->GetMrSleep(m_req->m_vhost);

    // update buffer when sleep ms changed.
    if (m_mrSleep != sleep_v) {
        SetSocketBuffer(sleep_v);
    }

#ifdef PERF_MERGED_READ
    // mr enabled=>disabled
    if (m_mr && !mr_enabled) {
        // disable the merge read
        m_rtmp->SetMergeRead(false, NULL);
    }
    // mr disabled=>enabled
    if (!m_mr && mr_enabled) {
        // enable the merge read
        m_rtmp->SetMergeRead(true, this);
    }
#endif

    // update to new state
    m_mr = mr_enabled;
    m_mrSleep = sleep_v;

    return err;
}

error PublishRecvThread::OnReloadVhostRealtime(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    bool realtime_enabled = config->GetRealtimeEnabled(m_req->m_vhost);
    trace("realtime changed %d=>%d", m_realtime, realtime_enabled);
    m_realtime = realtime_enabled;

    return err;
}

void PublishRecvThread::SetSocketBuffer(utime_t sleep_v)
{
    // the bytes:
    //      4KB=4096, 8KB=8192, 16KB=16384, 32KB=32768, 64KB=65536,
    //      128KB=131072, 256KB=262144, 512KB=524288
    // the buffer should set to sleep*kbps/8,
    // for example, your system delivery stream in 1000kbps,
    // sleep 800ms for small bytes, the buffer should set to:
    //      800*1000/8=100000B(about 128KB).
    // other examples:
    //      2000*3000/8=750000B(about 732KB).
    //      2000*5000/8=1250000B(about 1220KB).
    int kbps = 5000;
    int socket_buffer_size = u2msi(sleep_v) * kbps / 8;

    int fd = m_mrRd;
    int onb_rbuf = 0;
    socklen_t sock_buf_size = sizeof(int);
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &onb_rbuf, &sock_buf_size);

    // socket recv buffer, system will double it.
    int nb_rbuf = socket_buffer_size / 2;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, sock_buf_size) < 0) {
        warn("set sock SO_RCVBUF=%d failed.", nb_rbuf);
    }
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, &sock_buf_size);

    trace("mr change sleep %d=>%d, erbuf=%d, rbuf %d=>%d, sbytes=%d, realtime=%d",
              u2msi(m_mrSleep), u2msi(sleep_v), socket_buffer_size, onb_rbuf, nb_rbuf,
              MR_SMALL_BYTES, m_realtime);

    m_rtmp->SetRecvBuffer(nb_rbuf);
}

HttpRecvThread::HttpRecvThread(HttpxConn *c)
{
    m_conn = c;
    m_trd = new STCoroutine("http-receive", this, Context->GetId());
}

HttpRecvThread::~HttpRecvThread()
{
    Freep(m_trd);
}

error HttpRecvThread::Start()
{
    error err = SUCCESS;

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "http recv thread");
    }

    return err;
}

error HttpRecvThread::Pull()
{
    return m_trd->Pull();
}

error HttpRecvThread::Cycle()
{
    error err = SUCCESS;

    while ((err = m_trd->Pull()) == SUCCESS) {
        IHttpMessage* req = NULL;
        AutoFree(IHttpMessage, req);

        if ((err = m_conn->PopMessage(&req)) != SUCCESS) {
            return ERRORWRAP(err, "pop message");
        }
    }

    return err;
}

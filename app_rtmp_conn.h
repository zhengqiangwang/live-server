#ifndef APP_RTMP_CONN_H
#define APP_RTMP_CONN_H

#include "app_reload.h"
#include "app_conn.h"
#include "app_st.h"
#include "protocol_conn.h"
#include "protocol_kbps.h"
#include "protocol_rtmp_conn.h"
#include "core_time.h"
#include "log.h"
#include "protocol_rtmp_stack.h"
#include "protocol_st.h"
#include <string>

class Server;
class RtmpServer;
class Request;
class Response;
class LiveSource;
class Refer;
class LiveConsumer;
class CommonMessage;
class StSocket;
class HttpHooks;
class Bandwidth;
class Kbps;
class RtmpClient;
class SharedPtrMessage;
class QueueRecvThread;
class PublishRecvThread;
class Security;
class IWakable;
class CommonMessage;
class Packet;
class NetworkDelta;
class IApmSpan;

// The simple rtmp client for .
class SimpleRtmpClient : public BasicRtmpClient
{
public:
    SimpleRtmpClient(std::string u, utime_t ctm, utime_t stm);
    virtual ~SimpleRtmpClient();
protected:
    virtual error ConnectApp();
};

// Some information of client.
class ClientInfo
{
public:
    // The type of client, play or publish.
    RtmpConnType m_type;
    // Whether the client connected at the edge server.
    bool m_edge;
    // Original request object from client.
    Request* m_req;
    // Response object to client.
    Response* m_res;
public:
    ClientInfo();
    virtual ~ClientInfo();
};

// The client provides the main logic control for RTMP clients.
class RtmpConn : public IConnection, public IStartable, public IReloadHandler
    , public ICoroutineHandler, public IExpire
{
    // For the thread to directly access any field of connection.
    friend class PublishRecvThread;
private:
    Server* m_server;
    RtmpServer* m_rtmp;
    Refer* m_refer;
    Bandwidth* m_bandwidth;
    Security* m_security;
    // The wakable handler, maybe NULL.
    // TODO: FIXME: Should refine the state for receiving thread.
    IWakable* m_wakable;
    // The elapsed duration in utime_t
    // For live play duration, for instance, rtmpdump to record.
    utime_t m_duration;
    // The MR(merged-write) sleep time in utime_t.
    utime_t m_mwSleep;
    int m_mwMsgs;
    // For realtime
    // @see https://github.com/ossrs/srs/issues/257
    bool m_realtime;
    // The minimal interval in utime_t for delivery stream.
    utime_t m_sendMinInterval;
    // The publish 1st packet timeout in utime_t
    utime_t m_publish1stpktTimeout;
    // The publish normal packet timeout in utime_t
    utime_t m_publishNormalTimeout;
    // Whether enable the tcp_nodelay.
    bool m_tcpNodelay;
    // About the rtmp client.
    ClientInfo* m_info;
private:
    netfd_t m_stfd;
    TcpConnection* m_skt;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    Coroutine* m_trd;
    // The manager object to manage the connection.
    IResourceManager* m_manager;
    // The ip and port of client.
    std::string m_ip;
    int m_port;
    // The delta for statistic.
    NetworkDelta* m_delta;
    NetworkKbps* m_kbps;
    // The create time in milliseconds.
    // for current connection to log self create time and calculate the living time.
    int64_t m_createTime;
    // The span for tracing connection establishment.
    IApmSpan* m_spanMain;
    IApmSpan* m_spanConnect;
    IApmSpan* m_spanClient;
public:
    RtmpConn(Server* svr, netfd_t c, std::string cip, int port);
    virtual ~RtmpConn();
// Interface ISrsResource.
public:
    virtual std::string Desc();
protected:
    virtual error DoCycle();
// Interface ISrsReloadHandler
public:
    virtual error OnReloadVhostRemoved(std::string vhost);
    virtual error OnReloadVhostPlay(std::string vhost);
    virtual error OnreloadVhostTcpNodelay(std::string vhost);
    virtual error OnReloadVhostRealtime(std::string vhost);
    virtual error OnReloadVhostPublish(std::string vhost);
public:
    virtual IKbpsDelta* Delta();
private:
    // When valid and connected to vhost/app, service the client.
    virtual error ServiceCycle();
    // The stream(play/publish) service cycle, identify client first.
    virtual error StreamServiceCycle();
    virtual error CheckVhost(bool try_default_vhost);
    virtual error Playing(LiveSource* source);
    virtual error DoPlaying(LiveSource* source, LiveConsumer* consumer, QueueRecvThread* trd);
    virtual error Publishing(LiveSource* source);
    virtual error DoPublishing(LiveSource* source, PublishRecvThread* trd);
    virtual error AcquirePublish(LiveSource* source);
    virtual void ReleasePublish(LiveSource* source);
    virtual error HandlePublishMessage(LiveSource* source, CommonMessage* msg);
    virtual error ProcessPublishMessage(LiveSource* source, CommonMessage* msg);
    virtual error ProcessPlayControlMsg(LiveConsumer* consumer, CommonMessage* msg);
    virtual void SetSockOptions();
private:
    virtual error CheckEdgeTokenTraverseAuth();
    virtual error DoTokenTraverseAuth(RtmpClient* client);
private:
    // When the connection disconnect, call this method.
    // e.g. log msg of connection and report to other system.
    virtual error OnDisconnect();
private:
    virtual error HttpHooksOnConnect();
    virtual void HttpHooksOnClose();
    virtual error HttpHooksOnPublish();
    virtual void HttpHooksOnUnpublish();
    virtual error HttpHooksOnPlay();
    virtual void HttpHooksOnStop();
// Extract APIs from SrsTcpConnection.
// Interface ISrsStartable
public:
    // Start the client green thread.
    // when server get a client from listener,
    // 1. server will create an concrete connection(for instance, RTMP connection),
    // 2. then add connection to its connection manager,
    // 3. start the client thread by invoke this start()
    // when client cycle thread stop, invoke the on_thread_stop(), which will use server
    // To remove the client by server->remove(this).
    virtual error Start();
// Interface ISrsOneCycleThreadHandler
public:
    // The thread cycle function,
    // when serve connection completed, terminate the loop which will terminate the thread,
    // thread will invoke the on_thread_stop() when it terminated.
    virtual error Cycle();
// Interface ISrsConnection.
public:
    virtual std::string RemoteIp();
    virtual const ContextId& GetId();
// Interface ISrsExpire.
public:
    virtual void Expire();
};

#endif // APP_RTMP_CONN_H

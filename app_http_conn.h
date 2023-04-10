#ifndef APP_HTTP_CONN_H
#define APP_HTTP_CONN_H

#include "app_conn.h"
#include "app_reload.h"
#include "log.h"
#include "protocol_http_conn.h"
#include "protocol_kbps.h"


class Server;
class LiveSource;
class Request;
class LiveConsumer;
class StSocket;
class HttpParser;
class IHttpMessage;
class HttpHandler;
class MessageQueue;
class SharedPtrMessage;
class Request;
class FastStream;
class HttpUri;
class HttpMessage;
class HttpStreamServer;
class HttpStaticServer;
class NetworkDelta;

// The owner of HTTP connection.
class IHttpConnOwner
{
public:
    IHttpConnOwner();
    virtual ~IHttpConnOwner();
public:
    // When start the coroutine to process connection.
    virtual error OnStart() = 0;
    // Handle the HTTP message r, which may be parsed partially.
    // For the static service or api, discard any body.
    // For the stream caster, for instance, http flv streaming, may discard the flv header or not.
    virtual error OnHttpMessage(IHttpMessage* r, HttpResponseWriter* w) = 0;
    // When message is processed, we may need to do more things.
    virtual error OnMessageDone(IHttpMessage* r, HttpResponseWriter* w) = 0;
    // When connection is destroy, should use manager to dispose it.
    // The r0 is the original error, we will use the returned new error.
    virtual error OnConnDone(error r0) = 0;
};

// TODO: FIXME: Should rename to roundtrip or responder, not connection.
// The http connection which request the static or stream content.
class HttpConn : public IConnection, public IStartable, public ICoroutineHandler
    , public IExpire
{
protected:
    HttpParser* m_parser;
    IHttpServeMux* m_httpMux;
    HttpCorsMux* m_cors;
    IHttpConnOwner* m_handler;
protected:
    IProtocolReadWriter* m_skt;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    Coroutine* m_trd;
    // The ip and port of client.
    std::string m_ip;
    int m_port;
private:
    // The delta for statistic.
    NetworkDelta* m_delta;
    // The create time in milliseconds.
    // for current connection to log self create time and calculate the living time.
    int64_t m_createTime;
public:
    HttpConn(IHttpConnOwner* handler, IProtocolReadWriter* fd, IHttpServeMux* m, std::string cip, int port);
    virtual ~HttpConn();
// Interface IResource.
public:
    virtual std::string Desc();
public:
    IKbpsDelta* Delta();
// Interface IStartable
public:
    virtual error Start();
// Interface IOneCycleThreadHandler
public:
    virtual error Cycle();
private:
    virtual error DoCycle();
    virtual error ProcessRequests(Request** preq);
    virtual error ProcessRequest(IHttpResponseWriter* w, IHttpMessage* r, int rid);
    // When the connection disconnect, call this method.
    // e.g. log msg of connection and report to other system.
    // @param request: request which is converted by the last http message.
    virtual error OnDisconnect(Request* req);
public:
    // Get the HTTP message handler.
    virtual IHttpConnOwner* Handler();
    // Whether the connection coroutine is error or terminated.
    virtual error Pull();
    // Whether enable the CORS(cross-domain).
    virtual error SetCrossdomainEnabled(bool v);
    // Whether enable the JSONP.
    virtual error SetJsonp(bool v);
// Interface IConnection.
public:
    virtual std::string RemoteIp();
    virtual const ContextId& GetId();
// Interface IExpire.
public:
    virtual void Expire();
};

// Drop body of request, only process the response.
class HttpxConn : public IConnection, public IStartable, public IHttpConnOwner, public IReloadHandler
{
private:
    // The manager object to manage the connection.
    IResourceManager* m_manager;
    IProtocolReadWriter* m_io;
    SslConnection* m_ssl;
    HttpConn* m_conn;
    // We should never enable the stat, unless HTTP stream connection requires.
    bool m_enableStat;
public:
    HttpxConn(bool https, IResourceManager* cm, IProtocolReadWriter* io, IHttpServeMux* m, std::string cip, int port);
    virtual ~HttpxConn();
public:
    // Require statistic about HTTP connection, for HTTP streaming clients only.
    void SetEnableStat(bool v);
    // Directly read a HTTP request message.
    // It's exported for HTTP stream, such as HTTP FLV, only need to write to client when
    // serving it, but we need to start a thread to read message to detect whether FD is closed.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    // @remark Should only used in HTTP-FLV streaming connection.
    virtual error PopMessage(IHttpMessage** preq);
// Interface IHttpConnOwner.
public:
    virtual error OnStart();
    virtual error OnHttpMessage(IHttpMessage* r, HttpResponseWriter* w);
    virtual error OnMessageDone(IHttpMessage* r, HttpResponseWriter* w);
    virtual error OnConnDone(error r0);
// Interface IResource.
public:
    virtual std::string Desc();
// Interface IConnection.
public:
    virtual std::string RemoteIp();
    virtual const ContextId& GetId();
// Interface IStartable
public:
    virtual error Start();
public:
    IKbpsDelta* Delta();
};

// The http server, use http stream or static server to serve requests.
class HttpServer : public IHttpServeMux
{
private:
    Server* m_server;
    HttpStaticServer* m_httpStatic;
    HttpStreamServer* m_httpStream;
public:
    HttpServer(Server* svr);
    virtual ~HttpServer();
public:
    virtual error Initialize();
// Interface IHttpServeMux
public:
    virtual error Handle(std::string pattern, IHttpHandler* handler);
// Interface IHttpHandler
public:
    virtual error ServeHttp(IHttpResponseWriter* w, IHttpMessage* r);
public:
    virtual error HttpMount(LiveSource* s, Request* r);
    virtual void HttpUnmount(LiveSource* s, Request* r);
};


#endif // APP_HTTP_CONN_H

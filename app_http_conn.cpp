#include "app_http_conn.h"
#include "app_config.h"
#include "app_server.h"
#include "database.h"
#include "consts.h"
#include "protocol_utility.h"
#include "utility.h"
#include "core_autofree.h"
#include "app_statistic.h"
#include "file.h"

#include <cstring>
#include <inttypes.h>
#include <sstream>

IHttpConnOwner::IHttpConnOwner()
{

}

IHttpConnOwner::~IHttpConnOwner()
{

}

HttpConn::HttpConn(IHttpConnOwner *handler, IProtocolReadWriter *fd, IHttpServeMux *m, std::string cip, int cport)
{
    m_parser = new HttpParser();
    m_cors = new HttpCorsMux();
    m_httpMux = m;
    m_handler = handler;

    m_skt = fd;
    m_ip = cip;
    m_port = cport;
    m_createTime = u2ms(GetSystemTime());
    m_delta = new NetworkDelta();
    m_delta->SetIo(m_skt, m_skt);
    m_trd = new STCoroutine("http", this, Context->GetId());
}

HttpConn::~HttpConn()
{
    m_trd->Interrupt();
    Freep(m_trd);

    Freep(m_parser);
    Freep(m_cors);

    Freep(m_delta);
}

std::string HttpConn::Desc()
{
    return "HttpConn";
}

IKbpsDelta *HttpConn::Delta()
{
    return m_delta;
}

error HttpConn::Start()
{
    error err = SUCCESS;

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "coroutine");
    }

    return err;
}

error HttpConn::Cycle()
{
    error err = DoCycle();

    // Notify handler to handle it.
    // @remark The error may be transformed by handler.
    err = m_handler->OnConnDone(err);

    // success.
    if (err == SUCCESS) {
        trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (ERRORCODE(err) == ERROR_SUCCESS) {
        trace("client finished%s.", ERRORSUMMARY(err).c_str());
        Freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (IsClientGracefullyClose(err)) {
        warn("client disconnect peer. ret=%d", ERRORCODE(err));
    } else if (IsServerGracefullyClose(err)) {
        warn("server disconnect. ret=%d", ERRORCODE(err));
    } else {
        ERROR("serve error %s", ERRORDESC(err).c_str());
    }

    Freep(err);
    return SUCCESS;
}

error HttpConn::DoCycle()
{
    error err = SUCCESS;

    // set the recv timeout, for some clients never disconnect the connection.
    // @see https://github.com/ossrs/srs/issues/398
    m_skt->SetRecvTimeout(HTTP_RECV_TIMEOUT);

    // initialize parser
    if ((err = m_parser->Initialize(HTTP_REQUEST)) != SUCCESS) {
        return ERRORWRAP(err, "init parser for %s", m_ip.c_str());
    }

    // Notify the handler that we are starting to process the connection.
    if ((err = m_handler->OnStart()) != SUCCESS) {
        return ERRORWRAP(err, "start");
    }

    Request* last_req = NULL;
    AutoFree(Request, last_req);

    // process all http messages.
    err = ProcessRequests(&last_req);

    error r0 = SUCCESS;
    if ((r0 = OnDisconnect(last_req)) != SUCCESS) {
        err = ERRORWRAP(err, "on disconnect %s", ERRORDESC(r0).c_str());
        Freep(r0);
    }

    return err;
}

error HttpConn::ProcessRequests(Request **preq)
{
    error err = SUCCESS;

    for (int req_id = 0; ; req_id++) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "pull");
        }

        // get a http message
        IHttpMessage* req = NULL;
        m_skt->GetRecvBytes();
        if ((err = m_parser->ParseMessage(m_skt, &req)) != SUCCESS) {
            return ERRORWRAP(err, "parse message");
        }

        // if SUCCESS, always NOT-NULL.
        // always free it in this scope.
        Assert(req);
        AutoFree(IHttpMessage, req);

        // Attach owner connection to message.
        HttpMessage* hreq = (HttpMessage*)req;
        hreq->SetConnection(this);
        trace("request method %d, contentlength %d", req->Method(), req->ContentLength());

        // copy request to last request object.
        Freep(*preq);
        *preq = hreq->ToRequest(hreq->Host());

        // may should discard the body.
        HttpResponseWriter writer(m_skt);
        if ((err = m_handler->OnHttpMessage(req, &writer)) != SUCCESS) {
            return ERRORWRAP(err, "on http message");
        }

        std::string password = RandomStr(16);

        //clock time
        timeval tv;
        if(gettimeofday(&tv, nullptr) == -1){
            return err;
        }

        //to calendar time
        struct tm now;

        if(gmtime_r(&tv.tv_sec, &now) == nullptr){
            return err;
        }
        long tim = tv.tv_sec;
        trace("time: %ld", tim);
        char* buffer = new char[30];

        snprintf(buffer, 30, "%d-%02d-%02d %02d:%02d:%02d.%03d", 1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000));
        std::string time = buffer;


        if(hreq->Method() == CONSTS_HTTP_POST)
        {
            std::string content = "";
            hreq->BodyReadAll(content);
            trace("read content: %s", content.c_str());
            HttpHeader *header = hreq->Header();
            std::string cid = header->Get("Course_id");
            std::string uid = header->Get("User_id");
            std::string filename = header->Get("Filename");

            std::string filepath = "./file/" + cid + "/";
            CreateDirRecursivel(filepath);
            content = AesEncode(password,content);
            std::string Bencode = "";
            Base64Encode(content.c_str(), content.size(), Bencode);
            FileWriter* fwriter = new FileWriter();

            Database* instance = Database::Instance();
            filename = instance->UploadFile(cid, uid, filename, tim, password);

            if(filename != "server error" && filename != "don't select this course")
            {
                filename = filepath + filename + ".txt";
                fwriter->Open(filename);
                ssize_t nwrite = 0;
                fwriter->Write(Bencode.data(), Bencode.size(), &nwrite);

                writer.Write(nullptr, 0);
            }
        } else if(hreq->Method() == CONSTS_HTTP_GET) {
            HttpHeader *header = hreq->Header();
            std::string cid = header->Get("Course_id");
            std::string uid = header->Get("User_id");
            std::string filename = header->Get("Filename");
            std::string time = header->Get("Time");

            std::string filepath = "./file/" + cid + "/";
            FileReader* freader = new FileReader();

            Database* instance = Database::Instance();
            std::vector<std::string> filemessage = instance->DownloadFile(cid, uid, filename, stol(time));

            if(filemessage.size() != 0)
            {
                filename = filepath + filemessage[1] + ".txt";
                password = filemessage[0];

                freader->Open(filename);
                ssize_t fsize = 0;
                fsize = freader->Filesize();
                std::string content = "";
                content.resize(fsize);
                ssize_t nread = 0;
                freader->Read(content.data(), content.size(), &nread);
                std::string Bdecode;
                Base64Decode(content.c_str(), content.size(), Bdecode);
                content = AesDecode(password, Bdecode);

                writer.Write(content.data(), content.size());
            }

        }


        // ok, handle http request.
        //        if ((err = ProcessRequest(&writer, req, req_id)) != SUCCESS) {
        //            return ERRORWRAP(err, "process request=%d", req_id);
        //        }

        // After the request is processed.
        if ((err = m_handler->OnMessageDone(req, &writer)) != SUCCESS) {
            return ERRORWRAP(err, "on message done");
        }

        break;
        // donot keep alive, disconnect it.
        // @see https://github.com/ossrs/srs/issues/399
        if (!req->IsKeepAlive()) {
            break;
        }
    }

    return err;
}

error HttpConn::ProcessRequest(IHttpResponseWriter *w, IHttpMessage *r, int rid)
{
    error err = SUCCESS;

    trace("HTTP #%d %s:%d %s %s, content-length=%" PRId64 "", rid, m_ip.c_str(), m_port,
          r->MethodStr().c_str(), r->Url().c_str(), r->ContentLength());

    // use cors server mux to serve http request, which will proxy to http_remux.
    if ((err = m_cors->ServeHttp(w, r)) != SUCCESS) {
        return ERRORWRAP(err, "mux serve");
    }

    return err;
}

error HttpConn::OnDisconnect(Request *req)
{
    // TODO: FIXME: Implements it.
    return SUCCESS;
}

IHttpConnOwner *HttpConn::Handler()
{
    return m_handler;
}

error HttpConn::Pull()
{
    return m_trd->Pull();
}

error HttpConn::SetCrossdomainEnabled(bool v)
{
    error err = SUCCESS;

    // initialize the cors, which will proxy to mux.
    if ((err = m_cors->Initialize(m_httpMux, v)) != SUCCESS) {
        return ERRORWRAP(err, "init cors");
    }

    return err;
}

error HttpConn::SetJsonp(bool v)
{
    m_parser->SetJsonp(v);
    return SUCCESS;
}

std::string HttpConn::RemoteIp()
{
    return m_ip;
}

const ContextId &HttpConn::GetId()
{
    return m_trd->Cid();
}

void HttpConn::Expire()
{
    m_trd->Interrupt();
}

HttpxConn::HttpxConn(bool https, IResourceManager *cm, IProtocolReadWriter *io, IHttpServeMux *m, std::string cip, int port)
{
    // Create a identify for this client.
    Context->SetId(Context->GenerateId());

    m_io = io;
    m_manager = cm;
    m_enableStat = false;

    if (https) {
        m_ssl = new SslConnection(m_io);
        m_conn = new HttpConn(this, m_ssl, m, cip, port);
    } else {
        m_ssl = NULL;
        m_conn = new HttpConn(this, m_io, m, cip, port);
    }

    config->Subscribe(this);
}

HttpxConn::~HttpxConn()
{
    config->Unsubscribe(this);

    Freep(m_conn);
    Freep(m_ssl);
    Freep(m_io);
}

void HttpxConn::SetEnableStat(bool v)
{
    m_enableStat = v;
}

error HttpxConn::PopMessage(IHttpMessage **preq)
{
    error err = SUCCESS;

    IProtocolReadWriter* io = m_io;
    if (m_ssl) {
        io = m_ssl;
    }

    // Check user interrupt by interval.
    io->SetRecvTimeout(3 * UTIME_SECONDS);

    // We start a socket to read the stfd, which is writing by conn.
    // It's ok, because conn never read it after processing the HTTP request.
    // drop all request body.
    static char body[HTTP_READ_CACHE_BYTES];
    while (true) {
        if ((err = m_conn->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "timeout");
        }

        if ((err = io->Read(body, HTTP_READ_CACHE_BYTES, NULL)) != SUCCESS) {
            // Because we use timeout to check trd state, so we should ignore any timeout.
            if (ERRORCODE(err) == ERROR_SOCKET_TIMEOUT) {
                Freep(err);
                continue;
            }

            return ERRORWRAP(err, "read response");
        }
    }

    return err;
}

error HttpxConn::OnStart()
{
    error err = SUCCESS;

    // Enable JSONP for HTTP API.
    if ((err = m_conn->SetJsonp(true)) != SUCCESS) {
        return ERRORWRAP(err, "set jsonp");
    }

    // Do SSL handshake if HTTPS.
    if (m_ssl)  {
        utime_t starttime = UpdateSystemTime();
        std::string crt_file = config->GetHttpsStreamSslCert();
        std::string key_file = config->GetHttpsStreamSslKey();
        if ((err = m_ssl->Handshake(key_file, crt_file)) != SUCCESS) {
            return ERRORWRAP(err, "handshake");
        }

        int cost = u2msi(UpdateSystemTime() - starttime);
        trace("https: stream server done, use key %s and cert %s, cost=%dms",
              key_file.c_str(), crt_file.c_str(), cost);
    }

    return err;
}

error HttpxConn::OnHttpMessage(IHttpMessage *r, HttpResponseWriter *w)
{
    error err = SUCCESS;

    // After parsed the message, set the schema to https.
    if (m_ssl) {
        HttpMessage* hm = dynamic_cast<HttpMessage*>(r);
        hm->SetHttps(true);
    }

    // For each session, we use short-term HTTP connection.
    HttpHeader* hdr = w->Header();
    hdr->Set("Connection", "Close");
    hdr->Set("Content-Type", "text/plain");

    return err;
}

error HttpxConn::OnMessageDone(IHttpMessage *r, HttpResponseWriter *w)
{
    return SUCCESS;
}

error HttpxConn::OnConnDone(error r0)
{
    // Only stat the HTTP streaming clients, ignore all API clients.
    if (m_enableStat) {
        Statistic::Instance()->OnDisconnect(GetId().Cstr(), r0);
        Statistic::Instance()->KbpsAddDelta(GetId().Cstr(), m_conn->Delta());
    }

    // Because we use manager to manage this object,
    // not the http connection object, so we must remove it here.
    m_manager->Remove(this);

    // For HTTP-API timeout, we think it's done successfully,
    // because there may be no request or response for HTTP-API.
    if (ERRORCODE(r0) == ERROR_SOCKET_TIMEOUT) {
        Freep(r0);
        return SUCCESS;
    }

    return r0;
}

std::string HttpxConn::Desc()
{
    if (m_ssl) {
        return "HttpsConn";
    }
    return "HttpConn";
}

std::string HttpxConn::RemoteIp()
{
    return m_conn->RemoteIp();
}

const ContextId &HttpxConn::GetId()
{
    return m_conn->GetId();
}

error HttpxConn::Start()
{
    error err = SUCCESS;

    bool v = config->GetHttpStreamCrossdomain();
    if ((err = m_conn->SetCrossdomainEnabled(v)) != SUCCESS) {
        return ERRORWRAP(err, "set cors=%d", v);
    }

    return m_conn->Start();
}

IKbpsDelta *HttpxConn::Delta()
{
    return m_conn->Delta();
}

HttpServer::HttpServer(Server *svr)
{
    //    m_server = svr;
    //    m_httpStream = new HttpStreamServer(svr);
    //    m_httpStatic = new HttpStaticServer(svr);
}

HttpServer::~HttpServer()
{
    Freep(m_httpStream);
    Freep(m_httpStatic);
}

error HttpServer::Initialize()
{
    //    error err = SUCCESS;

    //    // for SRS go-sharp to detect the status of HTTP server of SRS HTTP FLV Cluster.
    //    if ((err = m_httpStatic->m_mux.handle("/api/v1/versions", new GoApiVersion())) != SUCCESS) {
    //        return ERRORWRAP(err, "handle versions");
    //    }

    //    if ((err = m_httpStream->initialize()) != SUCCESS) {
    //        return ERRORWRAP(err, "http stream");
    //    }

    //    if ((err = m_httpStatic->initialize()) != SUCCESS) {
    //        return ERRORWRAP(err, "http static");
    //    }

    //    return err;
}

error HttpServer::Handle(std::string pattern, IHttpHandler *handler)
{
    //    return m_httpStatic->mux.handle(pattern, handler);
}

error HttpServer::ServeHttp(IHttpResponseWriter *w, IHttpMessage *r)
{
    //    error err = SUCCESS;

    //    std::string path = r->Path();
    //    const char* p = path.data();

    //    // For /api/ or /console/, try static only.
    //    if (path.length() > 4 && p[0] == '/') {
    //        bool is_api = memcmp(p, "/api/", 5) == 0;
    //        bool is_console = path.length() > 8 && memcmp(p, "/console/", 9) == 0;
    //        if (is_api || is_console) {
    //            return m_httpStatic->mux.serve_http(w, r);
    //        }
    //    }

    //    // Try http stream first, then http static if not found.
    //    IHttpHandler* h = NULL;
    //    if ((err = m_httpStream->mux.find_handler(r, &h)) != SUCCESS) {
    //        return ERRORWRAP(err, "find handler");
    //    }
    //    if (!h->IsNotFound()) {
    //        return m_httpStream->mux.serve_http(w, r);
    //    }

    //    // Use http static as default server.
    //    return m_httpStatic->mux.serve_http(w, r);
}

error HttpServer::HttpMount(LiveSource *s, Request *r)
{
    //    return m_httpStream->http_mount(s, r);
}

void HttpServer::HttpUnmount(LiveSource *s, Request *r)
{
    //    m_httpStream->http_unmount(s, r);
}

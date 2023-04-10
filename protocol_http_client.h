#ifndef PROTOCOL_HTTP_CLIENT_H
#define PROTOCOL_HTTP_CLIENT_H

#include "core_time.h"
#include "io.h"
#include <cstddef>
#include <map>
#include <openssl/ossl_typ.h>
#include <string>

class HttpUri;
class HttpParser;
class IHttpMessage;
class StSocket;
class NetworkKbps;
class WallClock;
class TcpClient;

// The default timeout for http client.
#define HTTP_CLIENT_TIMEOUT (30 * UTIME_SECONDS)

// The SSL client over TCP transport.
class SslClient : public IReader, public IStreamWriter
{
private:
    TcpClient* m_transport;
private:
    SSL_CTX* m_sslCtx;
    SSL* m_ssl;
    BIO* m_bioIn;
    BIO* m_bioOut;
public:
    SslClient(TcpClient* tcp);
    virtual ~SslClient();
public:
    virtual error Handshake();
public:
    virtual error Read(void* buf, size_t size, ssize_t* nread);
    virtual error Write(void* buf, size_t size, ssize_t* nwrite);
};

// The client to GET/POST/PUT/DELETE over HTTP.
// @remark We will reuse the TCP transport until initialize or channel error,
//      such as send/recv failed.
// Usage:
//      SrsHttpClient hc;
//      hc.initialize("127.0.0.1", 80, 9000);
//      hc.post("/api/v1/version", "Hello world!", NULL);
class HttpClient
{
private:
    // The underlayer TCP transport, set to NULL when disconnect, or never not NULL when connected.
    // We will disconnect transport when initialize or channel error, such as send/recv error.
    TcpClient* m_transport;
    HttpParser* m_parser;
    std::map<std::string, std::string> m_headers;
    NetworkKbps* m_kbps;
private:
    // The timeout in utime_t.
    utime_t m_timeout;
    utime_t m_recvTimeout;
    // The schema, host name or ip.
    std::string m_schema;
    std::string m_host;
    int m_port;
private:
    SslClient* m_sslTransport;
public:
    HttpClient();
    virtual ~HttpClient();
public:
    // Initliaze the client, disconnect the transport, renew the HTTP parser.
    // @param schema Should be http or https.
    // @param tm The underlayer TCP transport timeout in utime_t.
    // @remark we will set default values in headers, which can be override by set_header.
    virtual error Initialize(std::string schema, std::string h, int p, utime_t tm = HTTP_CLIENT_TIMEOUT);
    // Set HTTP request header in header[k]=v.
    // @return the HTTP client itself.
    virtual HttpClient* SetHeader(std::string k, std::string v);
public:
    // Post data to the uri.
    // @param the path to request on.
    // @param req the data post to uri. empty string to ignore.
    // @param ppmsg output the http message to read the response.
    // @remark user must free the ppmsg if not NULL.
    virtual error Post(std::string path, std::string req, IHttpMessage** ppmsg);
    // Get data from the uri.
    // @param the path to request on.
    // @param req the data post to uri. empty string to ignore.
    // @param ppmsg output the http message to read the response.
    // @remark user must free the ppmsg if not NULL.
    virtual error Get(std::string path, std::string req, IHttpMessage** ppmsg);
public:
    virtual void SetRecvTimeout(utime_t tm);
public:
    virtual void KbpsSample(const char* label, utime_t age);
private:
    virtual void Disconnect();
    virtual error Connect();
    IStreamWriter* Writer();
    IReader* Reader();
};
#endif // PROTOCOL_HTTP_CLIENT_H

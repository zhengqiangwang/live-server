#include "protocol_http_client.h"
#include "error.h"
#include "protocol_http_conn.h"
#include "protocol_kbps.h"
#include "protocol_st.h"
#include "core_autofree.h"
#include "utility.h"
#include "consts.h"
#include "protocol_http_stack.h"
#include <openssl/ssl.h>
#include <inttypes.h>
#include <sstream>

// The return value of verify_callback controls the strategy of the further verification process. If verify_callback
// returns 0, the verification process is immediately stopped with "verification failed" state. If SSL_VERIFY_PEER is
// set, a verification failure alert is sent to the peer and the TLS/SSL handshake is terminated. If verify_callback
// returns 1, the verification process is continued. If verify_callback always returns 1, the TLS/SSL handshake will
// not be terminated with respect to verification failures and the connection will be established. The calling process
// can however retrieve the error code of the last verification error using SSL_get_verify_result(3) or by maintaining
// its own error storage managed by verify_callback.
// @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
int VerifyCallback(int preverify_ok, X509_STORE_CTX *ctx)
{
    // Always OK, we don't check the certificate of client,
    // because we allow client self-sign certificate.
    return 1;
}

SslClient::SslClient(TcpClient *tcp)
{
    m_transport = tcp;
    m_sslCtx = NULL;
    m_ssl = NULL;
}

SslClient::~SslClient()
{
    if (m_ssl) {
        // this function will free bio_in and bio_out
        SSL_free(m_ssl);
        m_ssl = NULL;
    }

    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = NULL;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
error SslClient::Handshake()
{
    error err = SUCCESS;

    // For HTTPS, try to connect over security transport.
#if (OPENSSL_VERSION_NUMBER < 0x10002000L) // v1.0.2
    ssl_ctx = SSL_CTX_new(TLS_method());
#else
    m_sslCtx = SSL_CTX_new(TLSv1_2_method());
#endif
    SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_PEER, VerifyCallback);
    Assert(SSL_CTX_set_cipher_list(m_sslCtx, "ALL") == 1);

    // TODO: Setup callback, see SSL_set_ex_data and SSL_set_info_callback
    if ((m_ssl = SSL_new(m_sslCtx)) == NULL) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "SSL_new ssl");
    }

    if ((m_bioIn = BIO_new(BIO_s_mem())) == NULL) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_new in");
    }

    if ((m_bioOut = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(m_bioIn);
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_new out");
    }

    SSL_set_bio(m_ssl, m_bioIn, m_bioOut);

    // SSL setup active, as client role.
    SSL_set_connect_state(m_ssl);
    SSL_set_mode(m_ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);

    // Send ClientHello.
    int r0 = SSL_do_handshake(m_ssl); int r1 = SSL_get_error(m_ssl, r0);
    if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
    }

    uint8_t* data = NULL;
    int size = BIO_get_mem_data(m_bioOut, &data);
    if (!data || size <= 0) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = m_transport->Write(data, size, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(m_bioOut)) != 1) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    info("https: ClientHello done");

    // Receive ServerHello, Certificate, Server Key Exchange, Server Hello Done
    while (true) {
        char buf[512]; ssize_t nn = 0;
        if ((err = m_transport->Read(buf, sizeof(buf), &nn)) != SUCCESS) {
            return ERRORWRAP(err, "handshake: read");
        }

        if ((r0 = BIO_write(m_bioIn, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        if ((r0 = SSL_do_handshake(m_ssl)) != -1 || (r1 = SSL_get_error(m_ssl, r0)) != SSL_ERROR_WANT_READ) {
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(m_bioOut, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(m_bioIn)) != 1) {
                return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    info("https: ServerHello done");

    // Send Client Key Exchange, Change Cipher Spec, Encrypted Handshake Message
    if ((err = m_transport->Write(data, size, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(m_bioOut)) != 1) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    info("https: Client done");

    // Receive New Session Ticket, Change Cipher Spec, Encrypted Handshake Message
    while (true) {
        char buf[128];
        ssize_t nn = 0;
        if ((err = m_transport->Read(buf, sizeof(buf), &nn)) != SUCCESS) {
            return ERRORWRAP(err, "handshake: read");
        }

        if ((r0 = BIO_write(m_bioIn, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(m_ssl); r1 = SSL_get_error(m_ssl, r0);
        if (r0 == 1 && r1 == SSL_ERROR_NONE) {
            break;
        }

        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }
    }

    info("https: Server done");

    return err;
}
#pragma GCC diagnostic pop

error SslClient::Read(void *plaintext, size_t nn_plaintext, ssize_t *nread)
{
    error err = SUCCESS;

    while (true) {
        int r0 = SSL_read(m_ssl, plaintext, nn_plaintext); int r1 = SSL_get_error(m_ssl, r0);
        int r2 = BIO_ctrl_pending(m_bioIn); int r3 = SSL_is_init_finished(m_ssl);

        // OK, got data.
        if (r0 > 0) {
            Assert(r0 <= (int)nn_plaintext);
            if (nread) {
                *nread = r0;
            }
            return err;
        }

        // Need to read more data to feed SSL.
        if (r0 == -1 && r1 == SSL_ERROR_WANT_READ) {
            // TODO: Can we avoid copy?
            int nn_cipher = nn_plaintext;
            char* cipher = new char[nn_cipher];
            AutoFreeA(char, cipher);

            // Read the cipher from SSL.
            ssize_t nn = 0;
            if ((err = m_transport->Read(cipher, nn_cipher, &nn)) != SUCCESS) {
                return ERRORWRAP(err, "https: read");
            }

            int r0 = BIO_write(m_bioIn, cipher, nn);
            if (r0 <= 0) {
                // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
                return ERRORNEW(ERROR_HTTPS_READ, "BIO_write r0=%d, cipher=%p, size=%d", r0, cipher, nn);
            }
            continue;
        }

        // Fail for error.
        if (r0 <= 0) {
            return ERRORNEW(ERROR_HTTPS_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                r0, r1, r2, r3);
        }
    }
}

error SslClient::Write(void *plaintext, size_t nn_plaintext, ssize_t *nwrite)
{
    error err = SUCCESS;

    for (char* p = (char*)plaintext; p < (char*)plaintext + nn_plaintext;) {
        int left = (int)nn_plaintext - (p - (char*)plaintext);
        int r0 = SSL_write(m_ssl, (const void*)p, left);
        int r1 = SSL_get_error(m_ssl, r0);
        if (r0 <= 0) {
            return ERRORNEW(ERROR_HTTPS_WRITE, "https: write data=%p, size=%d, r0=%d, r1=%d", p, left, r0, r1);
        }

        // Move p to the next writing position.
        p += r0;
        if (nwrite) {
            *nwrite += (ssize_t)r0;
        }

        uint8_t* data = NULL;
        int size = BIO_get_mem_data(m_bioOut, &data);
        if ((err = m_transport->Write(data, size, NULL)) != SUCCESS) {
            return ERRORWRAP(err, "https: write data=%p, size=%d", data, size);
        }
        if ((r0 = BIO_reset(m_bioOut)) != 1) {
            return ERRORNEW(ERROR_HTTPS_WRITE, "BIO_reset r0=%d", r0);
        }
    }

    return err;
}

HttpClient::HttpClient()
{
    m_transport = NULL;
    m_sslTransport = NULL;
    m_kbps = new NetworkKbps();
    m_parser = NULL;
    m_recvTimeout = m_timeout = UTIME_NO_TIMEOUT;
    m_port = 0;
}

HttpClient::~HttpClient()
{
    Disconnect();

    Freep(m_kbps);
    Freep(m_parser);
}

error HttpClient::Initialize(std::string schema, std::string h, int p, utime_t tm)
{
    error err = SUCCESS;

    Freep(m_parser);
    m_parser = new HttpParser();

    if ((err = m_parser->Initialize(HTTP_RESPONSE)) != SUCCESS) {
        return ERRORWRAP(err, "http: init parser");
    }

    // Always disconnect the transport.
    m_schema = schema;
    m_host = h;
    m_port = p;
    m_recvTimeout = m_timeout = tm;
    Disconnect();

    // ep used for host in header.
    std::string ep = m_host;
    if (m_port > 0 && m_port != CONSTS_HTTP_DEFAULT_PORT) {
        ep += ":" + Int2Str(m_port);
    }

    // Set default value for headers.
    m_headers["Host"] = ep;
    m_headers["Connection"] = "Keep-Alive";
    m_headers["User-Agent"] = RTMP_SIG_SERVER;
    m_headers["Content-Type"] = "application/json";

    return err;
}

HttpClient *HttpClient::SetHeader(std::string k, std::string v)
{
    m_headers[k] = v;

    return this;
}

error HttpClient::Post(std::string path, std::string req, IHttpMessage **ppmsg)
{
    *ppmsg = NULL;

    error err = SUCCESS;

    // always set the content length.
    m_headers["Content-Length"] = Int2Str(req.length());

    if ((err = Connect()) != SUCCESS) {
        return ERRORWRAP(err, "http: connect server");
    }

    if (path.size() == 0) {
        path = "/";
    }

    // TODO: FIXME: Use SrsHttpMessageWriter, never use stringstream and headers.
    // send POST request to uri
    // POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "POST " << path << " " << "HTTP/1.1" << HTTP_CRLF;
    for (std::map<std::string, std::string>::iterator it = m_headers.begin(); it != m_headers.end(); ++it) {
        std::string key = it->first;
        std::string value = it->second;
        ss << key << ": " << value << HTTP_CRLF;
    }
    ss << HTTP_CRLF << req;

    std::string data = ss.str();
    if ((err = Writer()->Write((void*)data.c_str(), data.length(), NULL)) != SUCCESS) {
        // Disconnect the transport when channel error, reconnect for next operation.
        Disconnect();
        return ERRORWRAP(err, "http: write");
    }

    IHttpMessage* msg = NULL;
    if ((err = m_parser->ParseMessage(Reader(), &msg)) != SUCCESS) {
        return ERRORWRAP(err, "http: parse response");
    }
    Assert(msg);

    if (ppmsg) {
        *ppmsg = msg;
    } else {
        Freep(msg);
    }

    return err;
}

error HttpClient::Get(std::string path, std::string req, IHttpMessage **ppmsg)
{
    *ppmsg = NULL;

    error err = SUCCESS;

    // always set the content length.
    m_headers["Content-Length"] = Int2Str(req.length());

    if ((err = Connect()) != SUCCESS) {
        return ERRORWRAP(err, "http: connect server");
    }

    // send POST request to uri
    // GET %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "GET " << path << " " << "HTTP/1.1" << HTTP_CRLF;
    for (std::map<std::string, std::string>::iterator it = m_headers.begin(); it != m_headers.end(); ++it) {
        std::string key = it->first;
        std::string value = it->second;
        ss << key << ": " << value << HTTP_CRLF;
    }
    ss << HTTP_CRLF << req;

    std::string data = ss.str();
    if ((err = Writer()->Write((void*)data.c_str(), data.length(), NULL)) != SUCCESS) {
        // Disconnect the transport when channel error, reconnect for next operation.
        Disconnect();
        return ERRORWRAP(err, "http: write");
    }

    IHttpMessage* msg = NULL;
    if ((err = m_parser->ParseMessage(Reader(), &msg)) != SUCCESS) {
        return ERRORWRAP(err, "http: parse response");
    }
    Assert(msg);

    if (ppmsg) {
        *ppmsg = msg;
    } else {
        Freep(msg);
    }

    return err;
}

void HttpClient::SetRecvTimeout(utime_t tm)
{
    m_recvTimeout = tm;
}

void HttpClient::KbpsSample(const char *label, utime_t age)
{
    m_kbps->Sample();

    int sr = m_kbps->GetSendKbps();
    int sr30s = m_kbps->GetSendKbps30s();
    int sr5m = m_kbps->GetSendKbps5m();
    int rr = m_kbps->GetRecvKbps();
    int rr30s = m_kbps->GetRecvKbps30s();
    int rr5m = m_kbps->GetRecvKbps5m();

    trace("<- %s time=%" PRId64 ", okbps=%d,%d,%d, ikbps=%d,%d,%d", label, u2ms(age), sr, sr30s, sr5m, rr, rr30s, rr5m);
}

void HttpClient::Disconnect()
{
    m_kbps->SetIo(NULL, NULL);
    Freep(m_sslTransport);
    Freep(m_transport);
}

error HttpClient::Connect()
{
    error err = SUCCESS;

    // When transport connected, ignore.
    if (m_transport) {
        return err;
    }

    m_transport = new TcpClient(m_host, m_port, m_timeout);
    if ((err = m_transport->Connect()) != SUCCESS) {
        Disconnect();
        return ERRORWRAP(err, "http: tcp connect %s %s:%d to=%dms, rto=%dms",
            m_schema.c_str(), m_host.c_str(), m_port, u2msi(m_timeout), u2msi(m_recvTimeout));
    }

    // Set the recv/send timeout in utime_t.
    m_transport->SetRecvTimeout(m_recvTimeout);
    m_transport->SetSendTimeout(m_timeout);

    m_kbps->SetIo(m_transport, m_transport);

    if (m_schema != "https") {
        return err;
    }

#if !defined(SRS_HTTPS)
    return ERRORNEW(ERROR_HTTPS_NOT_SUPPORTED, "should configure with --https=on");
#else
    Assert(!ssl_transport);
    ssl_transport = new SrsSslClient(transport);

    utime_t starttime = srs_update_system_time();

    if ((err = ssl_transport->handshake()) != SUCCESS) {
        disconnect();
        return ERRORWRAP(err, "http: ssl connect %s %s:%d to=%dms, rto=%dms",
            schema_.c_str(), host.c_str(), port, srsu2msi(timeout), srsu2msi(recv_timeout));
    }

    int cost = srsu2msi(srs_update_system_time() - starttime);
    srs_trace("https: connected to %s://%s:%d, cost=%dms", schema_.c_str(), host.c_str(), port, cost);

    return err;
#endif
}

IStreamWriter *HttpClient::Writer()
{
    if (m_sslTransport) {
        return m_sslTransport;
    }
    return m_transport;
}

IReader *HttpClient::Reader()
{
    if (m_sslTransport) {
        return m_sslTransport;
    }
    return m_transport;
}

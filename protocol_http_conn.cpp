#include "protocol_http_conn.h"
#include "protocol_conn.h"
#include "protocol_io.h"
#include "protocol_rtmp_stack.h"
#include "protocol_stream.h"
#include "consts.h"
#include "protocol_utility.h"
#include "utility.h"
#include <cstring>
#include <inttypes.h>

HttpParser::HttpParser()
{
    m_buffer = new FastStream();
    m_header = NULL;

    m_type = HTTP_REQUEST;
    m_parsedType = HTTP_BOTH;
}

HttpParser::~HttpParser()
{
    Freep(m_buffer);
    Freep(m_header);
}

error HttpParser::Initialize(http_parser_type type)
{
    error err = SUCCESS;

    m_jsonp = false;
    m_type = type;

    // Initialize the parser, however it's not necessary.
    HttpParserInit(&m_parser, m_type);
    m_parser.data = (void*)this;

    memset(&m_settings, 0, sizeof(m_settings));
    m_settings.on_message_begin = OnMessageBegin;
    m_settings.on_url = OnUrl;
    m_settings.on_header_field = OnHeaderField;
    m_settings.on_header_value = OnHeaderValue;
    m_settings.on_headers_complete = OnHeadersComplete;
    m_settings.on_body = OnBody;
    m_settings.on_message_complete = OnMessageComplete;

    return err;
}

void HttpParser::SetJsonp(bool allow_jsonp)
{
    m_jsonp = allow_jsonp;
}

error HttpParser::ParseMessage(IReader *reader, IHttpMessage **ppmsg)
{
    error err = SUCCESS;

    *ppmsg = NULL;

    // Reset parser data and state.
    m_state = HttpParseStateInit;
    memset(&m_hpHeader, 0, sizeof(http_parser));
    // We must reset the field name and value, because we may get a partial value in on_header_value.
    m_fieldName = m_fieldValue = "";
    // Reset the url.
    m_url = "";
    // The header of the request.
    Freep(m_header);
    m_header = new HttpHeader();

    // Reset parser for each message.
    // If the request is large, such as the fifth message at @utest ProtocolHTTPTest.ParsingLargeMessages,
    // we got header and part of body, so the parser will stay at SrsHttpParseStateBody:
    //      ***MESSAGE BEGIN***
    //      ***HEADERS COMPLETE***
    //      Body: xxx
    // when got next message, the whole next message is parsed as the body of previous one,
    // and the message fail.
    // @note You can comment the bellow line, the utest will fail.
    HttpParserInit(&m_parser, m_type);
    // Reset the parsed type.
    m_parsedType = HTTP_BOTH;
    // callback object ptr.
    m_parser.data = (void*)this;
    // Always skip body, because we only want to parse the header.
    m_parser.flags |= F_SKIPBODY;

    // do parse
    if ((err = ParseMessageImp(reader)) != SUCCESS) {
        return ERRORWRAP(err, "parse message");
    }

    // create msg
    HttpMessage* msg = new HttpMessage(reader, m_buffer);

    // Initialize the basic information.
    msg->SetBasic(m_hpHeader.type, (http_method)m_hpHeader.method, (http_status)m_hpHeader.status_code, m_hpHeader.content_length);
    msg->SetHeader(m_header, HttpShouldKeepAlive(&m_hpHeader));
    // For HTTP response, no url.
    if (m_parsedType != HTTP_RESPONSE && (err = msg->SetUrl(m_url, m_jsonp)) != SUCCESS) {
        Freep(msg);
        return ERRORWRAP(err, "set url=%s, jsonp=%d", m_url.c_str(), m_jsonp);
    }

    // parse ok, return the msg.
    *ppmsg = msg;

    return err;
}

error HttpParser::ParseMessageImp(IReader *reader)
{
    error err = SUCCESS;

    while (true) {
        if (m_buffer->Size() > 0) {
            ssize_t consumed = HttpParserExecute(&m_parser, &m_settings, m_buffer->Bytes(), m_buffer->Size());

            // The error is set in http_errno.
            enum http_errno code = HTTP_PARSER_ERRNO(&m_parser);
            if (code != HPE_OK) {
                return ERRORNEW(ERROR_HTTP_PARSE_HEADER, "parse %dB, nparsed=%d, err=%d/%s %s",
                    m_buffer->Size(), (int)consumed, code, HttpErrnoName(code), HttpErrnoDescription(code));
            }

            info("size=%d, nparsed=%d", m_buffer->Size(), (int)consumed);

            // Only consume the header bytes.
            m_buffer->ReadSlice(consumed);

            // Done when header completed, never wait for body completed, because it maybe chunked.
            if (m_state >= HttpParseStateHeaderComplete) {
                break;
            }
        }

        // when nothing parsed, read more to parse.
        // when requires more, only grow 1bytes, but the buffer will cache more.
        if ((err = m_buffer->Grow(reader, m_buffer->Size() + 1)) != SUCCESS) {
            return ERRORWRAP(err, "grow buffer");
        }
    }

    HttpParser* obj = this;
    if (!obj->m_fieldValue.empty()) {
        obj->m_header->Set(obj->m_fieldName, obj->m_fieldValue);
    }

    return err;
}

int HttpParser::OnMessageBegin(http_parser *parser)
{
    HttpParser* obj = (HttpParser*)parser->data;
    Assert(obj);

    // Now, we start to parse HTTP message.
    obj->m_state = HttpParseStateStart;

    // If we set to HTTP_BOTH, the type is detected and speicifed by parser.
    obj->m_parsedType = (http_parser_type)parser->type;

    info("***MESSAGE BEGIN***");

    return 0;
}

int HttpParser::OnHeadersComplete(http_parser *parser)
{
    HttpParser* obj = (HttpParser*)parser->data;
    Assert(obj);

    obj->m_hpHeader = *parser;
    // save the parser when header parse completed.
    obj->m_state = HttpParseStateHeaderComplete;

    info("***HEADERS COMPLETE***");

    // The return code of this callback:
    //      0: Continue to process body.
    //      1: Skip body, but continue to parse util all data parsed.
    //      2: Upgrade and skip body and left message, because it is in a different protocol.
    //      N: Error and failed as HPE_CB_headers_complete.
    // We choose 2 because we only want to parse the header, not the body.
    return 2;
}

int HttpParser::OnMessageComplete(http_parser *parser)
{
    HttpParser* obj = (HttpParser*)parser->data;
    Assert(obj);

    // save the parser when body parse completed.
    obj->m_state = HttpParseStateMessageComplete;

    info("***MESSAGE COMPLETE***\n");

    return 0;
}

int HttpParser::OnUrl(http_parser *parser, const char *at, size_t length)
{
    HttpParser* obj = (HttpParser*)parser->data;
    Assert(obj);

    if (length > 0) {
        // Note that this function might be called for multiple times, and we got pieces of content.
        obj->m_url.append(at, (int)length);
    }

    info("Method: %d, Url: %.*s", parser->method, (int)length, at);

    return 0;
}

int HttpParser::OnHeaderField(http_parser *parser, const char *at, size_t length)
{
    HttpParser* obj = (HttpParser*)parser->data;
    Assert(obj);

    if (!obj->m_fieldValue.empty()) {
        obj->m_header->Set(obj->m_fieldName, obj->m_fieldValue);
        obj->m_fieldName = obj->m_fieldValue = "";
    }

    if (length > 0) {
        obj->m_fieldName.append(at, (int)length);
    }

    info("Header field(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int HttpParser::OnHeaderValue(http_parser *parser, const char *at, size_t length)
{
    HttpParser* obj = (HttpParser*)parser->data;
    Assert(obj);

    if (length > 0) {
        obj->m_fieldValue.append(at, (int)length);
    }

    info("Header value(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int HttpParser::OnBody(http_parser *parser, const char *at, size_t length)
{
    HttpParser* obj = (HttpParser*)parser->data;
    Assert(obj);

    // save the parser when body parsed.
    obj->m_state = HttpParseStateBody;

    info("Body: %.*s", (int)length, at);

    return 0;
}

HttpMessage::HttpMessage(IReader *reader, FastStream *buffer)
{
    m_ownerConn = NULL;
    m_chunked = false;
    m_uri = new HttpUri();
    m_body = new HttpResponseReader(this, reader, buffer);

    m_jsonp = false;

    // As 0 is DELETE, so we use GET as default.
    m_method = (http_method)CONSTS_HTTP_GET;
    // 200 is ok.
    m_status = (http_status)CONSTS_HTTP_OK;
    // -1 means infinity chunked mode.
    m_contentLength = -1;
    // From HTTP/1.1, default to keep alive.
    m_keepAlive = true;
    m_type = 0;

    m_schema = "http";
}

HttpMessage::~HttpMessage()
{
    Freep(m_body);
    Freep(m_uri);
}

void HttpMessage::SetBasic(uint8_t type, http_method method, http_status status, int64_t content_length)
{
    m_type = type;
    m_method = method;
    m_status = status;
    if (m_contentLength == -1) {
        m_contentLength = content_length;
    }
}

void HttpMessage::SetHeader(HttpHeader *header, bool keep_alive)
{
    m_header = *header;
    m_keepAlive = keep_alive;

    // whether chunked.
    m_chunked = (header->Get("Transfer-Encoding") == "chunked");

    // Update the content-length in header.
    std::string clv = header->Get("Content-Length");
    if (!clv.empty()) {
        m_contentLength = ::atoll(clv.c_str());
    }

    // If no size(content-length or chunked), it's infinite chunked,
    // it means there is no body, so we must close the body reader.
    if (!m_chunked && m_contentLength == -1) {
        // The infinite chunked is only enabled for HTTP_RESPONSE, so we close the body for request.
        if (m_type == HTTP_REQUEST) {
            m_body->Close();
        }
    }
}

error HttpMessage::SetUrl(std::string url, bool allow_jsonp)
{
    error err = SUCCESS;

    m_url = url;

    // parse uri from schema/server:port/path?query
    std::string uri = m_url;

    if (!StringContains(uri, "://")) {
        // use server public ip when host not specified.
        // to make telnet happy.
        std::string host = m_header.Get("Host");

        // If no host in header, we use local discovered IP, IPv4 first.
        if (host.empty()) {
            host = GetPublicInternetAddress(true);
        }

        // The url must starts with slash if no schema. For example, SIP request line starts with "sip".
        if (!host.empty() && !StringStartsWith(m_url, "/")) {
            host += "/";
        }

        if (!host.empty()) {
            uri = "http://" + host + m_url;
        }
    }

    if ((err = m_uri->Initialize(uri)) != SUCCESS) {
        return ERRORWRAP(err, "init uri %s", uri.c_str());
    }

    // parse ext.
    m_ext = PathFilext(m_uri->GetPath());

    // parse query string.
    ParseQueryString(m_uri->GetQuery(), m_query);

    // parse jsonp request message.
    if (allow_jsonp) {
        if (!QueryGet("callback").empty()) {
            m_jsonp = true;
        }
        if (m_jsonp) {
            m_jsonpMethod = QueryGet("method");
        }
    }

    return err;
}

void HttpMessage::SetHttps(bool v)
{
    m_schema = v? "https" : "http";
    m_uri->SetSchema(m_schema);
}

IConnection *HttpMessage::Connection()
{
    return m_ownerConn;
}

void HttpMessage::SetConnection(IConnection *conn)
{
    m_ownerConn = conn;
}

std::string HttpMessage::Schema()
{
    return m_schema;
}

uint8_t HttpMessage::MessageType()
{
    return m_type;
}

uint8_t HttpMessage::Method()
{
    if (m_jsonp && !m_jsonpMethod.empty()) {
        if (m_jsonpMethod == "GET") {
            return CONSTS_HTTP_GET;
        } else if (m_jsonpMethod == "PUT") {
            return CONSTS_HTTP_PUT;
        } else if (m_jsonpMethod == "POST") {
            return CONSTS_HTTP_POST;
        } else if (m_jsonpMethod == "DELETE") {
            return CONSTS_HTTP_DELETE;
        }
    }

    return m_method;
}

uint16_t HttpMessage::StatusCodes()
{
    return m_status;
}

std::string HttpMessage::MethodStr()
{
    if (m_jsonp && !m_jsonpMethod.empty()) {
        return m_jsonpMethod;
    }

    return HttpMethodStr((http_method)m_method);
}

bool HttpMessage::IsHttpGet()
{
    return Method() == CONSTS_HTTP_GET;
}

bool HttpMessage::IsHttpPut()
{
    return Method() == CONSTS_HTTP_PUT;
}

bool HttpMessage::IsHttpPost()
{
    return Method() == CONSTS_HTTP_POST;
}

bool HttpMessage::IsHttpDelete()
{
    return Method() == CONSTS_HTTP_DELETE;
}

bool HttpMessage::IsHttpOptions()
{
    return m_method == CONSTS_HTTP_OPTIONS;
}

bool HttpMessage::IsChunked()
{
    return m_chunked;
}

bool HttpMessage::IsKeepAlive()
{
    return m_keepAlive;
}

std::string HttpMessage::Uri()
{
    std::string uri = m_uri->GetSchema();
    if (uri.empty()) {
        uri += "http";
    }
    uri += "://";

    uri += Host();
    uri += Path();

    return uri;
}

std::string HttpMessage::Url()
{
    return m_uri->GetUrl();
}

std::string HttpMessage::Host()
{
    std::map<std::string, std::string>::iterator it = m_query.find("vhost");
    if (it != m_query.end() && !it->second.empty()) {
        return it->second;
    }

    it = m_query.find("domain");
    if (it != m_query.end() && !it->second.empty()) {
        return it->second;
    }

    return m_uri->GetHost();
}

int HttpMessage::Port()
{
    return m_uri->GetPort();
}

std::string HttpMessage::Path()
{
    return m_uri->GetPath();
}

std::string HttpMessage::Query()
{
    return m_uri->GetQuery();
}

std::string HttpMessage::Ext()
{
    return m_ext;
}

std::string HttpMessage::ParseRestId(std::string pattern)
{
    std::string p = m_uri->GetPath();
    if (p.length() <= pattern.length()) {
        return "";
    }

    std::string id = p.substr((int)pattern.length());
    if (!id.empty()) {
        return id;
    }

    return "";
}

error HttpMessage::BodyReadAll(std::string &body)
{
    return IoutilReadAll(m_body, body);
}

IHttpResponseReader *HttpMessage::BodyReader()
{
    return m_body;
}

int64_t HttpMessage::ContentLength()
{
    return m_contentLength;
}

std::string HttpMessage::QueryGet(std::string key)
{
    std::string v;

    if (m_query.find(key) != m_query.end()) {
        v = m_query[key];
    }

    return v;
}

HttpHeader *HttpMessage::Header()
{
    return &m_header;
}

Request *HttpMessage::ToRequest(std::string vhost)
{
    Request* req = new Request();

    // http path, for instance, /live/livestream.flv, parse to
    //      app: /live
    //      stream: livestream.flv
    ParseRtmpUrl(m_uri->GetPath(), req->m_app, req->m_stream);

    // trim the start slash, for instance, /live to live
    req->m_app = StringTrimStart(req->m_app, "/");

    // remove the extension, for instance, livestream.flv to livestream
    req->m_stream = PathFilename(req->m_stream);

    // generate others.
    req->m_tcUrl = "rtmp://" + vhost + "/" + req->m_app;
    req->m_pageUrl = m_header.Get("Referer");
    req->m_objectEncoding = 0;

    std::string query = m_uri->GetQuery();
    if (!query.empty()) {
        req->m_param = "?" + query;
    }

    DiscoveryTcUrl(req->m_tcUrl, req->m_schema, req->m_host, req->m_vhost, req->m_app, req->m_stream, req->m_port, req->m_param);
    req->Strip();

    // reset the host to http request host.
    if (req->m_host == CONSTS_RTMP_DEFAULT_VHOST) {
        req->m_host = m_uri->GetHost();
    }

    // Set ip by remote ip of connection.
    if (m_ownerConn) {
        req->m_ip = m_ownerConn->RemoteIp();
    }

    // Overwrite by ip from proxy.
    std::string oip = GetOriginalIp(this);
    if (!oip.empty()) {
        req->m_ip = oip;
    }

    // The request streaming protocol.
    req->m_protocol = (m_schema == "http")? "flv" : "flvs";

    return req;
}

bool HttpMessage::IsJsonp()
{
    return m_jsonp;
}

IHttpHeaderFilter::IHttpHeaderFilter()
{

}

IHttpHeaderFilter::~IHttpHeaderFilter()
{

}

IHttpFirstLineWriter::IHttpFirstLineWriter()
{

}

IHttpFirstLineWriter::~IHttpFirstLineWriter()
{

}

HttpMessageWriter::HttpMessageWriter(IProtocolReadWriter *io, IHttpFirstLineWriter *flw)
{
    m_skt = io;
    m_hdr = new HttpHeader();
    m_headerWrote = false;
    m_contentLength = -1;
    m_written = 0;
    m_headerSent = false;
    m_nbIovssCache = 0;
    m_iovssCache = NULL;
    m_hf = NULL;
    m_flw = flw;
}

HttpMessageWriter::~HttpMessageWriter()
{
    Freep(m_hdr);
    Freepa(m_iovssCache);
}

error HttpMessageWriter::FinalRequest()
{
    error err = SUCCESS;

    // write the header data in memory.
    if (!m_headerWrote) {
        m_flw->WriteDefaultHeader();
    }

    // whatever header is wrote, we should try to send header.
    if ((err = SendHeader(NULL, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send header");
    }

    // complete the chunked encoding.
    if (m_contentLength == -1) {
        std::stringstream ss;
        ss << 0 << HTTP_CRLF << HTTP_CRLF;
        std::string ch = ss.str();
        return m_skt->Write((void*)ch.data(), (int)ch.length(), NULL);
    }

    // flush when send with content length
    return Write(NULL, 0);
}

HttpHeader *HttpMessageWriter::Header()
{
    return m_hdr;
}

error HttpMessageWriter::Write(char *data, int size)
{
    error err = SUCCESS;

    // write the header data in memory.
    if (!m_headerWrote) {
        if (m_hdr->ContentType().empty()) {
            m_hdr->SetContentType("text/plain; charset=utf-8");
        }
        if (m_hdr->ContentLength() == -1) {
            m_hdr->SetContentLength(size);
        }
        m_flw->WriteDefaultHeader();
    }

    // whatever header is wrote, we should try to send header.
    if ((err = SendHeader(data, size)) != SUCCESS) {
        return ERRORWRAP(err, "send header");
    }

    // check the bytes send and content length.
    m_written += size;
    if (m_contentLength != -1 && m_written > m_contentLength) {
        return ERRORNEW(ERROR_HTTP_CONTENT_LENGTH, "overflow writen=%" PRId64 ", max=%" PRId64, m_written, m_contentLength);
    }

    // ignore NULL content.
    if (!data || size <= 0) {
        return err;
    }

    // directly send with content length
    if (m_contentLength != -1) {
        return m_skt->Write((void*)data, size, NULL);
    }

    // send in chunked encoding.
    int nb_size = snprintf(m_headerCache, HTTP_HEADER_CACHE_SIZE, "%x", size);
    if (nb_size <= 0 || nb_size >= HTTP_HEADER_CACHE_SIZE) {
        return ERRORNEW(ERROR_HTTP_CONTENT_LENGTH, "overflow size=%d, expect=%d", size, nb_size);
    }

    iovec iovs[4];
    iovs[0].iov_base = (char*)m_headerCache;
    iovs[0].iov_len = (int)nb_size;
    iovs[1].iov_base = (char*)HTTP_CRLF;
    iovs[1].iov_len = 2;
    iovs[2].iov_base = (char*)data;
    iovs[2].iov_len = size;
    iovs[3].iov_base = (char*)HTTP_CRLF;
    iovs[3].iov_len = 2;

    ssize_t nwrite = 0;
    if ((err = m_skt->Writev(iovs, 4, &nwrite)) != SUCCESS) {
        return ERRORWRAP(err, "write chunk");
    }

    return err;
}

error HttpMessageWriter::Writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    error err = SUCCESS;

    // when header not ready, or not chunked, send one by one.
    if (!m_headerWrote || m_contentLength != -1) {
        ssize_t nwrite = 0;
        for (int i = 0; i < iovcnt; i++) {
            nwrite += iov[i].iov_len;
            if ((err = Write((char*)iov[i].iov_base, (int)iov[i].iov_len)) != SUCCESS) {
                return ERRORWRAP(err, "writev");
            }
        }

        if (pnwrite) {
            *pnwrite = nwrite;
        }

        return err;
    }

    // ignore NULL content.
    if (iovcnt <= 0) {
        return err;
    }

    // whatever header is wrote, we should try to send header.
    if ((err = SendHeader(NULL, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send header");
    }

    // send in chunked encoding.
    int nb_iovss = 3 + iovcnt;
    iovec* iovss = m_iovssCache;
    if (m_nbIovssCache < nb_iovss) {
        Freepa(m_iovssCache);
        m_nbIovssCache = nb_iovss;
        iovss = m_iovssCache = new iovec[nb_iovss];
    }

    // Send all iovs in one chunk, the size is the total size of iovs.
    int size = 0;
    for (int i = 0; i < iovcnt; i++) {
        const iovec* data_iov = iov + i;
        size += data_iov->iov_len;
    }
    m_written += size;

    // chunk header
    int nb_size = snprintf(m_headerCache, HTTP_HEADER_CACHE_SIZE, "%x", size);
    if (nb_size <= 0 || nb_size >= HTTP_HEADER_CACHE_SIZE) {
        return ERRORNEW(ERROR_HTTP_CONTENT_LENGTH, "overflow size=%d, expect=%d", size, nb_size);
    }
    iovss[0].iov_base = (char*)m_headerCache;
    iovss[0].iov_len = (int)nb_size;

    // chunk header eof.
    iovss[1].iov_base = (char*)HTTP_CRLF;
    iovss[1].iov_len = 2;

    // chunk body.
    for (int i = 0; i < iovcnt; i++) {
        iovss[2+i].iov_base = (char*)iov[i].iov_base;
        iovss[2+i].iov_len = (int)iov[i].iov_len;
    }

    // chunk body eof.
    iovss[2+iovcnt].iov_base = (char*)HTTP_CRLF;
    iovss[2+iovcnt].iov_len = 2;

    // sendout all ioves.
    ssize_t nwrite = 0;
    if ((err = WriteLargeIovs(m_skt, iovss, nb_iovss, &nwrite)) != SUCCESS) {
        return ERRORWRAP(err, "writev large iovs");
    }

    if (pnwrite) {
        *pnwrite = nwrite;
    }

    return err;
}

void HttpMessageWriter::WriteHeader()
{
    if (m_headerWrote) return;
    m_headerWrote = true;

    // parse the content length from header.
    m_contentLength = m_hdr->ContentLength();
}

error HttpMessageWriter::SendHeader(char *data, int size)
{
    error err = SUCCESS;

    if (m_headerSent) {
        return err;
    }
    m_headerSent = true;

    std::stringstream ss;

    // First line, the request line or status line.
    if ((err = m_flw->BuildFirstLine(ss, data, size)) != SUCCESS) {
        return ERRORWRAP(err, "first line");
    }

    // set server if not set.
    if (m_hdr->Get("Server").empty()) {
        m_hdr->Set("Server", RTMP_SIG_SERVER);
    }

    // chunked encoding
    if (m_contentLength == -1) {
        m_hdr->Set("Transfer-Encoding", "chunked");
    }

    // keep alive to make vlc happy.
    if (m_hdr->Get("Connection").empty()) {
        m_hdr->Set("Connection", "Keep-Alive");
    }

    // Filter the header before writing it.
    if (m_hf && ((err = m_hf->Filter(m_hdr)) != SUCCESS)) {
        return ERRORWRAP(err, "filter header");
    }

    // write header
    m_hdr->Write(ss);

    // header_eof
    ss << HTTP_CRLF;

    std::string buf = ss.str();
    return m_skt->Write((void*)buf.c_str(), buf.length(), NULL);
}

bool HttpMessageWriter::HeaderWrote()
{
    return m_headerWrote;
}

void HttpMessageWriter::SetHeaderFilter(IHttpHeaderFilter *hf)
{
    m_hf = hf;
}

HttpResponseWriter::HttpResponseWriter(IProtocolReadWriter *io)
{
    m_writer = new HttpMessageWriter(io, this);
    m_status = CONSTS_HTTP_OK;
}

HttpResponseWriter::~HttpResponseWriter()
{
    Freep(m_writer);
}

void HttpResponseWriter::SetHeaderFilter(IHttpHeaderFilter *hf)
{
    m_writer->SetHeaderFilter(hf);
}

error HttpResponseWriter::FinalRequest()
{
    return m_writer->FinalRequest();
}

HttpHeader *HttpResponseWriter::Header()
{
    return m_writer->Header();
}

error HttpResponseWriter::Write(char *data, int size)
{
    return m_writer->Write(data, size);
}

error HttpResponseWriter::Writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    return m_writer->Writev(iov, iovcnt, pnwrite);
}

void HttpResponseWriter::WriteHeader(int code)
{
    if (m_writer->HeaderWrote()) {
        warn("http: multiple write_header calls, status=%d, code=%d", m_status, code);
        return;
    }

    m_status = code;
    return m_writer->WriteHeader();
}

error HttpResponseWriter::BuildFirstLine(std::stringstream &ss, char *data, int size)
{
    error err = SUCCESS;

    // Write status line for response.
    ss << "HTTP/1.1 " << m_status << " " << GenerateHttpStatusText(m_status) << HTTP_CRLF;

    // Try to detect content type from response body data.
    HttpHeader* hdr = m_writer->Header();
    if (GoHttpBodyAllowd(m_status)) {
        if (data && hdr->ContentType().empty()) {
            hdr->SetContentType(GoHttpDetect(data, size));
        }
    }

    return err;
}

void HttpResponseWriter::WriteDefaultHeader()
{
    WriteHeader(CONSTS_HTTP_OK);
}

HttpRequestWriter::HttpRequestWriter(IProtocolReadWriter *io)
{
    m_writer = new HttpMessageWriter(io, this);
}

HttpRequestWriter::~HttpRequestWriter()
{
    Freep(m_writer);
}

error HttpRequestWriter::FinalRequest()
{
    return m_writer->FinalRequest();
}

HttpHeader *HttpRequestWriter::Header()
{
    return m_writer->Header();
}

error HttpRequestWriter::Write(char *data, int size)
{
    return m_writer->Write(data, size);
}

error HttpRequestWriter::Writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    return m_writer->Writev(iov, iovcnt, pnwrite);
}

void HttpRequestWriter::WriteHeader(const std::string &method, const std::string &path)
{
    if (m_writer->HeaderWrote()) {
        warn("http: multiple write_header calls, current=%s(%s), now=%s(%s)", m_method.c_str(), m_path.c_str(), method.c_str(), path.c_str());
        return;
    }

    m_method = method;
    m_path = path;
    return m_writer->WriteHeader();
}

error HttpRequestWriter::BuildFirstLine(std::stringstream &ss, char *data, int size)
{
    error err = SUCCESS;

    // Write status line for response.
    ss << m_method << " " << m_path << " HTTP/1.1" << HTTP_CRLF;

    // Try to detect content type from request body data.
    HttpHeader* hdr = m_writer->Header();
    if (data && hdr->ContentType().empty()) {
        hdr->SetContentType(GoHttpDetect(data, size));
    }

    return err;
}

void HttpRequestWriter::WriteDefaultHeader()
{
    WriteHeader("GET", "/");
}

HttpResponseReader::HttpResponseReader(HttpMessage *msg, IReader *reader, FastStream *buffer)
{
    m_skt = reader;
    m_owner = msg;
    m_isEof = false;
    m_nbTotalRead = 0;
    m_nbLeftChunk = 0;
    m_buffer = buffer;
    m_nbChunk = 0;
}

HttpResponseReader::~HttpResponseReader()
{

}

void HttpResponseReader::Close()
{
    m_isEof = true;
}

bool HttpResponseReader::Eof()
{
    return m_isEof;
}

error HttpResponseReader::Read(void *data, size_t nb_data, ssize_t *nb_read)
{
    error err = SUCCESS;

    if (m_isEof) {
        return ERRORNEW(ERROR_HTTP_RESPONSE_EOF, "EOF");
    }

    // chunked encoding.
    if (m_owner->IsChunked()) {
        return ReadChunked(data, nb_data, nb_read);
    }

    // read by specified content-length
    if (m_owner->ContentLength() != -1) {
        size_t max = (size_t)m_owner->ContentLength() - (size_t)m_nbTotalRead;
        if (max <= 0) {
            m_isEof = true;
            return err;
        }

        // change the max to read.
        nb_data = MIN(nb_data, max);
        return ReadSpecified(data, nb_data, nb_read);
    }

    // Infinite chunked mode.
    // If not chunked encoding, and no content-length, it's infinite chunked.
    // In this mode, all body is data and never EOF util socket closed.
    if ((err = ReadSpecified(data, nb_data, nb_read)) != SUCCESS) {
        // For infinite chunked, the socket close event is EOF.
        if (ERRORCODE(err) == ERROR_SOCKET_READ) {
            Freep(err); m_isEof = true;
            return err;
        }
    }

    return err;
}

error HttpResponseReader::ReadChunked(void *data, size_t nb_data, ssize_t *nb_read)
{
    error err = SUCCESS;

    // when no bytes left in chunk,
    // parse the chunk length first.
    if (m_nbLeftChunk <= 0) {
        char* at = NULL;
        int length = 0;
        while (!at) {
            // find the CRLF of chunk header end.
            char* start = m_buffer->Bytes();
            char* end = start + m_buffer->Size();
            for (char* p = start; p < end - 1; p++) {
                if (p[0] == HTTP_CR && p[1] == HTTP_LF) {
                    // invalid chunk, ignore.
                    if (p == start) {
                        return ERRORNEW(ERROR_HTTP_INVALID_CHUNK_HEADER, "chunk header");
                    }
                    length = (int)(p - start + 2);
                    at = m_buffer->ReadSlice(length);
                    break;
                }
            }

            // got at, ok.
            if (at) {
                break;
            }

            // when empty, only grow 1bytes, but the buffer will cache more.
            if ((err = m_buffer->Grow(m_skt, m_buffer->Size() + 1)) != SUCCESS) {
                return ERRORWRAP(err, "grow buffer");
            }
        }
        Assert(length >= 3);

        // it's ok to set the pos and pos+1 to NULL.
        at[length - 1] = 0;
        at[length - 2] = 0;

        // size is the bytes size, excludes the chunk header and end CRLF.
        // @remark It must be hex format, please read https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding#Directives
        // @remark For strtol, note that: If no conversion could be performed, 0 is returned and the global variable errno is set to EINVAL.
        char* at_parsed = at; errno = 0;
        int ilength = (int)::strtol(at, &at_parsed, 16);
        if (ilength < 0 || errno != 0 || at_parsed - at != length - 2) {
            return ERRORNEW(ERROR_HTTP_INVALID_CHUNK_HEADER, "invalid length %s as %d, parsed=%.*s, errno=%d",
                at, ilength, (int)(at_parsed-at), at, errno);
        }

        // all bytes in chunk is left now.
        m_nbChunk = m_nbLeftChunk = (size_t)ilength;
    }

    if (m_nbChunk <= 0) {
        // for the last chunk, eof.
        m_isEof = true;
        *nb_read = 0;
    } else {
        // for not the last chunk, there must always exists bytes.
        // left bytes in chunk, read some.
        Assert(m_nbLeftChunk);

        size_t nb_bytes = MIN(m_nbLeftChunk, nb_data);
        err = ReadSpecified(data, nb_bytes, (ssize_t*)&nb_bytes);

        // the nb_bytes used for output already read size of bytes.
        if (nb_read) {
            *nb_read = nb_bytes;
        }
        m_nbLeftChunk -= nb_bytes;

        if (err != SUCCESS) {
            return ERRORWRAP(err, "read specified");
        }

        // If still left bytes in chunk, ignore and read in future.
        if (m_nbLeftChunk > 0) {
            return err;
        }
    }

    // for both the last or not, the CRLF of chunk payload end.
    if ((err = m_buffer->Grow(m_skt, 2)) != SUCCESS) {
        return ERRORWRAP(err, "grow buffer");
    }
    m_buffer->ReadSlice(2);

    return err;
}

error HttpResponseReader::ReadSpecified(void *data, size_t nb_data, ssize_t *nb_read)
{
    error err = SUCCESS;

    if (m_buffer->Size() <= 0) {
        // when empty, only grow 1bytes, but the buffer will cache more.
        if ((err = m_buffer->Grow(m_skt, 1)) != SUCCESS) {
            return ERRORWRAP(err, "grow buffer");
        }
    }

    size_t nb_bytes = MIN(nb_data, (size_t)m_buffer->Size());

    // read data to buffer.
    Assert(nb_bytes);
    char* p = m_buffer->ReadSlice(nb_bytes);
    memcpy(data, p, nb_bytes);
    if (nb_read) {
        *nb_read = nb_bytes;
    }

    // increase the total read to determine whether EOF.
    m_nbTotalRead += nb_bytes;

    // for not chunked and specified content length.
    if (!m_owner->IsChunked() && m_owner->ContentLength() != -1) {
        // when read completed, eof.
        if (m_nbTotalRead >= (int)m_owner->ContentLength()) {
            m_isEof = true;
        }
    }

    return err;
}

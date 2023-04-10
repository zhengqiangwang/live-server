#ifndef PROTOCOL_HTTP_CONN_H
#define PROTOCOL_HTTP_CONN_H

#include "error.h"
#include "protocol_http_stack.h"
#include <string>

class IConnection;
class FastStream;
class Request;
class IReader;
class HttpResponseReader;
class IProtocolReadWriter;

// A wrapper for http-parser,
// provides HTTP message originted service.
class HttpParser
{
private:
    http_parser_settings m_settings;
    http_parser m_parser;
    // The global parse buffer.
    FastStream* m_buffer;
    // Whether allow jsonp parse.
    bool m_jsonp;
private:
    std::string m_fieldName;
    std::string m_fieldValue;
    HttpParseState m_state;
    http_parser m_hpHeader;
    std::string m_url;
    HttpHeader* m_header;
    enum http_parser_type m_type;
    enum http_parser_type m_parsedType;
public:
    HttpParser();
    virtual ~HttpParser();
public:
    // initialize the http parser with specified type,
    // one parser can only parse request or response messages.
    virtual error Initialize(enum http_parser_type type);
    // Whether allow jsonp parser, which indicates the method in query string.
    virtual void SetJsonp(bool allow_jsonp);
    // always parse a http message,
    // that is, the *ppmsg always NOT-NULL when return success.
    // or error and *ppmsg must be NULL.
    // @remark, if success, *ppmsg always NOT-NULL, *ppmsg always is_complete().
    // @remark user must free the ppmsg if not NULL.
    virtual error ParseMessage(IReader* reader, IHttpMessage** ppmsg);
private:
    // parse the HTTP message to member field: msg.
    virtual error ParseMessageImp(IReader* reader);
private:
    static int OnMessageBegin(http_parser* parser);
    static int OnHeadersComplete(http_parser* parser);
    static int OnMessageComplete(http_parser* parser);
    static int OnUrl(http_parser* parser, const char* at, size_t length);
    static int OnHeaderField(http_parser* parser, const char* at, size_t length);
    static int OnHeaderValue(http_parser* parser, const char* at, size_t length);
    static int OnBody(http_parser* parser, const char* at, size_t length);
};

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
class HttpMessage : public IHttpMessage
{
private:
    // The body object, reader object.
    // @remark, user can get body in string by get_body().
    HttpResponseReader* m_body;
    // Use a buffer to read and send ts file.
    // The transport connection, can be NULL.
    IConnection* m_ownerConn;
private:
    // The request type defined as
    //      enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };
    uint8_t m_type;
    // The HTTP method defined by HTTP_METHOD_MAP
    http_method m_method;
    http_status m_status;
    int64_t m_contentLength;
private:
    // The http headers
    HttpHeader m_header;
    // Whether the request indicates should keep alive for the http connection.
    bool m_keepAlive;
    // Whether the body is chunked.
    bool m_chunked;
private:
    std::string m_schema;
    // The parsed url.
    std::string m_url;
    // The extension of file, for example, .flv
    std::string m_ext;
    // The uri parser
    HttpUri* m_uri;
    // The query map
    std::map<std::string, std::string> m_query;
private:
    // Whether request is jsonp.
    bool m_jsonp;
    // The method in QueryString will override the HTTP method.
    std::string m_jsonpMethod;
public:
    HttpMessage(IReader* reader = NULL, FastStream* buffer = NULL);
    virtual ~HttpMessage();
public:
    // Set the basic information for HTTP request.
    // @remark User must call set_basic before set_header, because the content_length will be overwrite by header.
    virtual void SetBasic(uint8_t type, http_method method, http_status status, int64_t content_length);
    // Set HTTP header and whether the request require keep alive.
    // @remark User must call set_header before set_url, because the Host in header is used for url.
    virtual void SetHeader(HttpHeader* header, bool keep_alive);
    // set the original messages, then update the message.
    virtual error SetUrl(std::string url, bool allow_jsonp);
    // After parsed the message, set the schema to https.
    virtual void SetHttps(bool v);
public:
    // Get the owner connection, maybe NULL.
    virtual IConnection* Connection();
    virtual void SetConnection(IConnection* conn);
public:
    // The schema, http or https.
    virtual std::string Schema();
    virtual uint8_t MessageType();
    virtual uint8_t Method();
    virtual uint16_t StatusCodes();
    // The method helpers.
    virtual std::string MethodStr();
    virtual bool IsHttpGet();
    virtual bool IsHttpPut();
    virtual bool IsHttpPost();
    virtual bool IsHttpDelete();
    virtual bool IsHttpOptions();
    // Whether body is chunked encoding, for reader only.
    virtual bool IsChunked();
    // Whether should keep the connection alive.
    virtual bool IsKeepAlive();
    // The uri contains the host and path.
    virtual std::string Uri();
    // The url maybe the path.
    virtual std::string Url();
    virtual std::string Host();
    virtual int Port();
    virtual std::string Path();
    virtual std::string Query();
    virtual std::string Ext();
    // Get the RESTful matched id.
    virtual std::string ParseRestId(std::string pattern);
public:
    // Read body to string.
    // @remark for small http body.
    virtual error BodyReadAll(std::string& body);
    // Get the body reader, to read one by one.
    // @remark when body is very large, or chunked, use this.
    virtual IHttpResponseReader* BodyReader();
    // The content length, -1 for chunked or not set.
    virtual int64_t ContentLength();
    // Get the param in query string, for instance, query is "start=100&end=200",
    // then query_get("start") is "100", and query_get("end") is "200"
    virtual std::string QueryGet(std::string key);
    // Get the headers.
    virtual HttpHeader *Header();
public:
    // Convert the http message to a request.
    // @remark user must free the return request.
    virtual Request* ToRequest(std::string vhost);
public:
    virtual bool IsJsonp();
};

// The http chunked header size,
// for writev, there always one chunk to send it.
#define HTTP_HEADER_CACHE_SIZE 64

class IHttpHeaderFilter
{
public:
    IHttpHeaderFilter();
    virtual ~IHttpHeaderFilter();
public:
    // Filter the HTTP header h.
    virtual error Filter(HttpHeader* h) = 0;
};

class IHttpFirstLineWriter
{
public:
    IHttpFirstLineWriter();
    virtual ~IHttpFirstLineWriter();
public:
    // Build first line of HTTP message to ss. Note that data with size of bytes is the body to write, which enables us
    // to setup the header by detecting the body, and it might be NULL.
    virtual error BuildFirstLine(std::stringstream& ss, char* data, int size) = 0;
    // Write a default header line if user does not specify one.
    virtual void WriteDefaultHeader() = 0;
};

// Message writer use st socket, for writing HTTP request or response, which is only different at the first line. For
// HTTP request, the first line is RequestLine. While for HTTP response, it's StatusLine.
class HttpMessageWriter
{
private:
    IProtocolReadWriter* m_skt;
    HttpHeader* m_hdr;
    // Before writing header, there is a chance to filter it,
    // such as remove some headers or inject new.
    IHttpHeaderFilter* m_hf;
    // The first line writer.
    IHttpFirstLineWriter* m_flw;
private:
    char m_headerCache[HTTP_HEADER_CACHE_SIZE];
    iovec* m_iovssCache;
    int m_nbIovssCache;
private:
    // Reply header has been (logically) written
    bool m_headerWrote;
private:
    // The explicitly-declared Content-Length; or -1
    int64_t m_contentLength;
    // The number of bytes written in body
    int64_t m_written;
private:
    // The wroteHeader tells whether the header's been written to "the
    // wire" (or rather: w.conn.buf). this is unlike
    // (*response).wroteHeader, which tells only whether it was
    // logically written.
    bool m_headerSent;
public:
    HttpMessageWriter(IProtocolReadWriter* io, IHttpFirstLineWriter* flw);
    virtual ~HttpMessageWriter();
public:
    virtual error FinalRequest();
    virtual HttpHeader* Header();
    virtual error Write(char* data, int size);
    virtual error Writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void WriteHeader();
    virtual error SendHeader(char* data, int size);
public:
    bool HeaderWrote();
    void SetHeaderFilter(IHttpHeaderFilter* hf);
};

// Response writer use st socket
class HttpResponseWriter : public IHttpResponseWriter, public IHttpFirstLineWriter
{
protected:
    HttpMessageWriter* m_writer;
    // The status code passed to WriteHeader, for response only.
    int m_status;
public:
    HttpResponseWriter(IProtocolReadWriter* io);
    virtual ~HttpResponseWriter();
public:
    void SetHeaderFilter(IHttpHeaderFilter* hf);
// Interface ISrsHttpResponseWriter
public:
    virtual error FinalRequest();
    virtual HttpHeader* Header();
    virtual error Write(char* data, int size);
    virtual error Writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void WriteHeader(int code);
// Interface ISrsHttpFirstLineWriter
public:
    virtual error BuildFirstLine(std::stringstream& ss, char* data, int size);
    virtual void WriteDefaultHeader();
};

// Request writer use st socket
class HttpRequestWriter : public IHttpRequestWriter, public IHttpFirstLineWriter
{
protected:
    HttpMessageWriter* m_writer;
    // The method and path passed to WriteHeader, for request only.
    std::string m_method;
    std::string m_path;
public:
    HttpRequestWriter(IProtocolReadWriter* io);
    virtual ~HttpRequestWriter();
// Interface ISrsHttpResponseWriter
public:
    virtual error FinalRequest();
    virtual HttpHeader* Header();
    virtual error Write(char* data, int size);
    virtual error Writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void WriteHeader(const std::string& method, const std::string& path);
// Interface ISrsHttpFirstLineWriter
public:
    virtual error BuildFirstLine(std::stringstream& ss, char* data, int size);
    virtual void WriteDefaultHeader();
};

// Response reader use st socket.
class HttpResponseReader : public IHttpResponseReader
{
private:
    IReader* m_skt;
    HttpMessage* m_owner;
    FastStream* m_buffer;
    bool m_isEof;
    // The left bytes in chunk.
    size_t m_nbLeftChunk;
    // The number of bytes of current chunk.
    size_t m_nbChunk;
    // Already read total bytes.
    int64_t m_nbTotalRead;
public:
    // Generally the reader is the under-layer io such as socket,
    // while buffer is a fast cache which may have cached some data from reader.
    HttpResponseReader(HttpMessage* msg, IReader* reader, FastStream* buffer);
    virtual ~HttpResponseReader();
public:
    // User close the HTTP response reader.
    // For example, OPTIONS has no body, no content-length and not chunked,
    // so we must close it(set to eof) to avoid reading the response body.
    void Close();
// Interface ISrsHttpResponseReader
public:
    virtual bool Eof();
    virtual error Read(void* buf, size_t size, ssize_t* nread);
private:
    virtual error ReadChunked(void* buf, size_t size, ssize_t* nread);
    virtual error ReadSpecified(void* buf, size_t size, ssize_t* nread);
};

#endif // PROTOCOL_HTTP_CONN_H

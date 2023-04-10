#ifndef APP_HTTP_STREAM_H
#define APP_HTTP_STREAM_H


#include "app_server.h"
#include "app_source.h"
#include "file.h"
#include "protocol_http_stack.h"
#include "protocol_rtmp_stack.h"

class AacTransmuxer;
class Mp3Transmuxer;
class FlvTransmuxer;
class TsTransmuxer;

// A cache for HTTP Live Streaming encoder, to make android(weixin) happy.
class BufferCache : public ICoroutineHandler
{
private:
    utime_t m_fastCache;
private:
    MessageQueue* m_queue;
    LiveSource* m_source;
    Request* m_req;
    Coroutine* m_trd;
public:
    BufferCache(LiveSource* s, Request* r);
    virtual ~BufferCache();
    virtual error UpdateAuth(LiveSource* s, Request* r);
public:
    virtual error Start();
    virtual error DumpCache(LiveConsumer* consumer, RtmpJitterAlgorithm jitter);
// Interface IEndlessThreadHandler.
public:
    virtual error Cycle();
};

// The encoder to transmux RTMP stream.
class IBufferEncoder
{
public:
    IBufferEncoder();
    virtual ~IBufferEncoder();
public:
    // Initialize the encoder with file writer(to http response) and stream cache.
    // @param w the writer to write to http response.
    // @param c the stream cache for audio stream fast startup.
    virtual error Initialize(FileWriter* w, BufferCache* c) = 0;
    // Write rtmp video/audio/metadata.
    virtual error WriteAudio(int64_t timestamp, char* data, int size) = 0;
    virtual error WriteVideo(int64_t timestamp, char* data, int size) = 0;
    virtual error WriteMetadata(int64_t timestamp, char* data, int size) = 0;
public:
    // For some stream, for example, mp3 and aac, the audio stream,
    // we use large gop cache in encoder, for the gop cache of LiveSource is ignore audio.
    // @return true to use gop cache of encoder; otherwise, use LiveSource.
    virtual bool HasCache() = 0;
    // Dumps the cache of encoder to consumer.
    virtual error DumpCache(LiveConsumer* consumer, RtmpJitterAlgorithm jitter) = 0;
};

// Transmux RTMP to HTTP Live Streaming.
class FlvStreamEncoder : public IBufferEncoder
{
private:
    FlvTransmuxer* m_enc;
    bool m_headerWritten;
public:
    FlvStreamEncoder();
    virtual ~FlvStreamEncoder();
public:
    virtual error Initialize(FileWriter* w, BufferCache* c);
    virtual error WriteAudio(int64_t timestamp, char* data, int size);
    virtual error WriteVideo(int64_t timestamp, char* data, int size);
    virtual error WriteMetadata(int64_t timestamp, char* data, int size);
public:
    virtual bool HasCache();
    virtual error DumpCache(LiveConsumer* consumer, RtmpJitterAlgorithm jitter);
public:
    // Write the tags in a time.
    virtual error WriteTags(SharedPtrMessage** msgs, int count);
private:
    virtual error WriteHeader(bool has_video = true, bool has_audio = true);
};

// Transmux RTMP to HTTP TS Streaming.
class TsStreamEncoder : public IBufferEncoder
{
private:
    TsTransmuxer* m_enc;
public:
    TsStreamEncoder();
    virtual ~TsStreamEncoder();
public:
    virtual error Initialize(FileWriter* w, BufferCache* c);
    virtual error WriteAudio(int64_t timestamp, char* data, int size);
    virtual error WriteVideo(int64_t timestamp, char* data, int size);
    virtual error WriteMetadata(int64_t timestamp, char* data, int size);
public:
    virtual bool HasCache();
    virtual error DumpCache(LiveConsumer* consumer, RtmpJitterAlgorithm jitter);
};

// Transmux RTMP with AAC stream to HTTP AAC Streaming.
class AacStreamEncoder : public IBufferEncoder
{
private:
    AacTransmuxer* m_enc;
    BufferCache* m_cache;
public:
    AacStreamEncoder();
    virtual ~AacStreamEncoder();
public:
    virtual error Initialize(FileWriter* w, BufferCache* c);
    virtual error WriteAudio(int64_t timestamp, char* data, int size);
    virtual error WriteVideo(int64_t timestamp, char* data, int size);
    virtual error WriteMetadata(int64_t timestamp, char* data, int size);
public:
    virtual bool HasCache();
    virtual error DumpCache(LiveConsumer* consumer, RtmpJitterAlgorithm jitter);
};

// Transmux RTMP with MP3 stream to HTTP MP3 Streaming.
class Mp3StreamEncoder : public IBufferEncoder
{
private:
    Mp3Transmuxer* m_enc;
    BufferCache* m_cache;
public:
    Mp3StreamEncoder();
    virtual ~Mp3StreamEncoder();
public:
    virtual error Initialize(FileWriter* w, BufferCache* c);
    virtual error WriteAudio(int64_t timestamp, char* data, int size);
    virtual error WriteVideo(int64_t timestamp, char* data, int size);
    virtual error WriteMetadata(int64_t timestamp, char* data, int size);
public:
    virtual bool HasCache();
    virtual error DumpCache(LiveConsumer* consumer, RtmpJitterAlgorithm jitter);
};

// Write stream to http response direclty.
class BufferWriter : public FileWriter
{
private:
    IHttpResponseWriter* m_writer;
public:
    BufferWriter(IHttpResponseWriter* w);
    virtual ~BufferWriter();
public:
    virtual error Open(std::string file);
    virtual void Close();
public:
    virtual bool IsOpen();
    virtual int64_t Tellg();
public:
    virtual error Write(void* buf, size_t count, ssize_t* pnwrite);
    virtual error Writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
};

// HTTP Live Streaming, to transmux RTMP to HTTP FLV or other format.
// TODO: FIXME: Rename to HttpLive
class LiveStream : public IHttpHandler
{
private:
    Request* m_req;
    LiveSource* m_source;
    BufferCache* m_cache;
public:
    LiveStream(LiveSource* s, Request* r, BufferCache* c);
    virtual ~LiveStream();
    virtual error UpdateAuth(LiveSource* s, Request* r);
public:
    virtual error ServeHttp(IHttpResponseWriter* w, IHttpMessage* r);
private:
    virtual error DoServeHttp(IHttpResponseWriter* w, IHttpMessage* r);
    virtual error HttpHooksOnPlay(IHttpMessage* r);
    virtual void HttpHooksOnStop(IHttpMessage* r);
    virtual error StreamingSendMessages(IBufferEncoder* enc, SharedPtrMessage** msgs, int nb_msgs);
};

// The Live Entry, to handle HTTP Live Streaming.
struct LiveEntry
{
private:
    bool m_isFlv;
    bool m_isTs;
    bool m_isAac;
    bool m_isMp3;
public:
    // We will free the request.
    Request* m_req;
    // Shared source.
    LiveSource* m_source;
public:
    // For template, the mount contains variables.
    // For concrete stream, the mount is url to access.
    std::string m_mount;

    LiveStream* m_stream;
    BufferCache* cache;

    LiveEntry(std::string m);
    virtual ~LiveEntry();

    bool IsFlv();
    bool IsTs();
    bool IsMp3();
    bool IsAac();
};

// The HTTP Live Streaming Server, to serve FLV/TS/MP3/AAC stream.
// TODO: Support multiple stream.
class HttpStreamServer : public IReloadHandler
, public IHttpMatchHijacker
{
private:
    Server* m_server;
public:
    HttpServeMux m_mux;
    // The http live streaming template, to create streams.
    std::map<std::string, LiveEntry*> m_tflvs;
    // The http live streaming streams, crote by template.
    std::map<std::string, LiveEntry*> m_sflvs;
public:
    HttpStreamServer(Server* svr);
    virtual ~HttpStreamServer();
public:
    virtual error Initialize();
public:
    // HTTP flv/ts/mp3/aac stream
    virtual error HttpMount(LiveSource* s, Request* r);
    virtual void HttpUnmount(LiveSource* s, Request* r);
// Interface IHttpMatchHijacker
public:
    virtual error Hijack(IHttpMessage* request, IHttpHandler** ph);
private:
    virtual error InitializeFlvStreaming();
    virtual error InitializeFlvEntry(std::string vhost);
};

#endif // APP_HTTP_STREAM_H

#include "app_http_stream.h"

BufferCache::BufferCache(LiveSource *s, Request *r)
{

}

BufferCache::~BufferCache()
{

}

error BufferCache::UpdateAuth(LiveSource *s, Request *r)
{

}

error BufferCache::Start()
{

}

error BufferCache::DumpCache(LiveConsumer *consumer, RtmpJitterAlgorithm jitter)
{

}

error BufferCache::Cycle()
{

}

IBufferEncoder::IBufferEncoder()
{

}

IBufferEncoder::~IBufferEncoder()
{

}

FlvStreamEncoder::FlvStreamEncoder()
{

}

FlvStreamEncoder::~FlvStreamEncoder()
{

}

error FlvStreamEncoder::Initialize(FileWriter *w, BufferCache *c)
{

}

error FlvStreamEncoder::WriteAudio(int64_t timestamp, char *data, int size)
{

}

error FlvStreamEncoder::WriteVideo(int64_t timestamp, char *data, int size)
{

}

error FlvStreamEncoder::WriteMetadata(int64_t timestamp, char *data, int size)
{

}

bool FlvStreamEncoder::HasCache()
{

}

error FlvStreamEncoder::DumpCache(LiveConsumer *consumer, RtmpJitterAlgorithm jitter)
{

}

error FlvStreamEncoder::WriteTags(SharedPtrMessage **msgs, int count)
{

}

error FlvStreamEncoder::WriteHeader(bool has_video, bool has_audio)
{

}

TsStreamEncoder::TsStreamEncoder()
{

}

TsStreamEncoder::~TsStreamEncoder()
{

}

error TsStreamEncoder::Initialize(FileWriter *w, BufferCache *c)
{

}

error TsStreamEncoder::WriteAudio(int64_t timestamp, char *data, int size)
{

}

error TsStreamEncoder::WriteVideo(int64_t timestamp, char *data, int size)
{

}

error TsStreamEncoder::WriteMetadata(int64_t timestamp, char *data, int size)
{

}

bool TsStreamEncoder::HasCache()
{

}

error TsStreamEncoder::DumpCache(LiveConsumer *consumer, RtmpJitterAlgorithm jitter)
{

}

AacStreamEncoder::AacStreamEncoder()
{

}

AacStreamEncoder::~AacStreamEncoder()
{

}

error AacStreamEncoder::Initialize(FileWriter *w, BufferCache *c)
{

}

error AacStreamEncoder::WriteAudio(int64_t timestamp, char *data, int size)
{

}

error AacStreamEncoder::WriteVideo(int64_t timestamp, char *data, int size)
{

}

error AacStreamEncoder::WriteMetadata(int64_t timestamp, char *data, int size)
{

}

bool AacStreamEncoder::HasCache()
{

}

error AacStreamEncoder::DumpCache(LiveConsumer *consumer, RtmpJitterAlgorithm jitter)
{

}

Mp3StreamEncoder::Mp3StreamEncoder()
{

}

Mp3StreamEncoder::~Mp3StreamEncoder()
{

}

error Mp3StreamEncoder::Initialize(FileWriter *w, BufferCache *c)
{

}

error Mp3StreamEncoder::WriteAudio(int64_t timestamp, char *data, int size)
{

}

error Mp3StreamEncoder::WriteVideo(int64_t timestamp, char *data, int size)
{

}

error Mp3StreamEncoder::WriteMetadata(int64_t timestamp, char *data, int size)
{

}

bool Mp3StreamEncoder::HasCache()
{

}

error Mp3StreamEncoder::DumpCache(LiveConsumer *consumer, RtmpJitterAlgorithm jitter)
{

}

BufferWriter::BufferWriter(IHttpResponseWriter *w)
{

}

BufferWriter::~BufferWriter()
{

}

error BufferWriter::Open(std::string file)
{

}

void BufferWriter::Close()
{

}

bool BufferWriter::IsOpen()
{

}

int64_t BufferWriter::Tellg()
{

}

error BufferWriter::Write(void *buf, size_t count, ssize_t *pnwrite)
{

}

error BufferWriter::Writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{

}

LiveStream::LiveStream(LiveSource *s, Request *r, BufferCache *c)
{

}

LiveStream::~LiveStream()
{

}

error LiveStream::UpdateAuth(LiveSource *s, Request *r)
{

}

error LiveStream::ServeHttp(IHttpResponseWriter *w, IHttpMessage *r)
{

}

error LiveStream::DoServeHttp(IHttpResponseWriter *w, IHttpMessage *r)
{

}

error LiveStream::HttpHooksOnPlay(IHttpMessage *r)
{

}

void LiveStream::HttpHooksOnStop(IHttpMessage *r)
{

}

error LiveStream::StreamingSendMessages(IBufferEncoder *enc, SharedPtrMessage **msgs, int nb_msgs)
{

}

LiveEntry::LiveEntry(std::string m)
{

}

LiveEntry::~LiveEntry()
{

}

bool LiveEntry::IsFlv()
{

}

bool LiveEntry::IsTs()
{

}

bool LiveEntry::IsMp3()
{

}

bool LiveEntry::IsAac()
{

}

HttpStreamServer::HttpStreamServer(Server *svr)
{

}

HttpStreamServer::~HttpStreamServer()
{

}

error HttpStreamServer::Initialize()
{

}

error HttpStreamServer::HttpMount(LiveSource *s, Request *r)
{

}

void HttpStreamServer::HttpUnmount(LiveSource *s, Request *r)
{

}

error HttpStreamServer::Hijack(IHttpMessage *request, IHttpHandler **ph)
{

}

error HttpStreamServer::InitializeFlvStreaming()
{

}

error HttpStreamServer::InitializeFlvEntry(std::string vhost)
{

}

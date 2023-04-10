#include "flv.h"
#include "buffer.h"
#include "codec.h"
#include "error.h"
#include "io.h"
#include "kbps.h"
#include "utility.h"
#include "core_autofree.h"
#include "file.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>

Pps* pps_objs_msgs = NULL;

MessageHeader::MessageHeader()
{
    m_messageType = 0;
    m_payloadLength = 0;
    m_timestampDelta = 0;
    m_streamId = 0;

    m_timestamp = 0;
    // we always use the connection chunk-id
    m_perferCid = RTMP_CID_OverConnection;
}

MessageHeader::~MessageHeader()
{

}

bool MessageHeader::IsAudio()
{
    return m_messageType == RTMP_MSG_AudioMessage;
}

bool MessageHeader::IsVideo()
{
    return m_messageType == RTMP_MSG_VideoMessage;
}

bool MessageHeader::IsAmf0Command()
{
    return m_messageType == RTMP_MSG_AMF0CommandMessage;
}

bool MessageHeader::IsAmf0Data()
{
    return m_messageType == RTMP_MSG_AMF0DataMessage;
}

bool MessageHeader::IsAmf3Command()
{
    return m_messageType == RTMP_MSG_AMF3CommandMessage;
}

bool MessageHeader::IsAmf3Data()
{
    return m_messageType == RTMP_MSG_AMF3DataMessage;
}

bool MessageHeader::IsWindowAckledgementSize()
{
    return m_messageType == RTMP_MSG_WindowAcknowledgementSize;
}

bool MessageHeader::IsAckledgement()
{
    return m_messageType == RTMP_MSG_Acknowledgement;
}

bool MessageHeader::IsSetChunkSize()
{
    return m_messageType == RTMP_MSG_SetChunkSize;
}

bool MessageHeader::IsUserControlMessage()
{
    return m_messageType == RTMP_MSG_UserControlMessage;
}

bool MessageHeader::IsSetPeerBandwidth()
{
    return m_messageType == RTMP_MSG_SetPeerBandwidth;
}

bool MessageHeader::IsAggregate()
{
    return m_messageType == RTMP_MSG_AggregateMessage;
}

void MessageHeader::InitializeAmf0Script(int size, int stream)
{
    m_messageType = RTMP_MSG_AMF0DataMessage;
    m_payloadLength = (int32_t)size;
    m_timestampDelta = (int32_t)0;
    m_timestamp = (int64_t)0;
    m_streamId = (int32_t)stream;

    // amf0 script use connection2 chunk-id
    m_perferCid = RTMP_CID_OverConnection2;
}

void MessageHeader::InitializeAudio(int size, uint32_t time, int stream)
{
    m_messageType = RTMP_MSG_AudioMessage;
    m_payloadLength = (int32_t)size;
    m_timestampDelta = (int32_t)time;
    m_timestamp = (int64_t)time;
    m_streamId = (int32_t)stream;

    // audio chunk-id
    m_perferCid = RTMP_CID_Audio;
}

void MessageHeader::InitializeVideo(int size, uint32_t time, int stream)
{
    m_messageType = RTMP_MSG_VideoMessage;
    m_payloadLength = (int32_t)size;
    m_timestampDelta = (int32_t)time;
    m_timestamp = (int64_t)time;
    m_streamId = (int32_t)stream;

    // video chunk-id
    m_perferCid = RTMP_CID_Video;
}

CommonMessage::CommonMessage()
{
    m_payload = NULL;
    m_size = 0;
}

CommonMessage::~CommonMessage()
{
    Freepa(m_payload);
}

void CommonMessage::CreatePayload(int size)
{
    Freepa(m_payload);

    m_payload = new char[size];
    verbose("create payload for RTMP message. size=%d", size);
}

error CommonMessage::Create(MessageHeader *pheader, char *body, int size)
{
    // drop previous payload.
    Freepa(m_payload);

    this->m_header = *pheader;
    this->m_payload = body;
    this->m_size = size;

    return SUCCESS;
}

SharedMessageHeader::SharedMessageHeader()
{
    m_payloadLength = 0;
    m_messageType = 0;
    m_perferCid = 0;
}

SharedMessageHeader::~SharedMessageHeader()
{

}

SharedPtrMessage::SharedPtrPayload::SharedPtrPayload()
{
    m_payload = NULL;
    m_size = 0;
    m_sharedCount = 0;
}

SharedPtrMessage::SharedPtrPayload::~SharedPtrPayload()
{
    Freepa(m_payload);
}

SharedPtrMessage::SharedPtrMessage() : m_timestamp(0), m_streamId(0), m_size(0), m_payload(nullptr)
{
    m_ptr = nullptr;

    ++ pps_objs_msgs->m_sugar;
}

SharedPtrMessage::~SharedPtrMessage()
{
    if (m_ptr) {
        if (m_ptr->m_sharedCount == 0) {
            Freep(m_ptr);
        } else {
            m_ptr->m_sharedCount--;
        }
    }
}

error SharedPtrMessage::Create(CommonMessage *msg)
{
    error err = SUCCESS;

    if ((err = Create(&msg->m_header, msg->m_payload, msg->m_size)) != SUCCESS) {
        return ERRORWRAP(err, "create message");
    }

    // to prevent double free of payload:
    // initialize already attach the payload of msg,
    // detach the payload to transfer the owner to shared ptr.
    msg->m_payload = NULL;
    msg->m_size = 0;

    return err;
}

error SharedPtrMessage::Create(MessageHeader *pheader, char *payload, int size)
{
    error err = SUCCESS;

    if (size < 0) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_CREATE, "create message size=%d", size);
    }

    Assert(!m_ptr);
    m_ptr = new SharedPtrPayload();

    // direct attach the data.
    if (pheader) {
        m_ptr->m_header.m_messageType = pheader->m_messageType;
        m_ptr->m_header.m_payloadLength = size;
        m_ptr->m_header.m_perferCid = pheader->m_perferCid;
        this->m_timestamp = pheader->m_timestamp;
        this->m_streamId = pheader->m_streamId;
    }
    m_ptr->m_payload = payload;
    m_ptr->m_size = size;

    // message can access it.
    this->m_payload = m_ptr->m_payload;
    this->m_size = m_ptr->m_size;

    return err;
}

void SharedPtrMessage::Wrap(char *payload, int size)
{
    Assert(!m_ptr);
    m_ptr = new SharedPtrPayload();

    m_ptr->m_payload = payload;
    m_ptr->m_size = size;

    this->m_payload = m_ptr->m_payload;
    this->m_size = m_ptr->m_size;
}

int SharedPtrMessage::Count()
{
    return m_ptr? m_ptr->m_sharedCount : 0;
}

bool SharedPtrMessage::Check(int stream_id)
{
    // Ignore error when message has no payload.
    if (!m_ptr) {
        return true;
    }

    // we donot use the complex basic header,
    // ensure the basic header is 1bytes.
    if (m_ptr->m_header.m_perferCid < 2 || m_ptr->m_header.m_perferCid > 63) {
        info("change the chunk_id=%d to default=%d", m_ptr->m_header.m_perferCid, RTMP_CID_ProtocolControl);
        m_ptr->m_header.m_perferCid = RTMP_CID_ProtocolControl;
    }

    // we assume that the stream_id in a group must be the same.
    if (this->m_streamId == stream_id) {
        return true;
    }
    this->m_streamId = stream_id;

    return false;
}

bool SharedPtrMessage::IsAv()
{
    return m_ptr->m_header.m_messageType == RTMP_MSG_AudioMessage
            || m_ptr->m_header.m_messageType == RTMP_MSG_VideoMessage;
}

bool SharedPtrMessage::IsAudio()
{
    return m_ptr->m_header.m_messageType == RTMP_MSG_AudioMessage;
}

bool SharedPtrMessage::IsVideo()
{
    return m_ptr->m_header.m_messageType == RTMP_MSG_VideoMessage;
}

int SharedPtrMessage::ChunkHeader(char *cache, int nb_cache, bool c0)
{
    if (c0) {
        return ChunkHeaderC0(m_ptr->m_header.m_perferCid, (uint32_t)m_timestamp,
            m_ptr->m_header.m_payloadLength, m_ptr->m_header.m_messageType, m_streamId, cache, nb_cache);
    } else {
        return ChunkHeaderC3(m_ptr->m_header.m_perferCid, (uint32_t)m_timestamp,
            cache, nb_cache);
    }
}

SharedPtrMessage *SharedPtrMessage::Copy()
{
    Assert(m_ptr);

    SharedPtrMessage* copy = Copy2();

    copy->m_timestamp = m_timestamp;
    copy->m_streamId = m_streamId;

    return copy;
}

SharedPtrMessage *SharedPtrMessage::Copy2()
{
    SharedPtrMessage* copy = new SharedPtrMessage();

    // We got an object from cache, the ptr might exists, so unwrap it.
    //srs_assert(!copy->ptr);

    // Reference to this message instead.
    copy->m_ptr = m_ptr;
    m_ptr->m_sharedCount++;

    copy->m_payload = m_ptr->m_payload;
    copy->m_size = m_ptr->m_size;

    return copy;
}

FlvTransmuxer::FlvTransmuxer()
{
    m_writer = NULL;

    m_nbTagHeaders = 0;
    m_tagHeaders = nullptr;
    m_nbIovssCache = 0;
    m_iovssCache = nullptr;
    m_nbPpts = 0;
    m_ppts = nullptr;
}

FlvTransmuxer::~FlvTransmuxer()
{
    Freepa(m_tagHeaders);
    Freepa(m_iovssCache);
    Freepa(m_ppts);
}

error FlvTransmuxer::Initialize(IWriter *fw)
{
    Assert(fw);
    m_writer = fw;
    return SUCCESS;
}

error FlvTransmuxer::WriteHeader(bool has_video, bool has_audio)
{
    error err = SUCCESS;

    uint8_t av_flag = 0;
    av_flag += (has_audio? 4:0);
    av_flag += (has_video? 1:0);

    // 9bytes header and 4bytes first previous-tag-size
    char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)av_flag, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };

    // flv specification should set the audio and video flag,
    // actually in practise, application generally ignore this flag,
    // so we generally set the audio/video to 0.

    // write 9bytes header.
    if ((err = WriteHeader(flv_header)) != SUCCESS) {
        return ERRORWRAP(err, "write header");
    }

    return err;
}

error FlvTransmuxer::WriteHeader(char flv_header[])
{
    error err = SUCCESS;

    // write data.
    if ((err = m_writer->Write(flv_header, 9, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "write flv header failed");
    }

    // previous tag size.
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    if ((err = m_writer->Write(pts, 4, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "write pts");
    }

    return err;
}

error FlvTransmuxer::WriteMetadata(char type, char *data, int size)
{
    error err = SUCCESS;

    if (size > 0) {
        CacheMetadata(type, data, size, m_tagHeader);
    }

    if ((err = WriteTag(m_tagHeader, sizeof(m_tagHeader), data, size)) != SUCCESS) {
        return ERRORWRAP(err, "write tag");
    }

    return err;
}

error FlvTransmuxer::WriteAudio(int64_t timestamp, char *data, int size)
{
    error err = SUCCESS;

    if (size > 0) {
        CacheAudio(timestamp, data, size, m_tagHeader);
    }

    if ((err = WriteTag(m_tagHeader, sizeof(m_tagHeader), data, size)) != SUCCESS) {
        return ERRORWRAP(err, "write tag");
    }

    return err;
}

error FlvTransmuxer::WriteVideo(int64_t timestamp, char *data, int size)
{
    error err = SUCCESS;

    if (size > 0) {
        CacheVideo(timestamp, data, size, m_tagHeader);
    }

    if ((err = WriteTag(m_tagHeader, sizeof(m_tagHeader), data, size)) != SUCCESS) {
        return ERRORWRAP(err, "write flv video tag failed");
    }

    return err;
}

int FlvTransmuxer::SizeTag(int data_size)
{
    Assert(data_size >= 0);
    return FLV_TAG_HEADER_SIZE + data_size + FLV_PREVIOUS_TAG_SIZE;
}

error FlvTransmuxer::WriteTags(SharedPtrMessage **msgs, int count)
{
    error err = SUCCESS;

    // realloc the iovss.
    int nb_iovss = 3 * count;
    iovec* iovss = m_iovssCache;
    if (m_nbIovssCache < nb_iovss) {
        Freepa(m_iovssCache);

        m_nbIovssCache = nb_iovss;
        iovss = m_iovssCache = new iovec[nb_iovss];
    }

    // realloc the tag headers.
    char* cache = m_tagHeaders;
    if (m_nbTagHeaders < count) {
        Freepa(m_tagHeaders);

        m_nbTagHeaders = count;
        cache = m_tagHeaders = new char[FLV_TAG_HEADER_SIZE * count];
    }

    // realloc the pts.
    char* pts = m_ppts;
    if (m_nbPpts < count) {
        Freepa(m_ppts);

        m_nbPpts = count;
        pts = m_ppts = new char[FLV_PREVIOUS_TAG_SIZE * count];
    }

    // the cache is ok, write each messages.
    iovec* iovs = iovss;
    for (int i = 0; i < count; i++) {
        SharedPtrMessage* msg = msgs[i];

        // cache all flv header.
        if (msg->IsAudio()) {
            CacheAudio(msg->m_timestamp, msg->m_payload, msg->m_size, cache);
        } else if (msg->IsVideo()) {
            CacheVideo(msg->m_timestamp, msg->m_payload, msg->m_size, cache);
        } else {
            CacheMetadata(FrameTypeScript, msg->m_payload, msg->m_size, cache);
        }

        // cache all pts.
        CachePts(FLV_TAG_HEADER_SIZE + msg->m_size, pts);

        // all ioves.
        iovs[0].iov_base = cache;
        iovs[0].iov_len = FLV_TAG_HEADER_SIZE;
        iovs[1].iov_base = msg->m_payload;
        iovs[1].iov_len = msg->m_size;
        iovs[2].iov_base = pts;
        iovs[2].iov_len = FLV_PREVIOUS_TAG_SIZE;

        // move next.
        cache += FLV_TAG_HEADER_SIZE;
        pts += FLV_PREVIOUS_TAG_SIZE;
        iovs += 3;
    }

    if ((err = m_writer->Writev(iovss, nb_iovss, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "write flv tags failed");
    }

    return err;
}

void FlvTransmuxer::CacheMetadata(char type, char *data, int size, char *cache)
{
    Assert(data);

    // 11 bytes tag header
    /*char tag_header[] = {
     (char)type, // TagType UB [5], 18 = script data
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/

    Buffer* tag_stream = new Buffer(cache, 11);
    AutoFree(Buffer, tag_stream);

    // write data size.
    tag_stream->Write1Bytes(type);
    tag_stream->Write3Bytes(size);
    tag_stream->Write3Bytes(0x00);
    tag_stream->Write1Bytes(0x00);
    tag_stream->Write3Bytes(0x00);
}

void FlvTransmuxer::CacheAudio(int64_t timestamp, char *data, int size, char *cache)
{
    Assert(data);

    timestamp &= 0x7fffffff;

    // 11bytes tag header
    /*char tag_header[] = {
     (char)SrsFrameTypeAudio, // TagType UB [5], 8 = audio
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/

    Buffer* tag_stream = new Buffer(cache, 11);
    AutoFree(Buffer, tag_stream);

    // write data size.
    tag_stream->Write1Bytes(FrameTypeAudio);
    tag_stream->Write3Bytes(size);
    tag_stream->Write3Bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->Write1Bytes((timestamp >> 24) & 0xFF);
    tag_stream->Write3Bytes(0x00);
}

void FlvTransmuxer::CacheVideo(int64_t timestamp, char *data, int size, char *cache)
{
    Assert(data);

    timestamp &= 0x7fffffff;

    // 11bytes tag header
    /*char tag_header[] = {
     (char)SrsFrameTypeVideo, // TagType UB [5], 9 = video
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/

    Buffer* tag_stream = new Buffer(cache, 11);
    AutoFree(Buffer, tag_stream);

    // write data size.
    tag_stream->Write1Bytes(FrameTypeVideo);
    tag_stream->Write3Bytes(size);
    tag_stream->Write3Bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->Write1Bytes((timestamp >> 24) & 0xFF);
    tag_stream->Write3Bytes(0x00);
}

void FlvTransmuxer::CachePts(int size, char *cache)
{
    Buffer* tag_stream = new Buffer(cache, 11);
    AutoFree(Buffer, tag_stream);
    tag_stream->Write4Bytes(size);
}

error FlvTransmuxer::WriteTag(char *header, int header_size, char *tag, int tag_size)
{
    error err = SUCCESS;

    // PreviousTagSizeN UI32 Size of last tag, including its header, in bytes.
    char pre_size[FLV_PREVIOUS_TAG_SIZE];
    CachePts(tag_size + header_size, pre_size);

    iovec iovs[3];
    iovs[0].iov_base = header;
    iovs[0].iov_len = header_size;
    iovs[1].iov_base = tag;
    iovs[1].iov_len = tag_size;
    iovs[2].iov_base = pre_size;
    iovs[2].iov_len = FLV_PREVIOUS_TAG_SIZE;

    if ((err = m_writer->Writev(iovs, 3, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "write flv tag failed");
    }

    return err;
}

FlvDecoder::FlvDecoder()
{
    m_reader = nullptr;
}

FlvDecoder::~FlvDecoder()
{

}

error FlvDecoder::Initialize(IReader *fr)
{
    Assert(fr);
    m_reader = fr;
    return SUCCESS;
}

error FlvDecoder::ReadHeader(char header[])
{
    error err = SUCCESS;

    Assert(header);

    // TODO: FIXME: Should use readfully.
    if ((err = m_reader->Read(header, 9, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "read header");
    }

    char* h = header;
    if (h[0] != 'F' || h[1] != 'L' || h[2] != 'V') {
        return ERRORNEW(ERROR_KERNEL_FLV_HEADER, "flv header must start with FLV");
    }

    return err;
}

error FlvDecoder::ReadTagHeader(char *ptype, int32_t *pdata_size, uint32_t *ptime)
{
    error err = SUCCESS;

    Assert(ptype);
    Assert(pdata_size);
    Assert(ptime);

    char th[11]; // tag header

    // read tag header
    // TODO: FIXME: Should use readfully.
    if ((err = m_reader->Read(th, 11, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "read flv tag header failed");
    }

    // Reserved UB [2]
    // Filter UB [1]
    // TagType UB [5]
    *ptype = (th[0] & 0x1F);

    // DataSize UI24
    char* pp = (char*)pdata_size;
    pp[3] = 0;
    pp[2] = th[1];
    pp[1] = th[2];
    pp[0] = th[3];

    // Timestamp UI24
    pp = (char*)ptime;
    pp[2] = th[4];
    pp[1] = th[5];
    pp[0] = th[6];

    // TimestampExtended UI8
    pp[3] = th[7];

    return err;
}

error FlvDecoder::ReadTagData(char *data, int32_t size)
{
    error err = SUCCESS;

    Assert(data);

    // TODO: FIXME: Should use readfully.
    if ((err = m_reader->Read(data, size, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "read flv tag header failed");
    }

    return err;
}

error FlvDecoder::ReadPreviousTagSize(char previous_tag_size[])
{
    error err = SUCCESS;

    Assert(previous_tag_size);

    // ignore 4bytes tag size.
    // TODO: FIXME: Should use readfully.
    if ((err = m_reader->Read(previous_tag_size, 4, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "read flv previous tag size failed");
    }

    return err;
}

FlvVodStreamDecoder::FlvVodStreamDecoder()
{
    m_reader = nullptr;
}

FlvVodStreamDecoder::~FlvVodStreamDecoder()
{

}

error FlvVodStreamDecoder::Initialize(IReader *fr)
{
    error err = SUCCESS;

    Assert(fr);
    m_reader = dynamic_cast<FileReader*>(fr);
    if (!m_reader) {
        return ERRORNEW(ERROR_EXPECT_FILE_IO, "stream is not file io");
    }

    if (!m_reader->IsOpen()) {
        return ERRORNEW(ERROR_KERNEL_FLV_STREAM_CLOSED, "stream is not open for decoder");
    }

    return err;
}

error FlvVodStreamDecoder::ReadHeaderExt(char header[])
{
    error err = SUCCESS;

    Assert(header);

    // @remark, always false, for sizeof(char[13]) equals to sizeof(char*)
    //srs_assert(13 == sizeof(header));

    // 9bytes header and 4bytes first previous-tag-size
    int size = 13;

    if ((err = m_reader->Read(header, size, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "read header");
    }

    return err;
}

error FlvVodStreamDecoder::ReadSequenceHeaderSummary(int64_t *pstart, int *psize)
{
    error err = SUCCESS;

    Assert(pstart);
    Assert(psize);

    // simply, the first video/audio must be the sequence header.
    // and must be a sequence video and audio.

    // 11bytes tag header
    char tag_header[] = {
        (char)0x00, // TagType UB [5], 9 = video, 8 = audio, 18 = script data
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };

    // discovery the sequence header video and audio.
    // @remark, maybe no video or no audio.
    bool got_video = false;
    bool got_audio = false;
    // audio/video sequence and data offset.
    int64_t av_sequence_offset_start = -1;
    int64_t av_sequence_offset_end = -1;
    for (;;) {
        if ((err = m_reader->Read(tag_header, FLV_TAG_HEADER_SIZE, NULL)) != SUCCESS) {
            return ERRORWRAP(err, "read tag header");
        }

        Buffer* tag_stream = new Buffer(tag_header, FLV_TAG_HEADER_SIZE);
        AutoFree(Buffer, tag_stream);

        int8_t tag_type = tag_stream->Read1Bytes();
        int32_t data_size = tag_stream->Read3Bytes();

        bool is_video = tag_type == 0x09;
        bool is_audio = tag_type == 0x08;
        bool is_not_av = !is_video && !is_audio;
        if (is_not_av) {
            // skip body and tag size.
            m_reader->Skip(data_size + FLV_PREVIOUS_TAG_SIZE);
            continue;
        }

        // if video duplicated, no audio
        if (is_video && got_video) {
            break;
        }
        // if audio duplicated, no video
        if (is_audio && got_audio) {
            break;
        }

        // video
        if (is_video) {
            Assert(!got_video);
            got_video = true;

            if (av_sequence_offset_start < 0) {
                av_sequence_offset_start = m_reader->Tellg() - FLV_TAG_HEADER_SIZE;
            }
            av_sequence_offset_end = m_reader->Tellg() + data_size + FLV_PREVIOUS_TAG_SIZE;
            m_reader->Skip(data_size + FLV_PREVIOUS_TAG_SIZE);
        }

        // audio
        if (is_audio) {
            Assert(!got_audio);
            got_audio = true;

            if (av_sequence_offset_start < 0) {
                av_sequence_offset_start = m_reader->Tellg() - FLV_TAG_HEADER_SIZE;
            }
            av_sequence_offset_end = m_reader->Tellg() + data_size + FLV_PREVIOUS_TAG_SIZE;
            m_reader->Skip(data_size + FLV_PREVIOUS_TAG_SIZE);
        }

        if (got_audio && got_video) {
            break;
        }
    }

    // seek to the sequence header start offset.
    if (av_sequence_offset_start > 0) {
        m_reader->Seek2(av_sequence_offset_start);
        *pstart = av_sequence_offset_start;
        *psize = (int)(av_sequence_offset_end - av_sequence_offset_start);
    }

    return err;
}

error FlvVodStreamDecoder::Seek2(int64_t offset)
{
    error err = SUCCESS;

    if (offset >= m_reader->Filesize()) {
        return ERRORNEW(ERROR_SYSTEM_FILE_EOF, "flv fast decoder seek overflow file, size=%d, offset=%d", (int)m_reader->Filesize(), (int)offset);
    }

    if (m_reader->Seek2(offset) < 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_SEEK, "flv fast decoder seek error, size=%d, offset=%d", (int)m_reader->Filesize(), (int)offset);
    }

    return err;
}

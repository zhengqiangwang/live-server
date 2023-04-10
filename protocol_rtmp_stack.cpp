#include "protocol_rtmp_stack.h"
#include "buffer.h"
#include "core_autofree.h"
#include "consts.h"
#include "protocol_amf0.h"
#include "protocol_io.h"
#include "protocol_rtmp_handshak.h"
#include "protocol_stream.h"
#include "protocol_utility.h"
#include "utility.h"
#include "error.h"

#include "inttypes.h"
#include <cstring>

#define PERF_MERGED_READ
#define PERF_COMPLEX_SEND

#ifndef _WIN32
#include <unistd.h>
#endif


// FMLE
#define RTMP_AMF0_COMMAND_ON_FC_PUBLISH         "onFCPublish"
#define RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH       "onFCUnpublish"

// default stream id for response the createStream request.
#define DEFAULT_SID                         1

// when got a messae header, there must be some data,
// increase recv timeout to got an entire message.
#define MIN_RECV_TIMEOUT_US (int64_t)(60*1000*1000LL)

/****************************************************************************
 *****************************************************************************
 ****************************************************************************/
/**
 * 6.1.2. Chunk Message Header
 * There are four different formats for the chunk message header,
 * selected by the "fmt" field in the chunk basic header.
 */
// 6.1.2.1. Type 0
// Chunks of Type 0 are 11 bytes long. This type MUST be used at the
// start of a chunk stream, and whenever the stream timestamp goes
// backward (e.g., because of a backward seek).
#define RTMP_FMT_TYPE0                          0
// 6.1.2.2. Type 1
// Chunks of Type 1 are 7 bytes long. The message stream ID is not
// included; this chunk takes the same stream ID as the preceding chunk.
// Streams with variable-sized messages (for example, many video
// formats) SHOULD use this format for the first chunk of each new
// message after the first.
#define RTMP_FMT_TYPE1                          1
// 6.1.2.3. Type 2
// Chunks of Type 2 are 3 bytes long. Neither the stream ID nor the
// message length is included; this chunk has the same stream ID and
// message length as the preceding chunk. Streams with constant-sized
// messages (for example, some audio and data formats) SHOULD use this
// format for the first chunk of each message after the first.
#define RTMP_FMT_TYPE2                          2
// 6.1.2.4. Type 3
// Chunks of Type 3 have no header. Stream ID, message length and
// timestamp delta are not present; chunks of this type take values from
// the preceding chunk. When a single message is split into chunks, all
// chunks of a message except the first one, SHOULD use this type. Refer
// to example 2 in section 6.2.2. Stream consisting of messages of
// exactly the same size, stream ID and spacing in time SHOULD use this
// type for all chunks after chunk of Type 2. Refer to example 1 in
// section 6.2.1. If the delta between the first message and the second
// message is same as the time stamp of first message, then chunk of
// type 3 would immediately follow the chunk of type 0 as there is no
// need for a chunk of type 2 to register the delta. If Type 3 chunk
// follows a Type 0 chunk, then timestamp delta for this Type 3 chunk is
// the same as the timestamp of Type 0 chunk.
#define RTMP_FMT_TYPE3                          3

/****************************************************************************
 *****************************************************************************
 ****************************************************************************/

Packet::Packet()
{

}

Packet::~Packet()
{

}

error Packet::ToMsg(CommonMessage *msg, int stream_id)
{
    error err = SUCCESS;

    int size = 0;
    char* payload = NULL;
    if ((err = Encode(size, payload)) != SUCCESS) {
        return ERRORWRAP(err, "encode packet");
    }

    // encode packet to payload and size.
    if (size <= 0 || payload == NULL) {
        warn("packet is empty, ignore empty message.");
        return err;
    }

    // to message
    MessageHeader header;
    header.m_payloadLength = size;
    header.m_messageType = GetMessageType();
    header.m_streamId = stream_id;
    header.m_perferCid = GetPreferCid();

    if ((err = msg->Create(&header, payload, size)) != SUCCESS) {
        return ERRORWRAP(err, "create %dB message", size);
    }

    return err;
}

error Packet::Encode(int &psize, char *&ppayload)
{
    error err = SUCCESS;

    int size = GetSize();
    char* payload = NULL;

    if (size > 0) {
        payload = new char[size];

        Buffer* stream = new Buffer(payload, size);
        AutoFree(Buffer, stream);

        if ((err = EncodePacket(stream)) != SUCCESS) {
            Freepa(payload);
            return ERRORWRAP(err, "encode packet");
        }
    }

    psize = size;
    ppayload = payload;

    return err;
}

error Packet::Decode(Buffer *stream)
{
    return ERRORNEW(ERROR_SYSTEM_PACKET_INVALID, "decode");
}

int Packet::GetPreferCid()
{
    return 0;
}

int Packet::GetMessageType()
{
    return 0;
}

int Packet::GetSize()
{
    return 0;
}

error Packet::EncodePacket(Buffer *stream)
{
    return ERRORNEW(ERROR_SYSTEM_PACKET_INVALID, "encode");
}

Protocol::AckWindowSize::AckWindowSize()
{
    m_window = 0;
    m_sequenceNumber = 0;
    m_nbRecvBytes = 0;
}

Protocol::Protocol(IProtocolReadWriter *io)
{
    m_inBuffer = new FastStream();
    m_skt = io;

    m_inChunkSize = CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
    m_outChunkSize = CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;

    m_nbOutIovs = 8 * CONSTS_IOVS_MAX;
    m_outIovs = (iovec*)malloc(sizeof(iovec) * m_nbOutIovs);
    // each chunk consumers atleast 2 iovs
    Assert(m_nbOutIovs >= 2);

    m_warnedC0c3CacheDry = false;
    m_autoResponseWhenRecv = true;
    m_showDebugInfo = true;
    m_inBufferLength = 0;

    m_csCache = NULL;
    if (PERF_CHUNK_STREAM_CACHE > 0) {
        m_csCache = new ChunkStream*[PERF_CHUNK_STREAM_CACHE];
    }
    for (int cid = 0; cid < PERF_CHUNK_STREAM_CACHE; cid++) {
        ChunkStream* cs = new ChunkStream(cid);
        // set the perfer cid of chunk,
        // which will copy to the message received.
        cs->m_header.m_perferCid = cid;

        m_csCache[cid] = cs;
    }

    m_outC0c3Caches = new char[CONSTS_C0C3_HEADERS_MAX];
}

Protocol::~Protocol()
{
    if (true) {
        std::map<int, ChunkStream*>::iterator it;

        for (it = m_chunkStreams.begin(); it != m_chunkStreams.end(); ++it) {
            ChunkStream* stream = it->second;
            Freep(stream);
        }

        m_chunkStreams.clear();
    }

    if (true) {
        std::vector<Packet*>::iterator it;
        for (it = m_manualResponseQueue.begin(); it != m_manualResponseQueue.end(); ++it) {
            Packet* pkt = *it;
            Freep(pkt);
        }
        m_manualResponseQueue.clear();
    }

    Freep(m_inBuffer);

    // alloc by malloc, use free directly.
    if (m_outIovs) {
        free(m_outIovs);
        m_outIovs = NULL;
    }

    // free all chunk stream cache.
    for (int i = 0; i < PERF_CHUNK_STREAM_CACHE; i++) {
        ChunkStream* cs = m_csCache[i];
        Freep(cs);
    }
    Freepa(m_csCache);

    Freepa(m_outC0c3Caches);
}

void Protocol::SetAutoResponse(bool v)
{
    m_autoResponseWhenRecv = v;
}

error Protocol::ManualResponseFlush()
{
    error err = SUCCESS;

    if (m_manualResponseQueue.empty()) {
        return err;
    }

    std::vector<Packet*>::iterator it;
    for (it = m_manualResponseQueue.begin(); it != m_manualResponseQueue.end();) {
        Packet* pkt = *it;

        // erase this packet, the send api always free it.
        it = m_manualResponseQueue.erase(it);

        // use underlayer api to send, donot flush again.
        if ((err = DoSendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send packet");
        }
    }

    return err;
}

#ifdef PERF_MERGED_READ
void Protocol::SetMergeRead(bool v, IMergeReadHandler* handler)
{
    m_inBuffer->SetMergeRead(v, handler);
}

void Protocol::SetRecvBuffer(int buffer_size)
{
    m_inBuffer->SetBuffer(buffer_size);
}
#endif

void Protocol::SetRecvTimeout(utime_t tm)
{
    return m_skt->SetRecvTimeout(tm);
}

utime_t Protocol::GetRecvTimeout()
{
    return m_skt->GetRecvTimeout();
}

void Protocol::SetSendTimeout(utime_t tm)
{
    return m_skt->SetSendTimeout(tm);
}

utime_t Protocol::GetSendTimeout()
{
    return m_skt->GetSendTimeout();
}

int64_t Protocol::GetRecvBytes()
{
    return m_skt->GetRecvBytes();
}

int64_t Protocol::GetSendBytes()
{
    return m_skt->GetSendBytes();
}

error Protocol::SetInWindowAckSize(int ack_size)
{
    m_inAckSize.m_window = ack_size;
    return SUCCESS;
}

error Protocol::RecvMessage(CommonMessage **pmsg)
{
    *pmsg = NULL;

    error err = SUCCESS;

    while (true) {
        CommonMessage* msg = NULL;

        if ((err = RecvInterlacedMessage(&msg)) != SUCCESS) {
            Freep(msg);
            return ERRORWRAP(err, "recv interlaced message");
        }

        if (!msg) {
            continue;
        }

        if (msg->m_size <= 0 || msg->m_header.m_payloadLength <= 0) {
            trace("ignore empty message(type=%d, size=%d, time=%" PRId64 ", sid=%d).",
                      msg->m_header.m_messageType, msg->m_header.m_payloadLength,
                      msg->m_header.m_timestamp, msg->m_header.m_streamId);
            Freep(msg);
            continue;
        }

        if ((err = OnRecvMessage(msg)) != SUCCESS) {
            Freep(msg);
            return ERRORWRAP(err, "on recv message");
        }

        *pmsg = msg;
        break;
    }

    return err;
}

error Protocol::DecodeMessage(CommonMessage *msg, Packet **ppacket)
{
    *ppacket = NULL;

    error err = SUCCESS;

    Assert(msg != NULL);
    Assert(msg->m_payload != NULL);
    Assert(msg->m_size > 0);

    Buffer stream(msg->m_payload, msg->m_size);

    // decode the packet.
    Packet* packet = NULL;
    if ((err = DoDecodeMessage(msg->m_header, &stream, &packet)) != SUCCESS) {
        Freep(packet);
        return ERRORWRAP(err, "decode message");
    }

    // set to output ppacket only when success.
    *ppacket = packet;

    return err;
}

error Protocol::SendAndFreeMessage(SharedPtrMessage *msg, int stream_id)
{
    return SendAndFreeMessages(&msg, 1, stream_id);
}

error Protocol::SendAndFreeMessages(SharedPtrMessage **msgs, int nb_msgs, int stream_id)
{
    // always not NULL msg.
    Assert(msgs);
    Assert(nb_msgs > 0);

    // update the stream id in header.
    for (int i = 0; i < nb_msgs; i++) {
        SharedPtrMessage* msg = msgs[i];

        if (!msg) {
            continue;
        }

        // check perfer cid and stream,
        // when one msg stream id is ok, ignore left.
        if (msg->Check(stream_id)) {
            break;
        }
    }

    // donot use the auto free to free the msg,
    // for performance issue.
    error err = DoSendMessages(msgs, nb_msgs);

    for (int i = 0; i < nb_msgs; i++) {
        SharedPtrMessage* msg = msgs[i];
        Freep(msg);
    }

    // donot flush when send failed
    if (err != SUCCESS) {
        return ERRORWRAP(err, "send messages");
    }

    // flush messages in manual queue
    if ((err = ManualResponseFlush()) != SUCCESS) {
        return ERRORWRAP(err, "manual flush response");
    }

    PrintDebugInfo();

    return err;
}

error Protocol::SendAndFreePacket(Packet *packet, int stream_id)
{
    error err = SUCCESS;

    if ((err = DoSendAndFreePacket(packet, stream_id)) != SUCCESS) {
        return ERRORWRAP(err, "send packet");
    }

    // flush messages in manual queue
    if ((err = ManualResponseFlush()) != SUCCESS) {
        return ERRORWRAP(err, "manual flush response");
    }

    return err;
}

error Protocol::DoSendMessages(SharedPtrMessage **msgs, int nb_msgs)
{
    error err = SUCCESS;

#ifdef PERF_COMPLEX_SEND
    int iov_index = 0;
    iovec* iovs = m_outIovs + iov_index;

    int c0c3_cache_index = 0;
    char* c0c3_cache = m_outC0c3Caches + c0c3_cache_index;

    // try to send use the c0c3 header cache,
    // if cache is consumed, try another loop.
    for (int i = 0; i < nb_msgs; i++) {
        SharedPtrMessage* msg = msgs[i];

        if (!msg) {
            continue;
        }

        // ignore empty message.
        if (!msg->m_payload || msg->m_size <= 0) {
            continue;
        }

        // p set to current write position,
        // it's ok when payload is NULL and size is 0.
        char* p = msg->m_payload;
        char* pend = msg->m_payload + msg->m_size;

        // always write the header event payload is empty.
        while (p < pend) {
            // always has header
            int nb_cache = CONSTS_C0C3_HEADERS_MAX - c0c3_cache_index;
            int nbh = msg->ChunkHeader(c0c3_cache, nb_cache, p == msg->m_payload);
            Assert(nbh > 0);

            // header iov
            iovs[0].iov_base = c0c3_cache;
            iovs[0].iov_len = nbh;

            // payload iov
            int payload_size = MIN(m_outChunkSize, (int)(pend - p));
            iovs[1].iov_base = p;
            iovs[1].iov_len = payload_size;

            // consume sendout bytes.
            p += payload_size;

            // realloc the iovs if exceed,
            // for we donot know how many messges maybe to send entirely,
            // we just alloc the iovs, it's ok.
            if (iov_index >= m_nbOutIovs - 2) {
                int ov = m_nbOutIovs;
                m_nbOutIovs = 2 * m_nbOutIovs;
                int realloc_size = sizeof(iovec) * m_nbOutIovs;
                m_outIovs = (iovec*)realloc(m_outIovs, realloc_size);
                warn("resize iovs %d => %d, max_msgs=%d", ov, m_nbOutIovs, PERF_MW_MSGS);
            }

            // to next pair of iovs
            iov_index += 2;
            iovs = m_outIovs + iov_index;

            // to next c0c3 header cache
            c0c3_cache_index += nbh;
            c0c3_cache = m_outC0c3Caches + c0c3_cache_index;

            // the cache header should never be realloc again,
            // for the ptr is set to iovs, so we just warn user to set larger
            // and use another loop to send again.
            int c0c3_left = CONSTS_C0C3_HEADERS_MAX - c0c3_cache_index;
            if (c0c3_left < CONSTS_RTMP_MAX_FMT0_HEADER_SIZE) {
                // only warn once for a connection.
                if (!m_warnedC0c3CacheDry) {
                    warn("c0c3 cache header too small, recoment to %d", CONSTS_C0C3_HEADERS_MAX + CONSTS_RTMP_MAX_FMT0_HEADER_SIZE);
                    m_warnedC0c3CacheDry = true;
                }

                // when c0c3 cache dry,
                // sendout all messages and reset the cache, then send again.
                if ((err = DoIovsSend(m_outIovs, iov_index)) != SUCCESS) {
                    return ERRORWRAP(err, "send iovs");
                }

                // reset caches, while these cache ensure
                // atleast we can sendout a chunk.
                iov_index = 0;
                iovs = m_outIovs + iov_index;

                c0c3_cache_index = 0;
                c0c3_cache = m_outC0c3Caches + c0c3_cache_index;
            }
        }
    }

    // maybe the iovs already sendout when c0c3 cache dry,
    // so just ignore when no iovs to send.
    if (iov_index <= 0) {
        return err;
    }

    // Send out iovs at a time.
    if ((err = DoIovsSend(m_outIovs, iov_index)) != SUCCESS) {
        return ERRORWRAP(err, "send iovs");
    }

    return err;
#else
    // try to send use the c0c3 header cache,
    // if cache is consumed, try another loop.
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];

        if (!msg) {
            continue;
        }

        // ignore empty message.
        if (!msg->payload || msg->size <= 0) {
            continue;
        }

        // p set to current write position,
        // it's ok when payload is NULL and size is 0.
        char* p = msg->payload;
        char* pend = msg->payload + msg->size;

        // always write the header event payload is empty.
        while (p < pend) {
            // for simple send, send each chunk one by one
            iovec* iovs = out_iovs;
            char* c0c3_cache = out_c0c3_caches;
            int nb_cache = SRS_CONSTS_C0C3_HEADERS_MAX;

            // always has header
            int nbh = msg->chunk_header(c0c3_cache, nb_cache, p == msg->payload);
            Assert(nbh > 0);

            // header iov
            iovs[0].iov_base = c0c3_cache;
            iovs[0].iov_len = nbh;

            // payload iov
            int payload_size = srs_min(m_outChunkSize, pend - p);
            iovs[1].iov_base = p;
            iovs[1].iov_len = payload_size;

            // consume sendout bytes.
            p += payload_size;

            if ((er = skt->writev(iovs, 2, NULL)) != SUCCESS) {
                return ERRORWRAP(err, "writev");
            }
        }
    }

    return err;
#endif
}

error Protocol::DoIovsSend(iovec *iovs, int size)
{
    return WriteLargeIovs(m_skt, iovs, size);
}

error Protocol::DoSendAndFreePacket(Packet *packet, int stream_id)
{
    error err = SUCCESS;

    Assert(packet);
    AutoFree(Packet, packet);

    CommonMessage* msg = new CommonMessage();
    AutoFree(CommonMessage, msg);

    if ((err = packet->ToMsg(msg, stream_id)) != SUCCESS) {
        return ERRORWRAP(err, "to message");
    }

    SharedPtrMessage* shared_msg = new SharedPtrMessage();
    if ((err = shared_msg->Create(msg)) != SUCCESS) {
        Freep(shared_msg);
        return ERRORWRAP(err, "create message");
    }

    if ((err = SendAndFreeMessage(shared_msg, stream_id)) != SUCCESS) {
        return ERRORWRAP(err, "send packet");
    }

    if ((err = OnSendPacket(&msg->m_header, packet)) != SUCCESS) {
        return ERRORWRAP(err, "on send packet");
    }

    return err;
}

error Protocol::DoDecodeMessage(MessageHeader &header, Buffer *stream, Packet **ppacket)
{
    error err = SUCCESS;

    Packet* packet = NULL;

    // decode specified packet type
    if (header.IsAmf0Command() || header.IsAmf3Command() || header.IsAmf0Data() || header.IsAmf3Data()) {
        // Skip 1bytes to decode the amf3 command.
        if (header.IsAmf3Command() && stream->Require(1)) {
            stream->Skip(1);
        }

        // amf0 command message.
        // need to read the command name.
        std::string command;
        if ((err = Amf0ReadString(stream, command)) != SUCCESS) {
            return ERRORWRAP(err, "decode command name");
        }

        // result/error packet
        if (command == RTMP_AMF0_COMMAND_RESULT || command == RTMP_AMF0_COMMAND_ERROR) {
            double transactionId = 0.0;
            if ((err = Amf0ReadNumber(stream, transactionId)) != SUCCESS) {
                return ERRORWRAP(err, "decode tid for %s", command.c_str());
            }

            // reset stream, for header read completed.
            stream->Skip(-1 * stream->Pos());
            if (header.IsAmf3Command()) {
                stream->Skip(1);
            }

            // find the call name
            if (m_requests.find(transactionId) == m_requests.end()) {
                return ERRORNEW(ERROR_RTMP_NO_REQUEST, "find request for command=%s, tid=%.2f", command.c_str(), transactionId);
            }

            std::string request_name = m_requests[transactionId];
            if (request_name == RTMP_AMF0_COMMAND_CONNECT) {
                *ppacket = packet = new ConnectAppResPacket();
                return packet->Decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_CREATE_STREAM) {
                *ppacket = packet = new CreateStreamResPacket(0, 0);
                return packet->Decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_RELEASE_STREAM) {
                *ppacket = packet = new FMLEStartResPacket(0);
                return packet->Decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_FC_PUBLISH) {
                *ppacket = packet = new FMLEStartResPacket(0);
                return packet->Decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_UNPUBLISH) {
                *ppacket = packet = new FMLEStartResPacket(0);
                return packet->Decode(stream);
            } else {
                return ERRORNEW(ERROR_RTMP_NO_REQUEST, "request=%s, tid=%.2f", request_name.c_str(), transactionId);
            }
        }

        // reset to zero(amf3 to 1) to restart decode.
        stream->Skip(-1 * stream->Pos());
        if (header.IsAmf3Command()) {
            stream->Skip(1);
        }

        // decode command object.
        if (command == RTMP_AMF0_COMMAND_CONNECT) {
            *ppacket = packet = new ConnectAppPacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_CREATE_STREAM) {
            *ppacket = packet = new CreateStreamPacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_PLAY) {
            *ppacket = packet = new PlayPacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_PAUSE) {
            *ppacket = packet = new PausePacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_RELEASE_STREAM) {
            *ppacket = packet = new FMLEStartPacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_FC_PUBLISH) {
            *ppacket = packet = new FMLEStartPacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_PUBLISH) {
            *ppacket = packet = new PublishPacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_UNPUBLISH) {
            *ppacket = packet = new FMLEStartPacket();
            return packet->Decode(stream);
        } else if (command == CONSTS_RTMP_SET_DATAFRAME) {
            *ppacket = packet = new OnMetaDataPacket();
            return packet->Decode(stream);
        } else if (command == CONSTS_RTMP_ON_METADATA) {
            *ppacket = packet = new OnMetaDataPacket();
            return packet->Decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_CLOSE_STREAM) {
            *ppacket = packet = new CloseStreamPacket();
            return packet->Decode(stream);
        } else if (header.IsAmf0Command() || header.IsAmf3Command()) {
            *ppacket = packet = new CallPacket();
            return packet->Decode(stream);
        }

        // default packet to drop message.
        *ppacket = packet = new Packet();
        return err;
    } else if (header.IsUserControlMessage()) {
        *ppacket = packet = new UserControlPacket();
        return packet->Decode(stream);
    } else if (header.IsWindowAckledgementSize()) {
        *ppacket = packet = new SetWindowAckSizePacket();
        return packet->Decode(stream);
    } else if (header.IsAckledgement()) {
        *ppacket = packet = new AcknowledgementPacket();
        return packet->Decode(stream);
    } else if (header.IsSetChunkSize()) {
        *ppacket = packet = new SetChunkSizePacket();
        return packet->Decode(stream);
    } else {
        if (!header.IsSetPeerBandwidth() && !header.IsAckledgement()) {
            trace("drop unknown message, type=%d", header.m_messageType);
        }
    }

    return err;
}

error Protocol::RecvInterlacedMessage(CommonMessage **pmsg)
{
    error err = SUCCESS;

    // chunk stream basic header.
    char fmt = 0;
    int cid = 0;
    if ((err = ReadBasicHeader(fmt, cid)) != SUCCESS) {
        return ERRORWRAP(err, "read basic header");
    }

    // the cid must not negative.
    Assert(cid >= 0);

    // get the cached chunk stream.
    ChunkStream* chunk = NULL;

    // use chunk stream cache to get the chunk info.
    // @see https://github.com/ossrs/srs/issues/249
    if (cid < PERF_CHUNK_STREAM_CACHE) {
        // already init, use it direclty
        chunk = m_csCache[cid];
    } else {
        // chunk stream cache miss, use map.
        if (m_chunkStreams.find(cid) == m_chunkStreams.end()) {
            chunk = m_chunkStreams[cid] = new ChunkStream(cid);
            // set the perfer cid of chunk,
            // which will copy to the message received.
            chunk->m_header.m_perferCid = cid;
        } else {
            chunk = m_chunkStreams[cid];
        }
    }

    // chunk stream message header
    if ((err = ReadMessageHeader(chunk, fmt)) != SUCCESS) {
        return ERRORWRAP(err, "read message header");
    }

    // read msg payload from chunk stream.
    CommonMessage* msg = NULL;
    if ((err = ReadMessagePayload(chunk, &msg)) != SUCCESS) {
        return ERRORWRAP(err, "read message payload");
    }

    // not got an entire RTMP message, try next chunk.
    if (!msg) {
        return err;
    }

    *pmsg = msg;
    return err;
}

/**
 * 6.1.1. Chunk Basic Header
 * The Chunk Basic Header encodes the chunk stream ID and the chunk
 * type(represented by fmt field in the figure below). Chunk type
 * determines the format of the encoded message header. Chunk Basic
 * Header field may be 1, 2, or 3 bytes, depending on the chunk stream
 * ID.
 *
 * The bits 0-5 (least significant) in the chunk basic header represent
 * the chunk stream ID.
 *
 * Chunk stream IDs 2-63 can be encoded in the 1-byte version of this
 * field.
 *    0 1 2 3 4 5 6 7
 *   +-+-+-+-+-+-+-+-+
 *   |fmt|   cs id   |
 *   +-+-+-+-+-+-+-+-+
 *   Figure 6 Chunk basic header 1
 *
 * Chunk stream IDs 64-319 can be encoded in the 2-byte version of this
 * field. ID is computed as (the second byte + 64).
 *   0                   1
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |fmt|    0      | cs id - 64    |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   Figure 7 Chunk basic header 2
 *
 * Chunk stream IDs 64-65599 can be encoded in the 3-byte version of
 * this field. ID is computed as ((the third byte)*256 + the second byte
 * + 64).
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |fmt|     1     |         cs id - 64            |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   Figure 8 Chunk basic header 3
 *
 * cs id: 6 bits
 * fmt: 2 bits
 * cs id - 64: 8 or 16 bits
 *
 * Chunk stream IDs with values 64-319 could be represented by both 2-
 * byte version and 3-byte version of this field.
 */

error Protocol::ReadBasicHeader(char &fmt, int &cid)
{
    error err = SUCCESS;

    if ((err = m_inBuffer->Grow(m_skt, 1)) != SUCCESS) {
        return ERRORWRAP(err, "basic header requires 1 bytes");
    }

    fmt = m_inBuffer->Read1Byte();
    cid = fmt & 0x3f;
    fmt = (fmt >> 6) & 0x03;

    // 2-63, 1B chunk header
    if (cid > 1) {
        return err;
    // 64-319, 2B chunk header
    } else if (cid == 0) {
        if ((err = m_inBuffer->Grow(m_skt, 1)) != SUCCESS) {
            return ERRORWRAP(err, "basic header requires 2 bytes");
        }

        cid = 64;
        cid += (uint8_t)m_inBuffer->Read1Byte();
    // 64-65599, 3B chunk header
    } else {
        Assert(cid == 1);

        if ((err = m_inBuffer->Grow(m_skt, 2)) != SUCCESS) {
            return ERRORWRAP(err, "basic header requires 3 bytes");
        }

        cid = 64;
        cid += (uint8_t)m_inBuffer->Read1Byte();
        cid += ((uint8_t)m_inBuffer->Read1Byte()) * 256;
    }

    return err;
}

/**
 * parse the message header.
 *   3bytes: timestamp delta,    fmt=0,1,2
 *   3bytes: payload length,     fmt=0,1
 *   1bytes: message type,       fmt=0,1
 *   4bytes: stream id,          fmt=0
 * where:
 *   fmt=0, 0x0X
 *   fmt=1, 0x4X
 *   fmt=2, 0x8X
 *   fmt=3, 0xCX
 */

error Protocol::ReadMessageHeader(ChunkStream *chunk, char fmt)
{
    error err = SUCCESS;

    /**
     * we should not assert anything about fmt, for the first packet.
     * (when first packet, the chunk->msg is NULL).
     * the fmt maybe 0/1/2/3, the FMLE will send a 0xC4 for some audio packet.
     * the previous packet is:
     *     04                // fmt=0, cid=4
     *     00 00 1a          // timestamp=26
     *     00 00 9d          // payload_length=157
     *     08                // message_type=8(audio)
     *     01 00 00 00       // stream_id=1
     * the current packet maybe:
     *     c4             // fmt=3, cid=4
     * it's ok, for the packet is audio, and timestamp delta is 26.
     * the current packet must be parsed as:
     *     fmt=0, cid=4
     *     timestamp=26+26=52
     *     payload_length=157
     *     message_type=8(audio)
     *     stream_id=1
     * so we must update the timestamp even fmt=3 for first packet.
     */
    // fresh packet used to update the timestamp even fmt=3 for first packet.
    // fresh packet always means the chunk is the first one of message.
    bool is_first_chunk_of_msg = !chunk->m_msg;

    // but, we can ensure that when a chunk stream is fresh,
    // the fmt must be 0, a new stream.
    if (chunk->m_msgCount == 0 && fmt != RTMP_FMT_TYPE0) {
        // for librtmp, if ping, it will send a fresh stream with fmt=1,
        // 0x42             where: fmt=1, cid=2, protocol contorl user-control message
        // 0x00 0x00 0x00   where: timestamp=0
        // 0x00 0x00 0x06   where: payload_length=6
        // 0x04             where: message_type=4(protocol control user-control message)
        // 0x00 0x06            where: event Ping(0x06)
        // 0x00 0x00 0x0d 0x0f  where: event data 4bytes ping timestamp.
        if (fmt == RTMP_FMT_TYPE1) {
            warn("fresh chunk starts with fmt=1");
        } else {
            // must be a RTMP protocol level error.
            return ERRORNEW(ERROR_RTMP_CHUNK_START, "fresh chunk expect fmt=0, actual=%d, cid=%d", fmt, chunk->m_cid);
        }
    }

    // when exists cache msg, means got an partial message,
    // the fmt must not be type0 which means new message.
    if (chunk->m_msg && fmt == RTMP_FMT_TYPE0) {
        return ERRORNEW(ERROR_RTMP_CHUNK_START, "for existed chunk, fmt should not be 0");
    }

    // create msg when new chunk stream start
    if (!chunk->m_msg) {
        chunk->m_msg = new CommonMessage();
    }

    // read message header from socket to buffer.
    static char mh_sizes[] = {11, 7, 3, 0};
    int mh_size = mh_sizes[(int)fmt];

    if (mh_size > 0 && (err = m_inBuffer->Grow(m_skt, mh_size)) != SUCCESS) {
        return ERRORWRAP(err, "read %d bytes message header", mh_size);
    }

    /**
     * parse the message header.
     *   3bytes: timestamp delta,    fmt=0,1,2
     *   3bytes: payload length,     fmt=0,1
     *   1bytes: message type,       fmt=0,1
     *   4bytes: stream id,          fmt=0
     * where:
     *   fmt=0, 0x0X
     *   fmt=1, 0x4X
     *   fmt=2, 0x8X
     *   fmt=3, 0xCX
     */
    // see also: ngx_rtmp_recv
    if (fmt <= RTMP_FMT_TYPE2) {
        char* p = m_inBuffer->ReadSlice(mh_size);

        char* pp = (char*)&chunk->m_header.m_timestampDelta;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        pp[3] = 0;

        // fmt: 0
        // timestamp: 3 bytes
        // If the timestamp is greater than or equal to 16777215
        // (hexadecimal 0x00ffffff), this value MUST be 16777215, and the
        // 'extended timestamp header' MUST be present. Otherwise, this value
        // SHOULD be the entire timestamp.
        //
        // fmt: 1 or 2
        // timestamp delta: 3 bytes
        // If the delta is greater than or equal to 16777215 (hexadecimal
        // 0x00ffffff), this value MUST be 16777215, and the 'extended
        // timestamp header' MUST be present. Otherwise, this value SHOULD be
        // the entire delta.
        chunk->m_extendedTimestamp = (chunk->m_header.m_timestampDelta >= RTMP_EXTENDED_TIMESTAMP);
        if (!chunk->m_extendedTimestamp) {
            // Extended timestamp: 0 or 4 bytes
            // This field MUST be sent when the normal timsestamp is set to
            // 0xffffff, it MUST NOT be sent if the normal timestamp is set to
            // anything else. So for values less than 0xffffff the normal
            // timestamp field SHOULD be used in which case the extended timestamp
            // MUST NOT be present. For values greater than or equal to 0xffffff
            // the normal timestamp field MUST NOT be used and MUST be set to
            // 0xffffff and the extended timestamp MUST be sent.
            if (fmt == RTMP_FMT_TYPE0) {
                // 6.1.2.1. Type 0
                // For a type-0 chunk, the absolute timestamp of the message is sent
                // here.
                chunk->m_header.m_timestamp = chunk->m_header.m_timestampDelta;
            } else {
                // 6.1.2.2. Type 1
                // 6.1.2.3. Type 2
                // For a type-1 or type-2 chunk, the difference between the previous
                // chunk's timestamp and the current chunk's timestamp is sent here.
                chunk->m_header.m_timestamp += chunk->m_header.m_timestampDelta;
            }
        }

        if (fmt <= RTMP_FMT_TYPE1) {
            int32_t payload_length = 0;
            pp = (char*)&payload_length;
            pp[2] = *p++;
            pp[1] = *p++;
            pp[0] = *p++;
            pp[3] = 0;

            // for a message, if msg exists in cache, the size must not changed.
            // always use the actual msg size to compare, for the cache payload length can changed,
            // for the fmt type1(stream_id not changed), user can change the payload
            // length(it's not allowed in the continue chunks).
            if (!is_first_chunk_of_msg && chunk->m_header.m_payloadLength != payload_length) {
                return ERRORNEW(ERROR_RTMP_PACKET_SIZE, "msg in chunk cache, size=%d cannot change to %d", chunk->m_header.m_payloadLength, payload_length);
            }

            chunk->m_header.m_payloadLength = payload_length;
            chunk->m_header.m_messageType = *p++;

            if (fmt == RTMP_FMT_TYPE0) {
                pp = (char*)&chunk->m_header.m_streamId;
                pp[0] = *p++;
                pp[1] = *p++;
                pp[2] = *p++;
                pp[3] = *p++;
            }
        }
    } else {
        // update the timestamp even fmt=3 for first chunk packet
        if (is_first_chunk_of_msg && !chunk->m_extendedTimestamp) {
            chunk->m_header.m_timestamp += chunk->m_header.m_timestampDelta;
        }
    }

    // read extended-timestamp
    if (chunk->m_extendedTimestamp) {
        mh_size += 4;
        if ((err = m_inBuffer->Grow(m_skt, 4)) != SUCCESS) {
            return ERRORWRAP(err, "read 4 bytes ext timestamp");
        }
        // the ptr to the slice maybe invalid when grow()
        // reset the p to get 4bytes slice.
        char* p = m_inBuffer->ReadSlice(4);

        uint32_t timestamp = 0x00;
        char* pp = (char*)&timestamp;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;

        // always use 31bits timestamp, for some server may use 32bits extended timestamp.
        timestamp &= 0x7fffffff;

        /**
         * RTMP specification and ffmpeg/librtmp is false,
         * but, adobe changed the specification, so flash/FMLE/FMS always true.
         * default to true to support flash/FMLE/FMS.
         *
         * ffmpeg/librtmp may donot send this filed, need to detect the value.
         * @see also: http://blog.csdn.net/win_lin/article/details/13363699
         * compare to the chunk timestamp, which is set by chunk message header
         * type 0,1 or 2.
         *
         * @remark, nginx send the extended-timestamp in sequence-header,
         * and timestamp delta in continue C1 chunks, and so compatible with ffmpeg,
         * that is, there is no continue chunks and extended-timestamp in nginx-rtmp.
         *
         * @remark, srs always send the extended-timestamp, to keep simple,
         * and compatible with adobe products.
         */
        uint32_t chunk_timestamp = (uint32_t)chunk->m_header.m_timestamp;

        /**
         * if chunk_timestamp<=0, the chunk previous packet has no extended-timestamp,
         * always use the extended timestamp.
         */
        /**
         * about the is_first_chunk_of_msg.
         * @remark, for the first chunk of message, always use the extended timestamp.
         */
        if (!is_first_chunk_of_msg && chunk_timestamp > 0 && chunk_timestamp != timestamp) {
            mh_size -= 4;
            m_inBuffer->Skip(-4);
        } else {
            chunk->m_header.m_timestamp = timestamp;
        }
    }

    // the extended-timestamp must be unsigned-int,
    //         24bits timestamp: 0xffffff = 16777215ms = 16777.215s = 4.66h
    //         32bits timestamp: 0xffffffff = 4294967295ms = 4294967.295s = 1193.046h = 49.71d
    // because the rtmp protocol says the 32bits timestamp is about "50 days":
    //         3. Byte Order, Alignment, and Time Format
    //                Because timestamps are generally only 32 bits long, they will roll
    //                over after fewer than 50 days.
    //
    // but, its sample says the timestamp is 31bits:
    //         An application could assume, for example, that all
    //        adjacent timestamps are within 2^31 milliseconds of each other, so
    //        10000 comes after 4000000000, while 3000000000 comes before
    //        4000000000.
    // and flv specification says timestamp is 31bits:
    //        Extension of the Timestamp field to form a SI32 value. This
    //        field represents the upper 8 bits, while the previous
    //        Timestamp field represents the lower 24 bits of the time in
    //        milliseconds.
    // in a word, 31bits timestamp is ok.
    // convert extended timestamp to 31bits.
    chunk->m_header.m_timestamp &= 0x7fffffff;

    // valid message, the payload_length is 24bits,
    // so it should never be negative.
    Assert(chunk->m_header.m_payloadLength >= 0);

    // copy header to msg
    chunk->m_msg->m_header = chunk->m_header;

    // increase the msg count, the chunk stream can accept fmt=1/2/3 message now.
    chunk->m_msgCount++;

    return err;
}

error Protocol::ReadMessagePayload(ChunkStream *chunk, CommonMessage **pmsg)
{
    error err = SUCCESS;

    // empty message
    if (chunk->m_header.m_payloadLength <= 0) {
        trace("get an empty RTMP message(type=%d, size=%d, time=%" PRId64 ", sid=%d)", chunk->m_header.m_messageType,
                  chunk->m_header.m_payloadLength, chunk->m_header.m_timestamp, chunk->m_header.m_streamId);

        *pmsg = chunk->m_msg;
        chunk->m_msg = NULL;

        return err;
    }
    Assert(chunk->m_header.m_payloadLength > 0);

    // the chunk payload size.
    int payload_size = chunk->m_header.m_payloadLength - chunk->m_msg->m_size;
    payload_size = MIN(payload_size, m_inChunkSize);

    // create msg payload if not initialized
    if (!chunk->m_msg->m_payload) {
        chunk->m_msg->CreatePayload(chunk->m_header.m_payloadLength);
    }

    // read payload to buffer
    if ((err = m_inBuffer->Grow(m_skt, payload_size)) != SUCCESS) {
        return ERRORWRAP(err, "read %d bytes payload", payload_size);
    }
    memcpy(chunk->m_msg->m_payload + chunk->m_msg->m_size, m_inBuffer->ReadSlice(payload_size), payload_size);
    chunk->m_msg->m_size += payload_size;

    // got entire RTMP message?
    if (chunk->m_header.m_payloadLength == chunk->m_msg->m_size) {
        *pmsg = chunk->m_msg;
        chunk->m_msg = NULL;
        return err;
    }

    return err;
}

error Protocol::OnRecvMessage(CommonMessage *msg)
{
    error err = SUCCESS;

    Assert(msg != NULL);

    // try to response acknowledgement
    if ((err = ResponseAcknowledgementMessage()) != SUCCESS) {
        return ERRORWRAP(err, "response ack");
    }

    Packet* packet = NULL;
    switch (msg->m_header.m_messageType) {
        case RTMP_MSG_SetChunkSize:
        case RTMP_MSG_UserControlMessage:
        case RTMP_MSG_WindowAcknowledgementSize:
            if ((err = DecodeMessage(msg, &packet)) != SUCCESS) {
                return ERRORWRAP(err, "decode message");
            }
            break;
        case RTMP_MSG_VideoMessage:
        case RTMP_MSG_AudioMessage:
            PrintDebugInfo();
        default:
            return err;
    }

    Assert(packet);

    // always free the packet.
    AutoFree(Packet, packet);

    switch (msg->m_header.m_messageType) {
        case RTMP_MSG_WindowAcknowledgementSize: {
            SetWindowAckSizePacket* pkt = dynamic_cast<SetWindowAckSizePacket*>(packet);
            Assert(pkt != NULL);

            if (pkt->m_ackowledgementWindowSize > 0) {
                m_inAckSize.m_window = (uint32_t)pkt->m_ackowledgementWindowSize;
                // @remark, we ignore this message, for user noneed to care.
                // but it's important for dev, for client/server will block if required
                // ack msg not arrived.
            }
            break;
        }
        case RTMP_MSG_SetChunkSize: {
            SetChunkSizePacket* pkt = dynamic_cast<SetChunkSizePacket*>(packet);
            Assert(pkt != NULL);

            // for some server, the actual chunk size can greater than the max value(65536),
            // so we just warning the invalid chunk size, and actually use it is ok,
            // @see: https://github.com/ossrs/srs/issues/160
            if (pkt->m_chunkSize < CONSTS_RTMP_MIN_CHUNK_SIZE || pkt->m_chunkSize > CONSTS_RTMP_MAX_CHUNK_SIZE) {
                warn("accept chunk=%d, should in [%d, %d], please see #160",
                         pkt->m_chunkSize, CONSTS_RTMP_MIN_CHUNK_SIZE,  CONSTS_RTMP_MAX_CHUNK_SIZE);
            }

            // @see: https://github.com/ossrs/srs/issues/541
            if (pkt->m_chunkSize < CONSTS_RTMP_MIN_CHUNK_SIZE) {
                return ERRORNEW(ERROR_RTMP_CHUNK_SIZE, "chunk size should be %d+, value=%d", CONSTS_RTMP_MIN_CHUNK_SIZE, pkt->m_chunkSize);
            }

            m_inChunkSize = pkt->m_chunkSize;
            break;
        }
        case RTMP_MSG_UserControlMessage: {
            UserControlPacket* pkt = dynamic_cast<UserControlPacket*>(packet);
            Assert(pkt != NULL);

            if (pkt->m_eventType == SrcPCUCSetBufferLength) {
                m_inBufferLength = pkt->m_extraData;
            }
            if (pkt->m_eventType == SrcPCUCPingRequest) {
                if ((err = ResponsePingMessage(pkt->m_eventData)) != SUCCESS) {
                    return ERRORWRAP(err, "response ping");
                }
            }
            break;
        }
        default:
            break;
    }

    return err;
}

error Protocol::OnSendPacket(MessageHeader *mh, Packet *packet)
{
    error err = SUCCESS;

    // ignore raw bytes oriented RTMP message.
    if (packet == NULL) {
        return err;
    }

    switch (mh->m_messageType) {
        case RTMP_MSG_SetChunkSize: {
            SetChunkSizePacket* pkt = dynamic_cast<SetChunkSizePacket*>(packet);
            m_outChunkSize = pkt->m_chunkSize;
            break;
        }
        case RTMP_MSG_WindowAcknowledgementSize: {
            SetWindowAckSizePacket* pkt = dynamic_cast<SetWindowAckSizePacket*>(packet);
            m_outAckSize.m_window = (uint32_t)pkt->m_ackowledgementWindowSize;
            break;
        }
        case RTMP_MSG_AMF0CommandMessage:
        case RTMP_MSG_AMF3CommandMessage: {
            if (true) {
                ConnectAppPacket* pkt = dynamic_cast<ConnectAppPacket*>(packet);
                if (pkt) {
                    m_requests[pkt->m_transactionId] = pkt->m_commandName;
                    break;
                }
            }
            if (true) {
                CreateStreamPacket* pkt = dynamic_cast<CreateStreamPacket*>(packet);
                if (pkt) {
                    m_requests[pkt->m_transactionId] = pkt->m_commandName;
                    break;
                }
            }
            if (true) {
                FMLEStartPacket* pkt = dynamic_cast<FMLEStartPacket*>(packet);
                if (pkt) {
                    m_requests[pkt->m_transactionId] = pkt->m_commandName;
                    break;
                }
            }
            break;
        }
        case RTMP_MSG_VideoMessage:
        case RTMP_MSG_AudioMessage:
            PrintDebugInfo();
        default:
            break;
    }

    return err;
}

error Protocol::ResponseAcknowledgementMessage()
{
    error err = SUCCESS;

    if (m_inAckSize.m_window <= 0) {
        return err;
    }

    // ignore when delta bytes not exceed half of window(ack size).
    uint32_t delta = (uint32_t)(m_skt->GetRecvBytes() - m_inAckSize.m_nbRecvBytes);
    if (delta < m_inAckSize.m_window / 2) {
        return err;
    }
    m_inAckSize.m_nbRecvBytes = m_skt->GetRecvBytes();

    // when the sequence number overflow, reset it.
    uint32_t sequence_number = m_inAckSize.m_sequenceNumber + delta;
    if (sequence_number > 0xf0000000) {
        sequence_number = delta;
    }
    m_inAckSize.m_sequenceNumber = sequence_number;

    AcknowledgementPacket* pkt = new AcknowledgementPacket();
    pkt->m_sequenceNumber = sequence_number;

    // cache the message and use flush to send.
    if (!m_autoResponseWhenRecv) {
        m_manualResponseQueue.push_back(pkt);
        return err;
    }

    // use underlayer api to send, donot flush again.
    if ((err = DoSendAndFreePacket(pkt, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send ack");
    }

    return err;
}

error Protocol::ResponsePingMessage(int32_t timestamp)
{
    error err = SUCCESS;

    trace("get a ping request, response it. timestamp=%d", timestamp);

    UserControlPacket* pkt = new UserControlPacket();

    pkt->m_eventType = SrcPCUCPingResponse;
    pkt->m_eventData = timestamp;

    // cache the message and use flush to send.
    if (!m_autoResponseWhenRecv) {
        m_manualResponseQueue.push_back(pkt);
        return err;
    }

    // use underlayer api to send, donot flush again.
    if ((err = DoSendAndFreePacket(pkt, 0)) != SUCCESS) {
        return ERRORWRAP(err, "ping response");
    }

    return err;
}

void Protocol::PrintDebugInfo()
{
    if (m_showDebugInfo) {
        m_showDebugInfo = false;
        trace("protocol in.buffer=%d, in.ack=%d, out.ack=%d, in.chunk=%d, out.chunk=%d", m_inBufferLength,
                  m_inAckSize.m_window, m_outAckSize.m_window, m_inChunkSize, m_outChunkSize);
    }
}

ChunkStream::ChunkStream(int _cid)
{
    m_fmt = 0;
    m_cid = _cid;
    m_extendedTimestamp = false;
    m_msg = NULL;
    m_msgCount = 0;
}

ChunkStream::~ChunkStream()
{
    Freep(m_msg);
}

Request::Request()
{
    m_objectEncoding = RTMP_SIG_AMF0_VER;
    m_duration = -1;
    m_port = CONSTS_RTMP_DEFAULT_PORT;
    m_args = NULL;

    m_protocol = "rtmp";
}

Request::~Request()
{
    Freep(m_args);
}

Request *Request::Copy()
{
    Request* cp = new Request();

    cp->m_ip = m_ip;
    cp->m_vhost = m_vhost;
    cp->m_app = m_app;
    cp->m_objectEncoding = m_objectEncoding;
    cp->m_pageUrl = m_pageUrl;
    cp->m_host = m_host;
    cp->m_port = m_port;
    cp->m_param = m_param;
    cp->m_schema = m_schema;
    cp->m_stream = m_stream;
    cp->m_swfUrl = m_swfUrl;
    cp->m_tcUrl = m_tcUrl;
    cp->m_duration = m_duration;
    if (m_args) {
        cp->m_args = m_args->Copy()->ToObject();
    }

    cp->m_protocol = m_protocol;

    return cp;
}

void Request::UpdateAuth(Request *req)
{
    m_pageUrl = req->m_pageUrl;
    m_swfUrl = req->m_swfUrl;
    m_tcUrl = req->m_tcUrl;
    m_param = req->m_param;

    m_ip = req->m_ip;
    m_vhost = req->m_vhost;
    m_app = req->m_app;
    m_objectEncoding = req->m_objectEncoding;
    m_host = req->m_host;
    m_port = req->m_port;
    m_param = req->m_param;
    m_schema = req->m_schema;
    m_duration = req->m_duration;

    if (m_args) {
        Freep(m_args);
    }
    if (req->m_args) {
        m_args = req->m_args->Copy()->ToObject();
    }

    m_protocol = req->m_protocol;

    info("update req of soruce for auth ok");
}

std::string Request::GetStreamUrl()
{
    return GenerateStreamUrl(m_vhost, m_app, m_stream);
}

void Request::Strip()
{
    // remove the unsupported chars in names.
    m_host = StringRemove(m_host, "/ \n\r\t");
    m_vhost = StringRemove(m_vhost, "/ \n\r\t");
    m_app = StringRemove(m_app, " \n\r\t");
    m_stream = StringRemove(m_stream, " \n\r\t");

    // remove end slash of app/stream
    m_app = StringTrimEnd(m_app, "/");
    m_stream = StringTrimEnd(m_stream, "/");

    // remove start slash of app/stream
    m_app = StringTrimStart(m_app, "/");
    m_stream = StringTrimStart(m_stream, "/");
}

Request *Request::AsHttp()
{
    m_schema = "http";
    m_tcUrl = GenerateTcUrl(m_schema, m_host, m_vhost, m_app, m_port);
    return this;
}

Response::Response()
{
    m_streamId = DEFAULT_SID;
}

Response::~Response()
{

}

std::string ClientTypeString(RtmpConnType type)
{
    switch (type) {
        case RtmpConnPlay: return "rtmp-play";
        case HlsPlay: return "hls-play";
        case FlvPlay: return "flv-play";
        case RtcConnPlay: return "rtc-play";
        case RtmpConnFlashPublish: return "flash-publish";
        case RtmpConnFMLEPublish: return "fmle-publish";
        case RtmpConnHaivisionPublish: return "haivision-publish";
        case RtcConnPublish: return "rtc-publish";
        case SrtConnPlay: return "srt-play";
        case SrtConnPublish: return "srt-publish";
        default: return "Unknown";
    }
}

bool ClientTypeIsPublish(RtmpConnType type)
{
    return (type & 0xff00) == 0x0200;
}

HandshakeBytes::HandshakeBytes()
{
    m_c0c1 = m_s0s1s2 = m_c2 = NULL;
    m_proxyRealIp = 0;
}

HandshakeBytes::~HandshakeBytes()
{
    Dispose();
}

void HandshakeBytes::Dispose()
{
    Freepa(m_c0c1);
    Freepa(m_s0s1s2);
    Freepa(m_c2);
}

error HandshakeBytes::ReadC0c1(IProtocolReader *io)
{
    error err = SUCCESS;

    if (m_c0c1) {
        return err;
    }

    ssize_t nsize;

    m_c0c1 = new char[1537];
    if ((err = io->ReadFully(m_c0c1, 1537, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "read c0c1");
    }

    // Whether RTMP proxy, @see https://github.com/ossrs/go-oryx/wiki/RtmpProxy
    if (uint8_t(m_c0c1[0]) == 0xF3) {
        uint16_t nn = uint16_t(m_c0c1[1])<<8 | uint16_t(m_c0c1[2]);
        ssize_t nn_consumed = 3 + nn;
        if (nn > 1024) {
            return ERRORNEW(ERROR_RTMP_PROXY_EXCEED, "proxy exceed max size, nn=%d", nn);
        }

        // 4B client real IP.
        if (nn >= 4) {
            m_proxyRealIp = uint32_t(m_c0c1[3])<<24 | uint32_t(m_c0c1[4])<<16 | uint32_t(m_c0c1[5])<<8 | uint32_t(m_c0c1[6]);
            nn -= 4;
        }

        memmove(m_c0c1, m_c0c1 + nn_consumed, 1537 - nn_consumed);
        if ((err = io->ReadFully(m_c0c1 + 1537 - nn_consumed, nn_consumed, &nsize)) != SUCCESS) {
            return ERRORWRAP(err, "read c0c1");
        }
    }

    return err;
}

error HandshakeBytes::ReadS0s1s2(IProtocolReader *io)
{
    error err = SUCCESS;

    if (m_s0s1s2) {
        return err;
    }

    ssize_t nsize;

    m_s0s1s2 = new char[3073];
    if ((err = io->ReadFully(m_s0s1s2, 3073, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "read s0s1s2");
    }

    return err;
}

error HandshakeBytes::ReadC2(IProtocolReader *io)
{
    error err = SUCCESS;

    if (m_c2) {
        return err;
    }

    ssize_t nsize;

    m_c2 = new char[1536];
    if ((err = io->ReadFully(m_c2, 1536, &nsize)) != SUCCESS) {
        return ERRORWRAP(err, "read c2");
    }

    return err;
}

error HandshakeBytes::CreateC0c1()
{
    error err = SUCCESS;

    if (m_c0c1) {
        return err;
    }

    m_c0c1 = new char[1537];
    RandomGenerate(m_c0c1, 1537);

    // plain text required.
    Buffer stream(m_c0c1, 9);

    stream.Write1Bytes(0x03);
    stream.Write4Bytes((int32_t)::time(NULL));
    stream.Write4Bytes(0x00);

    return err;
}

error HandshakeBytes::CreateS0s1s2(const char *c1)
{
    error err = SUCCESS;

    if (m_s0s1s2) {
        return err;
    }

    m_s0s1s2 = new char[3073];
    RandomGenerate(m_s0s1s2, 3073);

    // plain text required.
    Buffer stream(m_s0s1s2, 9);

    stream.Write1Bytes(0x03);
    stream.Write4Bytes((int32_t)::time(NULL));
    // s1 time2 copy from c1
    if (m_c0c1) {
        stream.WriteBytes(m_c0c1 + 1, 4);
    }

    // if c1 specified, copy c1 to s2.
    // @see: https://github.com/ossrs/srs/issues/46
    if (c1) {
        memcpy(m_s0s1s2 + 1537, c1, 1536);
    }

    return err;
}

error HandshakeBytes::CreateC2()
{
    error err = SUCCESS;

    if (m_c2) {
        return err;
    }

    m_c2 = new char[1536];
    RandomGenerate(m_c2, 1536);

    // time
    Buffer stream(m_c2, 8);

    stream.Write4Bytes((int32_t)::time(NULL));
    // c2 time2 copy from s1
    if (m_s0s1s2) {
        stream.WriteBytes(m_s0s1s2 + 1, 4);
    }

    return err;
}

ServerInfo::ServerInfo()
{
    m_pid = m_cid = 0;
    m_major = m_minor = m_revision = m_build = 0;
}

RtmpClient::RtmpClient(IProtocolReadWriter *skt)
{
    m_io = skt;
    m_protocol = new Protocol(skt);
    m_hsBytes = new HandshakeBytes();
}

RtmpClient::~RtmpClient()
{
    Freep(m_protocol);
    Freep(m_hsBytes);
}

void RtmpClient::SetRecvTimeout(utime_t tm)
{
    m_protocol->SetRecvTimeout(tm);
}

void RtmpClient::SetSendTimeout(utime_t tm)
{
    m_protocol->SetSendTimeout(tm);
}

int64_t RtmpClient::GetRecvBytes()
{
    return m_protocol->GetRecvBytes();
}

int64_t RtmpClient::GetSendBytes()
{
    return m_protocol->GetSendBytes();
}

error RtmpClient::RecvMessage(CommonMessage **pmsg)
{
    return m_protocol->RecvMessage(pmsg);
}

error RtmpClient::DecodeMessage(CommonMessage *msg, Packet **ppacket)
{
    return m_protocol->DecodeMessage(msg, ppacket);
}

error RtmpClient::SendAndFreeMessage(SharedPtrMessage *msg, int stream_id)
{
    return m_protocol->SendAndFreeMessage(msg, stream_id);
}

error RtmpClient::SendAndFreeMessages(SharedPtrMessage **msgs, int nb_msgs, int stream_id)
{
    return m_protocol->SendAndFreeMessages(msgs, nb_msgs, stream_id);
}

error RtmpClient::SendAndFreePacket(Packet *packet, int stream_id)
{
    return m_protocol->SendAndFreePacket(packet, stream_id);
}

error RtmpClient::Handshake()
{
    error err = SUCCESS;

    Assert(m_hsBytes);

    // maybe st has problem when alloc object on stack, always alloc object at heap.
    // @see https://github.com/ossrs/srs/issues/509
    ComplexHandshake* complex_hs = new ComplexHandshake();
    AutoFree(ComplexHandshake, complex_hs);

    if ((err = complex_hs->HandshakeWithServer(m_hsBytes, m_io)) != SUCCESS) {
        // As client, we never verify s0s1s2, because some server doesn't follow the RTMP spec.
        // So we never have chance to use simple handshake.
        return ERRORWRAP(err, "complex handshake");
    }

    m_hsBytes->Dispose();

    return err;
}

error RtmpClient::SimpleHandshaken()
{
    error err = SUCCESS;

    Assert(m_hsBytes);

    SimpleHandshake simple_hs;
    if ((err = simple_hs.HandshakeWithServer(m_hsBytes, m_io)) != SUCCESS) {
        return ERRORWRAP(err, "simple handshake");
    }

    m_hsBytes->Dispose();

    return err;
}

error RtmpClient::ComplexHandshaken()
{
    error err = SUCCESS;

    Assert(m_hsBytes);

    ComplexHandshake complex_hs;
    if ((err = complex_hs.HandshakeWithServer(m_hsBytes, m_io)) != SUCCESS) {
        return ERRORWRAP(err, "complex handshake");
    }

    m_hsBytes->Dispose();

    return err;
}

error RtmpClient::ConnectApp(std::string app, std::string tcUrl, Request *r, bool dsu, ServerInfo *si)
{
    error err = SUCCESS;

    // Connect(vhost, app)
    if (true) {
        ConnectAppPacket* pkt = new ConnectAppPacket();

        pkt->m_commandObject->Set("app", Amf0Any::Str(app.c_str()));
        pkt->m_commandObject->Set("flashVer", Amf0Any::Str("WIN 15,0,0,239"));
        if (r) {
            pkt->m_commandObject->Set("swfUrl", Amf0Any::Str(r->m_swfUrl.c_str()));
        } else {
            pkt->m_commandObject->Set("swfUrl", Amf0Any::Str());
        }
        if (r && r->m_tcUrl != "") {
            pkt->m_commandObject->Set("tcUrl", Amf0Any::Str(r->m_tcUrl.c_str()));
        } else {
            pkt->m_commandObject->Set("tcUrl", Amf0Any::Str(tcUrl.c_str()));
        }
        pkt->m_commandObject->Set("fpad", Amf0Any::Boolean(false));
        pkt->m_commandObject->Set("capabilities", Amf0Any::Number(239));
        pkt->m_commandObject->Set("audioCodecs", Amf0Any::Number(3575));
        pkt->m_commandObject->Set("videoCodecs", Amf0Any::Number(252));
        pkt->m_commandObject->Set("videoFunction", Amf0Any::Number(1));
        if (r) {
            pkt->m_commandObject->Set("pageUrl", Amf0Any::Str(r->m_pageUrl.c_str()));
        } else {
            pkt->m_commandObject->Set("pageUrl", Amf0Any::Str());
        }
        pkt->m_commandObject->Set("objectEncoding", Amf0Any::Number(0));

        // @see https://github.com/ossrs/srs/issues/160
        // the debug_srs_upnode is config in vhost and default to true.
        if (dsu && r && r->m_args && r->m_args->Count() > 0) {
            Freep(pkt->m_args);
            pkt->m_args = r->m_args->Copy()->ToObject();
        }

        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send packet");
        }
    }

    // Set Window Acknowledgement size(2500000)
    if (true) {
        SetWindowAckSizePacket* pkt = new SetWindowAckSizePacket();
        pkt->m_ackowledgementWindowSize = 2500000;
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send packet");
        }
    }

    // expect connect _result
    CommonMessage* msg = NULL;
    ConnectAppResPacket* pkt = NULL;
    if ((err = ExpectMessage<ConnectAppResPacket>(&msg, &pkt)) != SUCCESS) {
        return ERRORWRAP(err, "expect connect app response");
    }
    AutoFree(CommonMessage, msg);
    AutoFree(ConnectAppResPacket, pkt);

    // server info
    Amf0Any* data = pkt->m_info->GetProperty("data");
    if (si && data && data->IsEcmaArray()) {
        Amf0EcmaArray* arr = data->ToEcmaArray();

        Amf0Any* prop = NULL;
        if ((prop = arr->EnsurePropertyString("srs_server_ip")) != NULL) {
            si->m_ip = prop->ToStr();
        }
        if ((prop = arr->EnsurePropertyString("srs_server")) != NULL) {
            si->m_sig = prop->ToStr();
        }
        if ((prop = arr->EnsurePropertyNumber("srs_id")) != NULL) {
            si->m_cid = (int)prop->ToNumber();
        }
        if ((prop = arr->EnsurePropertyNumber("srs_pid")) != NULL) {
            si->m_pid = (int)prop->ToNumber();
        }
        if ((prop = arr->EnsurePropertyString("srs_version")) != NULL) {
            std::vector<std::string> versions = StringSplit(prop->ToStr(), ".");
            if (versions.size() > 0) {
                si->m_major = ::atoi(versions.at(0).c_str());
                if (versions.size() > 1) {
                    si->m_minor = ::atoi(versions.at(1).c_str());
                    if (versions.size() > 2) {
                        si->m_revision = ::atoi(versions.at(2).c_str());
                        if (versions.size() > 3) {
                            si->m_build = ::atoi(versions.at(3).c_str());
                        }
                    }
                }
            }
        }
    }

    if (si) {
        trace("connected, version=%d.%d.%d.%d, ip=%s, pid=%d, id=%d, dsu=%d",
                  si->m_major, si->m_minor, si->m_revision, si->m_build, si->m_ip.c_str(), si->m_pid, si->m_cid, dsu);
    } else {
        trace("connected, dsu=%d", dsu);
    }

    return err;
}

error RtmpClient::CreateStream(int &stream_id)
{
    error err = SUCCESS;

    // CreateStream
    if (true) {
        CreateStreamPacket* pkt = new CreateStreamPacket();
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send packet");
        }
    }

    // CreateStream _result.
    if (true) {
        CommonMessage* msg = NULL;
        CreateStreamResPacket* pkt = NULL;
        if ((err = ExpectMessage<CreateStreamResPacket>(&msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "expect create stream response");
        }
        AutoFree(CommonMessage, msg);
        AutoFree(CreateStreamResPacket, pkt);

        stream_id = (int)pkt->m_streamId;
    }

    return err;
}

error RtmpClient::Play(std::string stream, int stream_id, int chunk_size)
{
    error err = SUCCESS;

    // Play(stream)
    if (true) {
        PlayPacket* pkt = new PlayPacket();
        pkt->m_streamName = stream;
        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send play stream failed. stream=%s, stream_id=%d", stream.c_str(), stream_id);
        }
    }

    // SetBufferLength(1000ms)
    int buffer_length_ms = 1000;
    if (true) {
        UserControlPacket* pkt = new UserControlPacket();

        pkt->m_eventType = SrcPCUCSetBufferLength;
        pkt->m_eventData = stream_id;
        pkt->m_extraData = buffer_length_ms;

        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send set buffer length failed. stream=%s, stream_id=%d, bufferLength=%d", stream.c_str(), stream_id, buffer_length_ms);
        }
    }

    // SetChunkSize
    if (chunk_size != CONSTS_RTMP_PROTOCOL_CHUNK_SIZE) {
        SetChunkSizePacket* pkt = new SetChunkSizePacket();
        pkt->m_chunkSize = chunk_size;
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send set chunk size failed. stream=%s, chunk_size=%d", stream.c_str(), chunk_size);
        }
    }

    return err;
}

error RtmpClient::Publish(std::string stream, int stream_id, int chunk_size)
{
    error err = SUCCESS;

    // SetChunkSize
    if (chunk_size != CONSTS_RTMP_PROTOCOL_CHUNK_SIZE) {
        SetChunkSizePacket* pkt = new SetChunkSizePacket();
        pkt->m_chunkSize = chunk_size;
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send set chunk size failed. stream=%s, chunk_size=%d", stream.c_str(), chunk_size);
        }
    }

    // publish(stream)
    if (true) {
        PublishPacket* pkt = new PublishPacket();
        pkt->m_streamName = stream;
        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send publish message failed. stream=%s, stream_id=%d", stream.c_str(), stream_id);
        }
    }

    return err;
}

error RtmpClient::FmlePublish(std::string stream, int &stream_id)
{
    stream_id = 0;

    error err = SUCCESS;

    // FMLEStartPacket
    if (true) {
        FMLEStartPacket* pkt = FMLEStartPacket::CreateReleaseStream(stream);
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send FMLE publish release stream failed. stream=%s", stream.c_str());
        }
    }

    // FCPublish
    if (true) {
        FMLEStartPacket* pkt = FMLEStartPacket::CreateFCPublish(stream);
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send FMLE publish FCPublish failed. stream=%s", stream.c_str());
        }
    }

    // CreateStream
    if (true) {
        CreateStreamPacket* pkt = new CreateStreamPacket();
        pkt->m_transactionId = 4;
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send FMLE publish createStream failed. stream=%s", stream.c_str());
        }
    }

    // expect result of CreateStream
    if (true) {
        CommonMessage* msg = NULL;
        CreateStreamResPacket* pkt = NULL;
        if ((err = ExpectMessage<CreateStreamResPacket>(&msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "expect create stream response message failed");
        }
        AutoFree(CommonMessage, msg);
        AutoFree(CreateStreamResPacket, pkt);

        stream_id = (int)pkt->m_streamId;
    }

    // publish(stream)
    if (true) {
        PublishPacket* pkt = new PublishPacket();
        pkt->m_streamName = stream;
        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send FMLE publish publish failed. stream=%s, stream_id=%d", stream.c_str(), stream_id);
        }
    }

    return err;
}

RtmpServer::RtmpServer(IProtocolReadWriter *skt)
{
    m_io = skt;
    m_protocol = new Protocol(skt);
    m_hsBytes = new HandshakeBytes();
}

RtmpServer::~RtmpServer()
{
    Freep(m_protocol);
    Freep(m_hsBytes);
}

uint32_t RtmpServer::ProxyRealIp()
{
    return m_hsBytes->m_proxyRealIp;
}

void RtmpServer::SetAutoResponse(bool v)
{
    m_protocol->SetAutoResponse(v);
}

#ifdef PERF_MERGED_READ
void RtmpServer::SetMergeRead(bool v, IMergeReadHandler* handler)
{
    m_protocol->SetMergeRead(v, handler);
}

void RtmpServer::SetRecvBuffer(int buffer_size)
{
    m_protocol->SetRecvBuffer(buffer_size);
}
#endif

void RtmpServer::SetRecvTimeout(utime_t tm)
{
    m_protocol->SetRecvTimeout(tm);
}

utime_t RtmpServer::GetRecvTimeout()
{
    return m_protocol->GetRecvTimeout();
}

void RtmpServer::SetSendTimeout(utime_t tm)
{
    m_protocol->SetSendTimeout(tm);
}

utime_t RtmpServer::GetSendTimeout()
{
    return m_protocol->GetSendTimeout();
}

int64_t RtmpServer::GetRecvBytes()
{
    return m_protocol->GetRecvBytes();
}

int64_t RtmpServer::GetSendBytes()
{
    return m_protocol->GetSendBytes();
}

error RtmpServer::RecvMessage(CommonMessage **pmsg)
{
    return m_protocol->RecvMessage(pmsg);
}

error RtmpServer::DecodeMessage(CommonMessage *msg, Packet **ppacket)
{
    return m_protocol->DecodeMessage(msg, ppacket);
}

error RtmpServer::SendAndFreeMessage(SharedPtrMessage *msg, int stream_id)
{
    return m_protocol->SendAndFreeMessage(msg, stream_id);
}

error RtmpServer::SendAndFreeMessages(SharedPtrMessage **msgs, int nb_msgs, int stream_id)
{
    return m_protocol->SendAndFreeMessages(msgs, nb_msgs, stream_id);
}

error RtmpServer::SendAndFreePacket(Packet *packet, int stream_id)
{
    return m_protocol->SendAndFreePacket(packet, stream_id);
}

error RtmpServer::Handshake()
{
    error err = SUCCESS;

    Assert(m_hsBytes);

    ComplexHandshake complex_hs;
    if ((err = complex_hs.HandshakeWithClient(m_hsBytes, m_io)) != SUCCESS) {
        if (ERRORCODE(err) == ERROR_RTMP_TRY_SIMPLE_HS) {
            Freep(err);

            SimpleHandshake simple_hs;
            if ((err = simple_hs.HandshakeWithClient(m_hsBytes, m_io)) != SUCCESS) {
                return ERRORWRAP(err, "simple handshake");
            }
        } else {
            return ERRORWRAP(err, "complex handshake");
        }
    }

    m_hsBytes->Dispose();

    return err;
}

error RtmpServer::ConnectApp(Request *req)
{
    error err = SUCCESS;

    CommonMessage* msg = NULL;
    ConnectAppPacket* pkt = NULL;
    if ((err = ExpectMessage<ConnectAppPacket>(&msg, &pkt)) != SUCCESS) {
        return ERRORWRAP(err, "expect connect app response");
    }
    AutoFree(CommonMessage, msg);
    AutoFree(ConnectAppPacket, pkt);

    Amf0Any* prop = NULL;

    if ((prop = pkt->m_commandObject->EnsurePropertyString("tcUrl")) == NULL) {
        return ERRORNEW(ERROR_RTMP_REQ_CONNECT, "invalid request without tcUrl");
    }
    req->m_tcUrl = prop->ToStr();

    if ((prop = pkt->m_commandObject->EnsurePropertyString("pageUrl")) != NULL) {
        req->m_pageUrl = prop->ToStr();
    }

    if ((prop = pkt->m_commandObject->EnsurePropertyString("swfUrl")) != NULL) {
        req->m_swfUrl = prop->ToStr();
    }

    if ((prop = pkt->m_commandObject->EnsurePropertyNumber("objectEncoding")) != NULL) {
        req->m_objectEncoding = prop->ToNumber();
    }

    if (pkt->m_args) {
        Freep(req->m_args);
        req->m_args = pkt->m_args->Copy()->ToObject();
    }

    DiscoveryTcUrl(req->m_tcUrl, req->m_schema, req->m_host, req->m_vhost, req->m_app, req->m_stream, req->m_port, req->m_param);
    req->Strip();

    return err;
}

error RtmpServer::SetWindowAckSize(int ack_size)
{
    error err = SUCCESS;

    SetWindowAckSizePacket* pkt = new SetWindowAckSizePacket();
    pkt->m_ackowledgementWindowSize = ack_size;
    if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send ack");
    }

    return err;
}

error RtmpServer::SetInWindowAckSize(int ack_size)
{
    return m_protocol->SetInWindowAckSize(ack_size);
}

error RtmpServer::SetPeerBandwidth(int bandwidth, int type)
{
    error err = SUCCESS;

    SetPeerBandwidthPacket* pkt = new SetPeerBandwidthPacket();
    pkt->m_bandwidth = bandwidth;
    pkt->m_type = type;
    if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send set peer bandwidth");
    }

    return err;
}

error RtmpServer::ResponseConnectApp(Request *req, const char *server_ip)
{
    error err = SUCCESS;

    ConnectAppResPacket* pkt = new ConnectAppResPacket();

    // @remark For windows, there must be a space between const string and macro.
    pkt->m_props->Set("fmsVer", Amf0Any::Str("FMS/" RTMP_SIG_FMS_VER));
    pkt->m_props->Set("capabilities", Amf0Any::Number(127));
    pkt->m_props->Set("mode", Amf0Any::Number(1));

    pkt->m_info->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
    pkt->m_info->Set(StatusCode, Amf0Any::Str(StatusCodeConnectSuccess));
    pkt->m_info->Set(StatusDescription, Amf0Any::Str("Connection succeeded"));
    pkt->m_info->Set("objectEncoding", Amf0Any::Number(req->m_objectEncoding));
    Amf0EcmaArray* data = Amf0Any::EcmaArray();
    pkt->m_info->Set("data", data);

    data->Set("version", Amf0Any::Str(RTMP_SIG_FMS_VER));
    data->Set("srs_sig", Amf0Any::Str(RTMP_SIG_KEY));
    data->Set("srs_server", Amf0Any::Str(RTMP_SIG_SERVER));
    data->Set("srs_license", Amf0Any::Str(RTMP_SIG_LICENSE));
    data->Set("srs_url", Amf0Any::Str(RTMP_SIG_URL));
    data->Set("srs_version", Amf0Any::Str(RTMP_SIG_VERSION));
    data->Set("srs_authors", Amf0Any::Str(RTMP_SIG_AUTHORS));

    if (server_ip) {
        data->Set("srs_server_ip", Amf0Any::Str(server_ip));
    }
    // for edge to directly get the id of client.
    data->Set("srs_pid", Amf0Any::Number(getpid()));
    data->Set("srs_id", Amf0Any::Str(Context->GetId().Cstr()));

    if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send connect app response");
    }

    return err;
}

#define RTMP_REDIRECT_TIMEOUT (3 * UTIME_SECONDS)

error RtmpServer::Redirect(Request *r, std::string url, bool &accepted)
{
    error err = SUCCESS;

    if (true) {
        Amf0Object* ex = Amf0Any::Object();
        ex->Set("code", Amf0Any::Number(302));

        // The redirect is tcUrl while redirect2 is RTMP URL.
        // https://github.com/ossrs/srs/issues/1575#issuecomment-574999798
        std::string tcUrl = PathDirname(url);
        ex->Set("redirect", Amf0Any::Str(tcUrl.c_str()));
        ex->Set("redirect2", Amf0Any::Str(url.c_str()));

        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelError));
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeConnectRejected));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("RTMP 302 Redirect"));
        pkt->m_data->Set("ex", ex);

        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send redirect/reject");
        }
    }

    // client must response a call message.
    // or we never know whether the client is ok to redirect.
    m_protocol->SetRecvTimeout(RTMP_REDIRECT_TIMEOUT);
    if (true) {
        CommonMessage* msg = NULL;
        CallPacket* pkt = NULL;
        if ((err = ExpectMessage<CallPacket>(&msg, &pkt)) != SUCCESS) {
            Freep(err);
            // ignore any error of redirect response.
            return SUCCESS;
        }
        AutoFree(CommonMessage, msg);
        AutoFree(CallPacket, pkt);

        std::string message;
        if (pkt->m_arguments && pkt->m_arguments->IsString()) {
            message = pkt->m_arguments->ToStr();
            accepted = true;
        }
    }

    return err;
}

void RtmpServer::ResponseConnectReject(Request *req, const char *desc)
{
    error err = SUCCESS;

    OnStatusCallPacket* pkt = new OnStatusCallPacket();
    pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelError));
    pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeConnectRejected));
    pkt->m_data->Set(StatusDescription, Amf0Any::Str(desc));

    if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
        warn("send reject response err %s", ERRORDESC(err).c_str());
        Freep(err);
    }

    return;
}

error RtmpServer::OnBwDone()
{
    error err = SUCCESS;

    OnBWDonePacket* pkt = new OnBWDonePacket();
    if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send onBWDone");
    }

    return err;
}

error RtmpServer::IdentifyClient(int stream_id, RtmpConnType &type, std::string &stream_name, utime_t &duration)
{
    type = RtmpConnUnknown;
    error err = SUCCESS;

    while (true) {
        CommonMessage* msg = NULL;
        if ((err = m_protocol->RecvMessage(&msg)) != SUCCESS) {
            return ERRORWRAP(err, "recv identify message");
        }

        AutoFree(CommonMessage, msg);
        MessageHeader& h = msg->m_header;

        if (h.IsAckledgement() || h.IsSetChunkSize() || h.IsWindowAckledgementSize() || h.IsUserControlMessage()) {
            continue;
        }

        if (!h.IsAmf0Command() && !h.IsAmf3Command()) {
            trace("ignore message type=%#x", h.m_messageType);
            continue;
        }

        Packet* pkt = NULL;
        if ((err = m_protocol->DecodeMessage(msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "decode identify");
        }

        AutoFree(Packet, pkt);

        if (dynamic_cast<CreateStreamPacket*>(pkt)) {
            return IdentifyCreateStreamClient(dynamic_cast<CreateStreamPacket*>(pkt), stream_id, 3, type, stream_name, duration);
        }
        if (dynamic_cast<FMLEStartPacket*>(pkt)) {
            return IdentifyFmlePublishClient(dynamic_cast<FMLEStartPacket*>(pkt), type, stream_name);
        }
        if (dynamic_cast<PlayPacket*>(pkt)) {
            return IdentifyPlayClient(dynamic_cast<PlayPacket*>(pkt), type, stream_name, duration);
        }

        // call msg,
        // support response null first,
        // @see https://github.com/ossrs/srs/issues/106
        // TODO: FIXME: response in right way, or forward in edge mode.
        CallPacket* call = dynamic_cast<CallPacket*>(pkt);
        if (call) {
            CallResPacket* res = new CallResPacket(call->m_transactionId);
            res->m_commandObject = Amf0Any::Null();
            res->m_response = Amf0Any::Null();
            if ((err = m_protocol->SendAndFreePacket(res, 0)) != SUCCESS) {
                return ERRORWRAP(err, "response call");
            }

            // For encoder of Haivision, it always send a _checkbw call message.
            // @remark the next message is createStream, so we continue to identify it.
            // @see https://github.com/ossrs/srs/issues/844
            if (call->m_commandName == "_checkbw") {
                continue;
            }
            continue;
        }

        trace("ignore AMF0/AMF3 command message.");
    }

    return err;
}

error RtmpServer::SetChunkSize(int chunk_size)
{
    error err = SUCCESS;

    SetChunkSizePacket* pkt = new SetChunkSizePacket();
    pkt->m_chunkSize = chunk_size;
    if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
        return ERRORWRAP(err, "send set chunk size");
    }

    return err;
}

error RtmpServer::StartPlay(int stream_id)
{
    error err = SUCCESS;

    // StreamBegin
    if (true) {
        UserControlPacket* pkt = new UserControlPacket();
        pkt->m_eventType = SrcPCUCStreamBegin;
        pkt->m_eventData = stream_id;
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send StreamBegin");
        }
    }

    // onStatus(NetStream.Play.Reset)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeStreamReset));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Playing and resetting stream."));
        pkt->m_data->Set(StatusDetails, Amf0Any::Str("stream"));
        pkt->m_data->Set(StatusClientId, Amf0Any::Str(RTMP_SIG_CLIENT_ID));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Play.Reset");
        }
    }

    // onStatus(NetStream.Play.Start)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeStreamStart));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Started playing stream."));
        pkt->m_data->Set(StatusDetails, Amf0Any::Str("stream"));
        pkt->m_data->Set(StatusClientId, Amf0Any::Str(RTMP_SIG_CLIENT_ID));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Play.Start");
        }
    }

    // |RtmpSampleAccess(false, false)
    if (true) {
        SampleAccessPacket* pkt = new SampleAccessPacket();

        // allow audio/video sample.
        // @see: https://github.com/ossrs/srs/issues/49
        pkt->m_audioSampleAccess = true;
        pkt->m_videoSampleAccess = true;

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send |RtmpSampleAccess true");
        }
    }

    // onStatus(NetStream.Data.Start)
    if (true) {
        OnStatusDataPacket* pkt = new OnStatusDataPacket();
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeDataStart));
        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Data.Start");
        }
    }

    return err;
}

error RtmpServer::OnPlayClientPause(int stream_id, bool is_pause)
{
    error err = SUCCESS;

    if (is_pause) {
        // onStatus(NetStream.Pause.Notify)
        if (true) {
            OnStatusCallPacket* pkt = new OnStatusCallPacket();

            pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
            pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeStreamPause));
            pkt->m_data->Set(StatusDescription, Amf0Any::Str("Paused stream."));

            if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
                return ERRORWRAP(err, "send NetStream.Pause.Notify");
            }
        }
        // StreamEOF
        if (true) {
            UserControlPacket* pkt = new UserControlPacket();

            pkt->m_eventType = SrcPCUCStreamEOF;
            pkt->m_eventData = stream_id;

            if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
                return ERRORWRAP(err, "send StreamEOF");
            }
        }
    } else {
        // onStatus(NetStream.Unpause.Notify)
        if (true) {
            OnStatusCallPacket* pkt = new OnStatusCallPacket();

            pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
            pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeStreamUnpause));
            pkt->m_data->Set(StatusDescription, Amf0Any::Str("Unpaused stream."));

            if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
                return ERRORWRAP(err, "send NetStream.Unpause.Notify");
            }
        }
        // StreamBegin
        if (true) {
            UserControlPacket* pkt = new UserControlPacket();

            pkt->m_eventType = SrcPCUCStreamBegin;
            pkt->m_eventData = stream_id;

            if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
                return ERRORWRAP(err, "send StreamBegin");
            }
        }
    }

    return err;
}

error RtmpServer::StartFmlePublish(int stream_id)
{
    error err = SUCCESS;

    // FCPublish
    double fc_publish_tid = 0;
    if (true) {
        CommonMessage* msg = NULL;
        FMLEStartPacket* pkt = NULL;
        if ((err = ExpectMessage<FMLEStartPacket>(&msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "recv FCPublish");
        }

        AutoFree(CommonMessage, msg);
        AutoFree(FMLEStartPacket, pkt);

        fc_publish_tid = pkt->m_transactionId;
    }
    // FCPublish response
    if (true) {
        FMLEStartResPacket* pkt = new FMLEStartResPacket(fc_publish_tid);
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send FCPublish response");
        }
    }

    // createStream
    double create_stream_tid = 0;
    if (true) {
        CommonMessage* msg = NULL;
        CreateStreamPacket* pkt = NULL;
        if ((err = ExpectMessage<CreateStreamPacket>(&msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "recv createStream");
        }

        AutoFree(CommonMessage, msg);
        AutoFree(CreateStreamPacket, pkt);

        create_stream_tid = pkt->m_transactionId;
    }
    // createStream response
    if (true) {
        CreateStreamResPacket* pkt = new CreateStreamResPacket(create_stream_tid, stream_id);
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send createStream response");
        }
    }

    // publish
    if (true) {
        CommonMessage* msg = NULL;
        PublishPacket* pkt = NULL;
        if ((err = ExpectMessage<PublishPacket>(&msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "recv publish");
        }

        AutoFree(CommonMessage, msg);
        AutoFree(PublishPacket, pkt);
    }
    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_commandName = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodePublishStart));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Started publishing stream."));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Publish.Start");
        }
    }
    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodePublishStart));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Started publishing stream."));
        pkt->m_data->Set(StatusClientId, Amf0Any::Str(RTMP_SIG_CLIENT_ID));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Publish.Start");
        }
    }

    return err;
}

error RtmpServer::StartHaivisionPublish(int stream_id)
{
    error err = SUCCESS;

    // publish
    if (true) {
        CommonMessage* msg = NULL;
        PublishPacket* pkt = NULL;
        if ((err = ExpectMessage<PublishPacket>(&msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "recv publish");
        }

        AutoFree(CommonMessage, msg);
        AutoFree(PublishPacket, pkt);
    }

    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_commandName = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodePublishStart));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Started publishing stream."));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Publish.Start");
        }
    }

    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodePublishStart));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Started publishing stream."));
        pkt->m_data->Set(StatusClientId, Amf0Any::Str(RTMP_SIG_CLIENT_ID));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Publish.Start");
        }
    }

    return err;
}

error RtmpServer::FmleUnpublish(int stream_id, double unpublish_tid)
{
    error err =SUCCESS;

    // publish response onFCUnpublish(NetStream.unpublish.Success)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_commandName = RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH;
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeUnpublishSuccess));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Stop publishing stream."));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.unpublish.Success");
        }
    }
    // FCUnpublish response
    if (true) {
        FMLEStartResPacket* pkt = new FMLEStartResPacket(unpublish_tid);
        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send FCUnpublish response");
        }
    }
    // publish response onStatus(NetStream.Unpublish.Success)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodeUnpublishSuccess));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Stream is now unpublished"));
        pkt->m_data->Set(StatusClientId, Amf0Any::Str(RTMP_SIG_CLIENT_ID));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Unpublish.Success");
        }
    }

    return err;
}

error RtmpServer::StartFlashPublish(int stream_id)
{
    error err = SUCCESS;

    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        OnStatusCallPacket* pkt = new OnStatusCallPacket();

        pkt->m_data->Set(StatusLevel, Amf0Any::Str(StatusLevelStatus));
        pkt->m_data->Set(StatusCode, Amf0Any::Str(StatusCodePublishStart));
        pkt->m_data->Set(StatusDescription, Amf0Any::Str("Started publishing stream."));
        pkt->m_data->Set(StatusClientId, Amf0Any::Str(RTMP_SIG_CLIENT_ID));

        if ((err = m_protocol->SendAndFreePacket(pkt, stream_id)) != SUCCESS) {
            return ERRORWRAP(err, "send NetStream.Publish.Start");
        }
    }

    return err;
}

error RtmpServer::IdentifyCreateStreamClient(CreateStreamPacket *req, int stream_id, int depth, RtmpConnType &type, std::string &stream_name, utime_t &duration)
{
    error err = SUCCESS;

    if (depth <= 0) {
        return ERRORNEW(ERROR_RTMP_CREATE_STREAM_DEPTH, "create stream recursive depth");
    }

    if (true) {
        CreateStreamResPacket* pkt = new CreateStreamResPacket(req->m_transactionId, stream_id);
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send createStream response");
        }
    }

    while (true) {
        CommonMessage* msg = NULL;
        if ((err = m_protocol->RecvMessage(&msg)) != SUCCESS) {
            return ERRORWRAP(err, "recv identify");
        }

        AutoFree(CommonMessage, msg);
        MessageHeader& h = msg->m_header;

        if (h.IsAckledgement() || h.IsSetChunkSize() || h.IsWindowAckledgementSize() || h.IsUserControlMessage()) {
            continue;
        }

        if (!h.IsAmf0Command() && !h.IsAmf3Command()) {
            trace("ignore message type=%#x", h.m_messageType);
            continue;
        }

        Packet* pkt = NULL;
        if ((err = m_protocol->DecodeMessage(msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "decode identify");
        }

        AutoFree(Packet, pkt);

        if (dynamic_cast<PlayPacket*>(pkt)) {
            return IdentifyPlayClient(dynamic_cast<PlayPacket*>(pkt), type, stream_name, duration);
        }
        if (dynamic_cast<PublishPacket*>(pkt)) {
            return IdentifyFlashPublishClient(dynamic_cast<PublishPacket*>(pkt), type, stream_name);
        }
        if (dynamic_cast<CreateStreamPacket*>(pkt)) {
            return IdentifyCreateStreamClient(dynamic_cast<CreateStreamPacket*>(pkt), stream_id, depth-1, type, stream_name, duration);
        }
        if (dynamic_cast<FMLEStartPacket*>(pkt)) {
            return IdentifyHaivisionPublishClient(dynamic_cast<FMLEStartPacket*>(pkt), type, stream_name);
        }

        trace("ignore AMF0/AMF3 command message.");
    }

    return err;
}

error RtmpServer::IdentifyFmlePublishClient(FMLEStartPacket *req, RtmpConnType &type, std::string &stream_name)
{
    error err = SUCCESS;

    type = RtmpConnFMLEPublish;
    stream_name = req->m_streamName;

    // releaseStream response
    if (true) {
        FMLEStartResPacket* pkt = new FMLEStartResPacket(req->m_transactionId);
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send releaseStream response");
        }
    }

    return err;
}

error RtmpServer::IdentifyHaivisionPublishClient(FMLEStartPacket *req, RtmpConnType &type, std::string &stream_name)
{
    error err = SUCCESS;

    type = RtmpConnHaivisionPublish;
    stream_name = req->m_streamName;

    // FCPublish response
    if (true) {
        FMLEStartResPacket* pkt = new FMLEStartResPacket(req->m_transactionId);
        if ((err = m_protocol->SendAndFreePacket(pkt, 0)) != SUCCESS) {
            return ERRORWRAP(err, "send FCPublish");
        }
    }

    return err;
}

error RtmpServer::IdentifyFlashPublishClient(PublishPacket *req, RtmpConnType &type, std::string &stream_name)
{
    type = RtmpConnFlashPublish;
    stream_name = req->m_streamName;

    return SUCCESS;
}

error RtmpServer::IdentifyPlayClient(PlayPacket *req, RtmpConnType &type, std::string &stream_name, utime_t &duration)
{
    type = RtmpConnPlay;
    stream_name = req->m_streamName;
    duration = utime_t(req->m_duration) * UTIME_MILLISECONDS;

    return SUCCESS;
}

ConnectAppPacket::ConnectAppPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_CONNECT;
    m_transactionId = 1;
    m_commandObject = Amf0Any::Object();
    // optional
    m_args = NULL;
}

ConnectAppPacket::~ConnectAppPacket()
{
    Freep(m_commandObject);
    Freep(m_args);
}

error ConnectAppPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_CONNECT) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    // some client donot send id=1.0, so we only warn user if not match.
    if (m_transactionId != 1.0) {
        warn("invalid transaction_id=%.2f", m_transactionId);
    }

    if ((err = m_commandObject->Read(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if (!stream->Empty()) {
        Freep(m_args);

        // see: https://github.com/ossrs/srs/issues/186
        // the args maybe any amf0, for instance, a string. we should drop if not object.
        Amf0Any* any = NULL;
        if ((err = Amf0Any::Discovery(stream, &any)) != SUCCESS) {
            return ERRORWRAP(err, "args");
        }
        Assert(any);

        // read the instance
        if ((err = any->Read(stream)) != SUCCESS) {
            Freep(any);
            return ERRORWRAP(err, "args");
        }

        // drop when not an AMF0 object.
        if (!any->IsObject()) {
            warn("drop the args, see: '4.1.1. connect', marker=%#x", (uint8_t)any->m_marker);
            Freep(any);
        } else {
            m_args = any->ToObject();
        }
    }

    return err;
}

int ConnectAppPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int ConnectAppPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int ConnectAppPacket::GetSize()
{
    int size = 0;

    size += Amf0Size::Str(m_commandName);
    size += Amf0Size::Number();
    size += Amf0Size::Object(m_commandObject);
    if (m_args) {
        size += Amf0Size::Object(m_args);
    }

    return size;
}

error ConnectAppPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = m_commandObject->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if (m_args && (err = m_args->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "args");
    }

    return err;
}

ConnectAppResPacket::ConnectAppResPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_RESULT;
    m_transactionId = 1;
    m_props = Amf0Any::Object();
    m_info = Amf0Any::Object();
}

ConnectAppResPacket::~ConnectAppResPacket()
{
    Freep(m_props);
    Freep(m_info);
}

error ConnectAppResPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_RESULT) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    // some client donot send id=1.0, so we only warn user if not match.
    if (m_transactionId != 1.0) {
        warn("invalid transaction_id=%.2f", m_transactionId);
    }

    // for RED5(1.0.6), the props is NULL, we must ignore it.
    // @see https://github.com/ossrs/srs/issues/418
    if (!stream->Empty()) {
        Amf0Any* p = NULL;
        if ((err = Amf0ReadAny(stream, &p)) != SUCCESS) {
            return ERRORWRAP(err, "args");
        }

        // ignore when props is not amf0 object.
        if (!p->IsObject()) {
            warn("ignore connect response props marker=%#x.", (uint8_t)p->m_marker);
            Freep(p);
        } else {
            Freep(m_props);
            m_props = p->ToObject();
        }
    }

    if ((err = m_info->Read(stream)) != SUCCESS) {
        return ERRORWRAP(err, "args");
    }

    return err;
}

int ConnectAppResPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int ConnectAppResPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int ConnectAppResPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Object(m_props) + Amf0Size::Object(m_info);
}

error ConnectAppResPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = m_props->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "props");
    }

    if ((err = m_info->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "info");
    }

    return err;
}

CallPacket::CallPacket()
{
    m_commandName = "";
    m_transactionId = 0;
    m_commandObject = NULL;
    m_arguments = NULL;
}

CallPacket::~CallPacket()
{
    Freep(m_commandObject);
    Freep(m_arguments);
}

error CallPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty()) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "empty command_name");
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    Freep(m_commandObject);
    if ((err = Amf0Any::Discovery(stream, &m_commandObject)) != SUCCESS) {
        return ERRORWRAP(err, "discovery m_commandObject");
    }
    if ((err = m_commandObject->Read(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if (!stream->Empty()) {
        Freep(m_arguments);
        if ((err = Amf0Any::Discovery(stream, &m_arguments)) != SUCCESS) {
            return ERRORWRAP(err, "discovery args");
        }
        if ((err = m_arguments->Read(stream)) != SUCCESS) {
            return ERRORWRAP(err, "read args");
        }
    }

    return err;
}

int CallPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int CallPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int CallPacket::GetSize()
{
    int size = 0;

    size += Amf0Size::Str(m_commandName) + Amf0Size::Number();

    if (m_commandObject) {
        size += m_commandObject->TotalSize();
    }

    if (m_arguments) {
        size += m_arguments->TotalSize();
    }

    return size;
}

error CallPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if (m_commandObject && (err = m_commandObject->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if (m_arguments && (err = m_arguments->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "args");
    }

    return err;
}

CallResPacket::CallResPacket(double _transaction_id)
{
    m_commandName = RTMP_AMF0_COMMAND_RESULT;
    _transaction_id = _transaction_id;
    m_commandObject = NULL;
    m_response = NULL;
}

CallResPacket::~CallResPacket()
{
    Freep(m_commandObject);
    Freep(m_response);
}

int CallResPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int CallResPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int CallResPacket::GetSize()
{
    int size = 0;

    size += Amf0Size::Str(m_commandName) + Amf0Size::Number();

    if (m_commandObject) {
        size += m_commandObject->TotalSize();
    }

    if (m_response) {
        size += m_response->TotalSize();
    }

    return size;
}

error CallResPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if (m_commandObject && (err = m_commandObject->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if (m_response && (err = m_response->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "response");
    }

    return err;
}

CreateStreamPacket::CreateStreamPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_CREATE_STREAM;
    m_transactionId = 2;
    m_commandObject = Amf0Any::Null();
}

CreateStreamPacket::~CreateStreamPacket()
{
    Freep(m_commandObject);
}

error CreateStreamPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_CREATE_STREAM) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    return err;
}

int CreateStreamPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int CreateStreamPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int CreateStreamPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null();
}

error CreateStreamPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    return err;
}

CreateStreamResPacket::CreateStreamResPacket(double _transaction_id, double _stream_id)
{
    m_commandName = RTMP_AMF0_COMMAND_RESULT;
    m_transactionId = _transaction_id;
    m_commandObject = Amf0Any::Null();
    m_streamId = _stream_id;
}

CreateStreamResPacket::~CreateStreamResPacket()
{
    Freep(m_commandObject);
}

error CreateStreamResPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_RESULT) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0ReadNumber(stream, m_streamId)) != SUCCESS) {
        return ERRORWRAP(err, "stream_id");
    }

    return err;
}

int CreateStreamResPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int CreateStreamResPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int CreateStreamResPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null() + Amf0Size::Number();
}

error CreateStreamResPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0WriteNumber(stream, m_streamId)) != SUCCESS) {
        return ERRORWRAP(err, "stream_id");
    }

    return err;
}

CloseStreamPacket::CloseStreamPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_CLOSE_STREAM;
    m_transactionId = 0;
    m_commandObject = Amf0Any::Null();
}

CloseStreamPacket::~CloseStreamPacket()
{
    Freep(m_commandObject);
}

error CloseStreamPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    return err;
}

FMLEStartPacket::FMLEStartPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_RELEASE_STREAM;
    m_transactionId = 0;
    m_commandObject = Amf0Any::Null();
}

FMLEStartPacket::~FMLEStartPacket()
{
    Freep(m_commandObject);
}

error FMLEStartPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    bool invalid_command_name = (m_commandName != RTMP_AMF0_COMMAND_RELEASE_STREAM
        && m_commandName != RTMP_AMF0_COMMAND_FC_PUBLISH && m_commandName != RTMP_AMF0_COMMAND_UNPUBLISH);
    if (m_commandName.empty() || invalid_command_name) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0ReadString(stream, m_streamName)) != SUCCESS) {
        return ERRORWRAP(err, "stream_name");
    }

    return err;
}

int FMLEStartPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int FMLEStartPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int FMLEStartPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null() + Amf0Size::Str(m_streamName);
}

error FMLEStartPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0WriteString(stream, m_streamName)) != SUCCESS) {
        return ERRORWRAP(err, "stream_name");
    }

    return err;
}

FMLEStartPacket *FMLEStartPacket::CreateReleaseStream(std::string stream)
{
    FMLEStartPacket* pkt = new FMLEStartPacket();

    pkt->m_commandName = RTMP_AMF0_COMMAND_RELEASE_STREAM;
    pkt->m_transactionId = 2;
    pkt->m_streamName = stream;

    return pkt;
}

FMLEStartPacket *FMLEStartPacket::CreateFCPublish(std::string stream)
{
    FMLEStartPacket* pkt = new FMLEStartPacket();

    pkt->m_commandName = RTMP_AMF0_COMMAND_FC_PUBLISH;
    pkt->m_transactionId = 3;
    pkt->m_streamName = stream;

    return pkt;
}

FMLEStartResPacket::FMLEStartResPacket(double _transaction_id)
{
    m_commandName = RTMP_AMF0_COMMAND_RESULT;
    m_transactionId = _transaction_id;
    m_commandObject = Amf0Any::Null();
    m_args = Amf0Any::Undefined();
}

FMLEStartResPacket::~FMLEStartResPacket()
{
    Freep(m_commandObject);
    Freep(m_args);
}

error FMLEStartResPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_RESULT) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0ReadUndefined(stream)) != SUCCESS) {
        return ERRORWRAP(err, "stream_id");
    }

    return err;
}

int FMLEStartResPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int FMLEStartResPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int FMLEStartResPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null() + Amf0Size::Undefined();
}

error FMLEStartResPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0WriteUndefined(stream)) != SUCCESS) {
        return ERRORWRAP(err, "args");
    }

    return err;
}

PublishPacket::PublishPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_PUBLISH;
    m_transactionId = 0;
    m_commandObject = Amf0Any::Null();
    m_type = "live";
}

PublishPacket::~PublishPacket()
{
    Freep(m_commandObject);
}

error PublishPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_PUBLISH) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0ReadString(stream, m_streamName)) != SUCCESS) {
        return ERRORWRAP(err, "stream_name");
    }

    if (!stream->Empty() && (err = Amf0ReadString(stream, m_type)) != SUCCESS) {
        return ERRORWRAP(err, "publish type");
    }

    return err;
}

int PublishPacket::GetPreferCid()
{
    return RTMP_CID_OverStream;
}

int PublishPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int PublishPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null() + Amf0Size::Str(m_streamName)
    + Amf0Size::Str(m_type);
}

error PublishPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0WriteString(stream, m_streamName)) != SUCCESS) {
        return ERRORWRAP(err, "stream_name");
    }

    if ((err = Amf0WriteString(stream, m_type)) != SUCCESS) {
        return ERRORWRAP(err, "type");
    }

    return err;
}

PausePacket::PausePacket()
{
    m_commandName = RTMP_AMF0_COMMAND_PAUSE;
    m_transactionId = 0;
    m_commandObject = Amf0Any::Null();

    m_timeMs = 0;
    m_isPause = true;
}

PausePacket::~PausePacket()
{
    Freep(m_commandObject);
}

error PausePacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_PAUSE) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0ReadBoolean(stream, m_isPause)) != SUCCESS) {
        return ERRORWRAP(err, "is_pause");
    }

    if ((err = Amf0ReadNumber(stream, m_timeMs)) != SUCCESS) {
        return ERRORWRAP(err, "time");
    }

    return err;
}

PlayPacket::PlayPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_PLAY;
    m_transactionId = 0;
    m_commandObject = Amf0Any::Null();

    m_start = -2;
    m_duration = -1;
    m_reset = true;
}

PlayPacket::~PlayPacket()
{
    Freep(m_commandObject);
}

error PlayPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }
    if (m_commandName.empty() || m_commandName != RTMP_AMF0_COMMAND_PLAY) {
        return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", m_commandName.c_str());
    }

    if ((err = Amf0ReadNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0ReadNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0ReadString(stream, m_streamName)) != SUCCESS) {
        return ERRORWRAP(err, "stream_name");
    }

    if (!stream->Empty() && (err = Amf0ReadNumber(stream, m_start)) != SUCCESS) {
        return ERRORWRAP(err, "start");
    }
    if (!stream->Empty() && (err = Amf0ReadNumber(stream, m_duration)) != SUCCESS) {
        return ERRORWRAP(err, "duration");
    }

    if (stream->Empty()) {
        return err;
    }

    Amf0Any* reset_value = NULL;
    if ((err = Amf0ReadAny(stream, &reset_value)) != SUCCESS) {
        return ERRORWRAP(err, "reset");
    }
    AutoFree(Amf0Any, reset_value);

    if (reset_value) {
        // check if the value is bool or number
        // An optional Boolean value or number that specifies whether
        // to flush any previous playlist
        if (reset_value->IsBoolean()) {
            m_reset = reset_value->ToBoolean();
        } else if (reset_value->IsNumber()) {
            m_reset = (reset_value->ToNumber() != 0);
        } else {
            return ERRORNEW(ERROR_RTMP_AMF0_DECODE, "invalid marker=%#x", (uint8_t)reset_value->m_marker);
        }
    }

    return err;
}

int PlayPacket::GetPreferCid()
{
    return RTMP_CID_OverStream;
}

int PlayPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int PlayPacket::GetSize()
{
    int size = Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null() + Amf0Size::Str(m_streamName);

    if (m_start != -2 || m_duration != -1 || !m_reset) {
        size += Amf0Size::Number();
    }

    if (m_duration != -1 || !m_reset) {
        size += Amf0Size::Number();
    }

    if (!m_reset) {
        size += Amf0Size::Boolean();
    }

    return size;
}

error PlayPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = Amf0WriteString(stream, m_streamName)) != SUCCESS) {
        return ERRORWRAP(err, "stream_name");
    }

    if ((m_start != -2 || m_duration != -1 || !m_reset) && (err = Amf0WriteNumber(stream, m_start)) != SUCCESS) {
        return ERRORWRAP(err, "start");
    }

    if ((m_duration != -1 || !m_reset) && (err = Amf0WriteNumber(stream, m_duration)) != SUCCESS) {
        return ERRORWRAP(err, "duration");
    }

    if (!m_reset && (err = Amf0WriteBoolean(stream, m_reset)) != SUCCESS) {
        return ERRORWRAP(err, "reset");
    }

    return err;
}

PlayResPacket::PlayResPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_RESULT;
    m_transactionId = 0;
    m_commandObject = Amf0Any::Null();
    m_desc = Amf0Any::Object();
}

PlayResPacket::~PlayResPacket()
{
    Freep(m_commandObject);
    Freep(m_desc);
}

int PlayResPacket::GetPreferCid()
{
    return RTMP_CID_OverStream;
}

int PlayResPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int PlayResPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null() + Amf0Size::Object(m_desc);
}

error PlayResPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "command_object");
    }

    if ((err = m_desc->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "desc");
    }

    return err;
}

OnBWDonePacket::OnBWDonePacket()
{
    m_commandName = RTMP_AMF0_COMMAND_ON_BW_DONE;
    m_transactionId = 0;
    m_args = Amf0Any::Null();
}

OnBWDonePacket::~OnBWDonePacket()
{
    Freep(m_args);
}

int OnBWDonePacket::GetPreferCid()
{
    return RTMP_CID_OverConnection;
}

int OnBWDonePacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int OnBWDonePacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null();
}

error OnBWDonePacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "args");
    }

    return err;
}

OnStatusCallPacket::OnStatusCallPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_ON_STATUS;
    m_transactionId = 0;
    m_args = Amf0Any::Null();
    m_data = Amf0Any::Object();
}

OnStatusCallPacket::~OnStatusCallPacket()
{
    Freep(m_args);
    Freep(m_data);
}

int OnStatusCallPacket::GetPreferCid()
{
    return RTMP_CID_OverStream;
}

int OnStatusCallPacket::GetMessageType()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int OnStatusCallPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Number()
    + Amf0Size::Null() + Amf0Size::Object(m_data);
}

error OnStatusCallPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteNumber(stream, m_transactionId)) != SUCCESS) {
        return ERRORWRAP(err, "transaction_id");
    }

    if ((err = Amf0WriteNull(stream)) != SUCCESS) {
        return ERRORWRAP(err, "args");
    }

    if ((err = m_data->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "data");
    }

    return err;
}

OnStatusDataPacket::OnStatusDataPacket()
{
    m_commandName = RTMP_AMF0_COMMAND_ON_STATUS;
    m_data = Amf0Any::Object();
}

OnStatusDataPacket::~OnStatusDataPacket()
{
    Freep(m_data);
}

int OnStatusDataPacket::GetPreferCid()
{
    return RTMP_CID_OverStream;
}

int OnStatusDataPacket::GetMessageType()
{
    return RTMP_MSG_AMF0DataMessage;
}

int OnStatusDataPacket::GetSize()
{
    return Amf0Size::Str(m_commandName) + Amf0Size::Object(m_data);
}

error OnStatusDataPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = m_data->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "data");
    }

    return err;
}

SampleAccessPacket::SampleAccessPacket()
{
    m_commandName = RTMP_AMF0_DATA_SAMPLE_ACCESS;
    m_videoSampleAccess = false;
    m_audioSampleAccess = false;
}

SampleAccessPacket::~SampleAccessPacket()
{

}

int SampleAccessPacket::GetPreferCid()
{
    return RTMP_CID_OverStream;
}

int SampleAccessPacket::GetMessageType()
{
    return RTMP_MSG_AMF0DataMessage;
}

int SampleAccessPacket::GetSize()
{
    return Amf0Size::Str(m_commandName)
    + Amf0Size::Boolean() + Amf0Size::Boolean();
}

error SampleAccessPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_commandName)) != SUCCESS) {
        return ERRORWRAP(err, "command_name");
    }

    if ((err = Amf0WriteBoolean(stream, m_videoSampleAccess)) != SUCCESS) {
        return ERRORWRAP(err, "video sample access");
    }

    if ((err = Amf0WriteBoolean(stream, m_audioSampleAccess)) != SUCCESS) {
        return ERRORWRAP(err, "audio sample access");
    }

    return err;
}

OnMetaDataPacket::OnMetaDataPacket()
{
    m_name = CONSTS_RTMP_ON_METADATA;
    m_metadata = Amf0Any::Object();
}

OnMetaDataPacket::~OnMetaDataPacket()
{
    Freep(m_metadata);
}

error OnMetaDataPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0ReadString(stream, m_name)) != SUCCESS) {
        return ERRORWRAP(err, "name");
    }

    // ignore the @setDataFrame
    if (m_name == CONSTS_RTMP_SET_DATAFRAME) {
        if ((err = Amf0ReadString(stream, m_name)) != SUCCESS) {
            return ERRORWRAP(err, "name");
        }
    }

    // Allows empty body metadata.
    if (stream->Empty()) {
        return err;
    }

    // the metadata maybe object or ecma array
    Amf0Any* any = NULL;
    if ((err = Amf0ReadAny(stream, &any)) != SUCCESS) {
        return ERRORWRAP(err, "metadata");
    }

    Assert(any);
    if (any->IsObject()) {
        Freep(m_metadata);
        m_metadata = any->ToObject();
        return err;
    }

    AutoFree(Amf0Any, any);

    if (any->IsEcmaArray()) {
        Amf0EcmaArray* arr = any->ToEcmaArray();

        // if ecma array, copy to object.
        for (int i = 0; i < arr->Count(); i++) {
            m_metadata->Set(arr->KeyAt(i), arr->ValueAt(i)->Copy());
        }
    }

    return err;
}

int OnMetaDataPacket::GetPreferCid()
{
    return RTMP_CID_OverConnection2;
}

int OnMetaDataPacket::GetMessageType()
{
    return RTMP_MSG_AMF0DataMessage;
}

int OnMetaDataPacket::GetSize()
{
    return Amf0Size::Str(m_name) + Amf0Size::Object(m_metadata);
}

error OnMetaDataPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if ((err = Amf0WriteString(stream, m_name)) != SUCCESS) {
        return ERRORWRAP(err, "name");
    }

    if ((err = m_metadata->Write(stream)) != SUCCESS) {
        return ERRORWRAP(err, "metadata");
    }

    return err;
}

SetWindowAckSizePacket::SetWindowAckSizePacket()
{
    m_ackowledgementWindowSize = 0;
}

SetWindowAckSizePacket::~SetWindowAckSizePacket()
{

}

error SetWindowAckSizePacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->Remain());
    }

    m_ackowledgementWindowSize = stream->Read4Bytes();

    return err;
}

int SetWindowAckSizePacket::GetPreferCid()
{
    return RTMP_CID_ProtocolControl;
}

int SetWindowAckSizePacket::GetMessageType()
{
    return RTMP_MSG_WindowAcknowledgementSize;
}

int SetWindowAckSizePacket::GetSize()
{
    return 4;
}

error SetWindowAckSizePacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->Remain());
    }

    stream->Write4Bytes(m_ackowledgementWindowSize);

    return err;
}

AcknowledgementPacket::AcknowledgementPacket()
{
    m_sequenceNumber = 0;
}

AcknowledgementPacket::~AcknowledgementPacket()
{

}

error AcknowledgementPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->Remain());
    }

    m_sequenceNumber = (uint32_t)stream->Read4Bytes();

    return err;
}

int AcknowledgementPacket::GetPreferCid()
{
    return RTMP_CID_ProtocolControl;
}

int AcknowledgementPacket::GetMessageType()
{
    return RTMP_MSG_Acknowledgement;
}

int AcknowledgementPacket::GetSize()
{
    return 4;
}

error AcknowledgementPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->Remain());
    }

    stream->Write4Bytes(m_sequenceNumber);

    return err;
}

SetChunkSizePacket::SetChunkSizePacket()
{
    m_chunkSize = CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
}

SetChunkSizePacket::~SetChunkSizePacket()
{

}

error SetChunkSizePacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->Remain());
    }

    m_chunkSize = stream->Read4Bytes();

    return err;
}

int SetChunkSizePacket::GetPreferCid()
{
    return RTMP_CID_ProtocolControl;
}

int SetChunkSizePacket::GetMessageType()
{
    return RTMP_MSG_SetChunkSize;
}

int SetChunkSizePacket::GetSize()
{
    return 4;
}

error SetChunkSizePacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(4)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->Remain());
    }

    stream->Write4Bytes(m_chunkSize);

    return err;
}

SetPeerBandwidthPacket::SetPeerBandwidthPacket()
{
    m_bandwidth = 0;
    m_type = PeerBandwidthDynamic;
}

SetPeerBandwidthPacket::~SetPeerBandwidthPacket()
{

}

int SetPeerBandwidthPacket::GetPreferCid()
{
    return RTMP_CID_ProtocolControl;
}

int SetPeerBandwidthPacket::GetMessageType()
{
    return RTMP_MSG_SetPeerBandwidth;
}

int SetPeerBandwidthPacket::GetSize()
{
    return 5;
}

error SetPeerBandwidthPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(5)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_ENCODE, "requires 5 only %d bytes", stream->Remain());
    }

    stream->Write4Bytes(m_bandwidth);
    stream->Write1Bytes(m_type);

    return err;
}

UserControlPacket::UserControlPacket()
{
    m_eventType = 0;
    m_eventData = 0;
    m_extraData = 0;
}

UserControlPacket::~UserControlPacket()
{

}

error UserControlPacket::Decode(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(2)) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_DECODE, "requires 2 only %d bytes", stream->Remain());
    }

    m_eventType = stream->Read2Bytes();

    if (m_eventType == PCUCFmsEvent0) {
        if (!stream->Require(1)) {
            return ERRORNEW(ERROR_RTMP_MESSAGE_DECODE, "requires 1 only %d bytes", stream->Remain());
        }
        m_eventData = stream->Read1Bytes();
    } else {
        if (!stream->Require(4)) {
            return ERRORNEW(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->Remain());
        }
        m_eventData = stream->Read4Bytes();
    }

    if (m_eventType == SrcPCUCSetBufferLength) {
        if (!stream->Require(4)) {
            return ERRORNEW(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->Remain());
        }
        m_extraData = stream->Read4Bytes();
    }

    return err;
}

int UserControlPacket::GetPreferCid()
{
    return RTMP_CID_ProtocolControl;
}

int UserControlPacket::GetMessageType()
{
    return RTMP_MSG_UserControlMessage;
}

int UserControlPacket::GetSize()
{
    int size = 2;

    if (m_eventType == PCUCFmsEvent0) {
        size += 1;
    } else {
        size += 4;
    }

    if (m_eventType == SrcPCUCSetBufferLength) {
        size += 4;
    }

    return size;
}

error UserControlPacket::EncodePacket(Buffer *stream)
{
    error err = SUCCESS;

    if (!stream->Require(GetSize())) {
        return ERRORNEW(ERROR_RTMP_MESSAGE_ENCODE, "requires %d only %d bytes", GetSize(), stream->Remain());
    }

    stream->Write2Bytes(m_eventType);

    if (m_eventType == PCUCFmsEvent0) {
        stream->Write1Bytes(m_eventData);
    } else {
        stream->Write4Bytes(m_eventData);
    }

    // when event type is set buffer length,
    // write the extra buffer length.
    if (m_eventType == SrcPCUCSetBufferLength) {
        stream->Write4Bytes(m_extraData);
    }

    return err;
}

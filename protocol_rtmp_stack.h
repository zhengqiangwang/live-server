#ifndef PROTOCOL_RTMP_STACK_H
#define PROTOCOL_RTMP_STACK_H

#include "core_time.h"
#include "error.h"
#include "flv.h"
#include "core_performance.h"
#include <bits/types/struct_iovec.h>
#include <map>
#include <vector>

class FastStream;
class Buffer;
class Amf0Any;
class MessageHeader;
class ChunkStream;
class SharedPtrMessage;

class Protocol;
class IProtocolReader;
class IProtocolReadWriter;
class CreateStreamPacket;
class FMLEStartPacket;
class PublishPacket;
class OnMetaDataPacket;
class PlayPacket;
class CommonMessage;
class Packet;
class Amf0Object;
class IMergeReadHandler;
class CallPacket;

// The amf0 command message, command name macros
#define RTMP_AMF0_COMMAND_CONNECT               "connect"
#define RTMP_AMF0_COMMAND_CREATE_STREAM         "createStream"
#define RTMP_AMF0_COMMAND_CLOSE_STREAM          "closeStream"
#define RTMP_AMF0_COMMAND_PLAY                  "play"
#define RTMP_AMF0_COMMAND_PAUSE                 "pause"
#define RTMP_AMF0_COMMAND_ON_BW_DONE            "onBWDone"
#define RTMP_AMF0_COMMAND_ON_STATUS             "onStatus"
#define RTMP_AMF0_COMMAND_RESULT                "_result"
#define RTMP_AMF0_COMMAND_ERROR                 "_error"
#define RTMP_AMF0_COMMAND_RELEASE_STREAM        "releaseStream"
#define RTMP_AMF0_COMMAND_FC_PUBLISH            "FCPublish"
#define RTMP_AMF0_COMMAND_UNPUBLISH             "FCUnpublish"
#define RTMP_AMF0_COMMAND_PUBLISH               "publish"
#define RTMP_AMF0_DATA_SAMPLE_ACCESS            "|RtmpSampleAccess"

// The signature for packets to client.
#define RTMP_SIG_FMS_VER                        "3,5,3,888"
#define RTMP_SIG_AMF0_VER                       0
#define RTMP_SIG_CLIENT_ID                      "ASAICiss"

// The onStatus consts.
#define StatusLevel                             "level"
#define StatusCode                              "code"
#define StatusDescription                       "description"
#define StatusDetails                           "details"
#define StatusClientId                          "clientid"
// The status value
#define StatusLevelStatus                       "status"
// The status error
#define StatusLevelError                        "error"
// The code value
#define StatusCodeConnectSuccess                "NetConnection.Connect.Success"
#define StatusCodeConnectRejected               "NetConnection.Connect.Rejected"
#define StatusCodeStreamReset                   "NetStream.Play.Reset"
#define StatusCodeStreamStart                   "NetStream.Play.Start"
#define StatusCodeStreamPause                   "NetStream.Pause.Notify"
#define StatusCodeStreamUnpause                 "NetStream.Unpause.Notify"
#define StatusCodePublishStart                  "NetStream.Publish.Start"
#define StatusCodeDataStart                     "NetStream.Data.Start"
#define StatusCodeUnpublishSuccess              "NetStream.Unpublish.Success"

// The decoded message payload.
// @remark we seperate the packet from message,
//        for the packet focus on logic and domain data,
//        the message bind to the protocol and focus on protocol, such as header.
//         we can merge the message and packet, using OOAD hierachy, packet extends from message,
//         it's better for me to use components -- the message use the packet as payload.
class Packet
{
public:
    Packet();
    virtual ~Packet();
public:
    // Covert packet to common message.
    virtual error ToMsg(CommonMessage* msg, int stream_id);
public:
    // The subpacket can override this encode,
    // For example, video and audio will directly set the payload withou memory copy,
    // other packet which need to serialize/encode to bytes by override the
    // get_size and encode_packet.
    virtual error Encode(int& psize, char*& ppayload);
// Decode functions for concrete packet to override.
public:
    // The subpacket must override to decode packet from stream.
    // @remark never invoke the super.decode, it always failed.
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    // The cid(chunk id) specifies the chunk to send data over.
    // Generally, each message perfer some cid, for example,
    // all protocol control messages perfer RTMP_CID_ProtocolControl,
    // SetWindowAckSizePacket is protocol control message.
    virtual int GetPreferCid();
    // The subpacket must override to provide the right message type.
    // The message type set the RTMP message type in header.
    virtual int GetMessageType();
protected:
    // The subpacket can override to calc the packet size.
    virtual int GetSize();
    // The subpacket can override to encode the payload to stream.
    // @remark never invoke the super.encode_packet, it always failed.
    virtual error EncodePacket(Buffer* stream);
};

// The protocol provides the rtmp-message-protocol services,
// To recv RTMP message from RTMP chunk stream,
// and to send out RTMP message over RTMP chunk stream.
class Protocol
{
private:
    class AckWindowSize
    {
    public:
        uint32_t m_window;
        // number of received bytes.
        int64_t m_nbRecvBytes;
        // previous responsed sequence number.
        uint32_t m_sequenceNumber;

        AckWindowSize();
    };
// For peer in/out
private:
    // The underlayer socket object, send/recv bytes.
    IProtocolReadWriter* m_skt;
    // The requests sent out, used to build the response.
    // key: transactionId
    // value: the request command name
    std::map<double, std::string> m_requests;
// For peer in
private:
    // The chunk stream to decode RTMP messages.
    std::map<int, ChunkStream*> m_chunkStreams;
    // Cache some frequently used chunk header.
    // cs_cache, the chunk stream cache.
    ChunkStream** m_csCache;
    // The bytes buffer cache, recv from skt, provide services for stream.
    FastStream* m_inBuffer;
    // The input chunk size, default to 128, set by peer packet.
    int32_t m_inChunkSize;
    // The input ack window, to response acknowledge to peer,
    // For example, to respose the encoder, for server got lots of packets.
    AckWindowSize m_inAckSize;
    // The output ack window, to require peer to response the ack.
    AckWindowSize m_outAckSize;
    // The buffer length set by peer.
    int32_t m_inBufferLength;
    // Whether print the protocol level debug info.
    // Generally we print the debug info when got or send first A/V packet.
    bool m_showDebugInfo;
    // Whether auto response when recv messages.
    // default to true for it's very easy to use the protocol stack.
    bool m_autoResponseWhenRecv;
    // When not auto response message, manual flush the messages in queue.
    std::vector<Packet*> m_manualResponseQueue;
// For peer out
private:
    // Cache for multiple messages send,
    // initialize to iovec[_CONSTS_IOVS_MAX] and realloc when consumed,
    // it's ok to realloc the iovs cache, for all ptr is ok.
    iovec* m_outIovs;
    int m_nbOutIovs;
    // The output header cache.
    // used for type0, 11bytes(or 15bytes with extended timestamp) header.
    // or for type3, 1bytes(or 5bytes with extended timestamp) header.
    // The c0c3 caches must use unit _CONSTS_RTMP_MAX_FMT0_HEADER_SIZE bytes.
    //
    // @remark, the c0c3 cache cannot be realloc.
    // To allocate it in heap to make VS2015 happy.
    char* m_outC0c3Caches;
    // Whether warned user to increase the c0c3 header cache.
    bool m_warnedC0c3CacheDry;
    // The output chunk size, default to 128, set by config.
    int32_t m_outChunkSize;
public:
    Protocol(IProtocolReadWriter* io);
    virtual ~Protocol();
public:
    // Set the auto response message when recv for protocol stack.
    // @param v, whether auto response message when recv message.
    virtual void SetAutoResponse(bool v);
    // Flush for manual response when the auto response is disabled
    // by set_auto_response(false), we default use auto response, so donot
    // need to call this api(the protocol sdk will auto send message).
    // @see the auto_response_when_recv and manual_response_queue.
    virtual error ManualResponseFlush();
public:
#ifdef PERF_MERGED_READ
    // To improve read performance, merge some packets then read,
    // When it on and read small bytes, we sleep to wait more data.,
    // that is, we merge some data to read together.
    // @param v true to ename merged read.
    // @param handler the handler when merge read is enabled.
    virtual void SetMergeRead(bool v, IMergeReadHandler* handler);
    // Create buffer with specifeid size.
    // @param buffer the size of buffer.
    // @remark when MR(_PERF_MERGED_READ) disabled, always set to 8K.
    // @remark when buffer changed, the previous ptr maybe invalid.
    virtual void SetRecvBuffer(int buffer_size);
#endif
public:
    // To set/get the recv timeout in utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void SetRecvTimeout(utime_t tm);
    virtual utime_t GetRecvTimeout();
    // To set/get the send timeout in utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void SetSendTimeout(utime_t tm);
    virtual utime_t GetSendTimeout();
    // Get recv/send bytes.
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
public:
    // Set the input default ack size. This is generally set by the message from peer,
    // but for some encoder, it never send the ack message while it default to a none zone size.
    // This will cause the encoder to block after publishing some messages to server,
    // because it wait for server to send acknowledge, but server default to 0 which means no need
    // To ack encoder. We can change the default input ack size. We will always response the
    // ack size whatever the encoder set or not.
    virtual error SetInWindowAckSize(int ack_size);
public:
    // Recv a RTMP message, which is bytes oriented.
    // user can use decode_message to get the decoded RTMP packet.
    // @param pmsg, set the received message,
    //       always NULL if error,
    //       NULL for unknown packet but return success.
    //       never NULL if decode success.
    // @remark, drop message when msg is empty or payload length is empty.
    virtual error RecvMessage(CommonMessage** pmsg);
    // Decode bytes oriented RTMP message to RTMP packet,
    // @param ppacket, output decoded packet,
    //       always NULL if error, never NULL if success.
    // @return error when unknown packet, error when decode failed.
    virtual error DecodeMessage(CommonMessage* msg, Packet** ppacket);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msg, the msg to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual error SendAndFreeMessage(SharedPtrMessage* msg, int stream_id);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msgs, the msgs to send out, never be NULL.
    // @param nb_msgs, the size of msgs to send out.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual error SendAndFreeMessages(SharedPtrMessage** msgs, int nb_msgs, int stream_id);
    // Send the RTMP packet and always free it.
    // user must never free or use the packet after this method,
    // For it will always free the packet.
    // @param packet, the packet to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual error SendAndFreePacket(Packet* packet, int stream_id);
public:
    // Expect a specified message, drop others util got specified one.
    // @pmsg, user must free it. NULL if not success.
    // @ppacket, user must free it, which decode from payload of message. NULL if not success.
    // @remark, only when success, user can use and must free the pmsg and ppacket.
    // For example:
    //          CommonMessage* msg = NULL;
    //          ConnectAppResPacket* pkt = NULL;
    //          if ((ret = protocol->expect_message<ConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
    //              return ret;
    //          }
    //          // Use then free msg and pkt
    //          Freep(msg);
    //          Freep(pkt);
    // user should never recv message and convert it, use this method instead.
    // if need to set timeout, use set timeout of Protocol.
    template<class T>
    error ExpectMessage(CommonMessage** pmsg, T** ppacket)
    {
        *pmsg = NULL;
        *ppacket = NULL;

        error err = SUCCESS;

        while (true) {
            CommonMessage* msg = NULL;
            if ((err = RecvMessage(&msg)) != SUCCESS) {
                return ERRORWRAP(err, "recv message");
            }

            Packet* packet = NULL;
            if ((err = DecodeMessage(msg, &packet)) != SUCCESS) {
                Freep(msg);
                Freep(packet);
                return ERRORWRAP(err, "decode message");
            }

            T* pkt = dynamic_cast<T*>(packet);
            if (!pkt) {
                Freep(msg);
                Freep(packet);
                continue;
            }

            *pmsg = msg;
            *ppacket = pkt;
            break;
        }

        return err;
    }
private:
    // Send out the messages, donot free it,
    // The caller must free the param msgs.
    virtual error DoSendMessages(SharedPtrMessage** msgs, int nb_msgs);
    // Send iovs. send multiple times if exceed limits.
    virtual error DoIovsSend(iovec* iovs, int size);
    // The underlayer api for send and free packet.
    virtual error DoSendAndFreePacket(Packet* packet, int stream_id);
    // The imp for decode_message
    virtual error DoDecodeMessage(MessageHeader& header, Buffer* stream, Packet** ppacket);
    // Recv bytes oriented RTMP message from protocol stack.
    // return error if error occur and nerver set the pmsg,
    // return success and pmsg set to NULL if no entire message got,
    // return success and pmsg set to entire message if got one.
    virtual error RecvInterlacedMessage(CommonMessage** pmsg);
    // Read the chunk basic header(fmt, cid) from chunk stream.
    // user can discovery a ChunkStream by cid.
    virtual error ReadBasicHeader(char& fmt, int& cid);
    // Read the chunk message header(timestamp, payload_length, message_type, stream_id)
    // From chunk stream and save to ChunkStream.
    virtual error ReadMessageHeader(ChunkStream* chunk, char fmt);
    // Read the chunk payload, remove the used bytes in buffer,
    // if got entire message, set the pmsg.
    virtual error ReadMessagePayload(ChunkStream* chunk, CommonMessage** pmsg);
    // When recv message, update the context.
    virtual error OnRecvMessage(CommonMessage* msg);
    // When message sentout, update the context.
    virtual error OnSendPacket(MessageHeader* mh, Packet* packet);
private:
    // Auto response the ack message.
    virtual error ResponseAcknowledgementMessage();
    // Auto response the ping message.
    virtual error ResponsePingMessage(int32_t timestamp);
private:
    virtual void PrintDebugInfo();
};

// incoming chunk stream maybe interlaced,
// Use the chunk stream to cache the input RTMP chunk streams.
class ChunkStream
{
public:
    // Represents the basic header fmt,
    // which used to identify the variant message header type.
    char m_fmt;
    // Represents the basic header cid,
    // which is the chunk stream id.
    int m_cid;
    // Cached message header
    MessageHeader m_header;
    // Whether the chunk message header has extended timestamp.
    bool m_extendedTimestamp;
    // The partially read message.
    CommonMessage* m_msg;
    // Decoded msg count, to identify whether the chunk stream is fresh.
    int64_t m_msgCount;
public:
    ChunkStream(int _cid);
    virtual ~ChunkStream();
};

// The original request from client.
class Request
{
public:
    // The client ip.
    std::string m_ip;
public:
    // The tcUrl: rtmp://request_vhost:port/app/stream
    // support pass vhost in query string, such as:
    //    rtmp://ip:port/app?vhost=request_vhost/stream
    //    rtmp://ip:port/app...vhost...request_vhost/stream
    std::string m_tcUrl;
    std::string m_pageUrl;
    std::string m_swfUrl;
    double m_objectEncoding;
// The data discovery from request.
public:
    // Discovery from tcUrl and play/publish.
    std::string m_schema;
    // The vhost in tcUrl.
    std::string m_vhost;
    // The host in tcUrl.
    std::string m_host;
    // The port in tcUrl.
    int m_port;
    // The app in tcUrl, without param.
    std::string m_app;
    // The param in tcUrl(app).
    std::string m_param;
    // The stream in play/publish
    std::string m_stream;
    // For play live stream,
    // used to specified the stop when exceed the duration.
    // in utime_t.
    utime_t m_duration;
    // The token in the connect request,
    // used for edge traverse to origin authentication,
    // @see https://github.com/os//issues/104
    Amf0Object* m_args;
public:
    Request();
    virtual ~Request();
public:
    // Deep copy the request, for source to use it to support reload,
    // For when initialize the source, the request is valid,
    // When reload it, the request maybe invalid, so need to copy it.
    virtual Request* Copy();
    // update the auth info of request,
    // To keep the current request ptr is ok,
    // For many components use the ptr of request.
    virtual void UpdateAuth(Request* req);
    // Get the stream identify, vhost/app/stream.
    virtual std::string GetStreamUrl();
    // To strip url, user must strip when update the url.
    virtual void Strip();
public:
    // Transform it as HTTP request.
    virtual Request* AsHttp();
public:
    // The protocol of client:
    //      rtmp, Adobe RTMP protocol.
    //      flv, HTTP-FLV protocol.
    //      flvs, HTTPS-FLV protocol.
    std::string m_protocol;
};

// The response to client.
class Response
{
public:
    // The stream id to response client createStream.
    int m_streamId;
public:
    Response();
    virtual ~Response();
};

// The rtmp client type.
enum RtmpConnType
{
    RtmpConnUnknown = 0x0000,
    // All players.
    RtmpConnPlay = 0x0100,
    HlsPlay = 0x0101,
    FlvPlay = 0x0102,
    RtcConnPlay = 0x0110,
    SrtConnPlay = 0x0120,
    // All publishers.
    RtmpConnFMLEPublish = 0x0200,
    RtmpConnFlashPublish = 0x0201,
    RtmpConnHaivisionPublish = 0x0202,
    RtcConnPublish = 0x0210,
    SrtConnPublish = 0x0220,
};
std::string ClientTypeString(RtmpConnType type);
bool ClientTypeIsPublish(RtmpConnType type);

// store the handshake bytes,
// For smart switch between complex and simple handshake.
class HandshakeBytes
{
public:
    // For RTMP proxy, the real IP.
    uint32_t m_proxyRealIp;
    // [1+1536]
    char* m_c0c1;
    // [1+1536+1536]
    char* m_s0s1s2;
    // [1536]
    char* m_c2;
public:
    HandshakeBytes();
    virtual ~HandshakeBytes();
public:
    virtual void Dispose();
public:
    virtual error ReadC0c1(IProtocolReader* io);
    virtual error ReadS0s1s2(IProtocolReader* io);
    virtual error ReadC2(IProtocolReader* io);
    virtual error CreateC0c1();
    virtual error CreateS0s1s2(const char* c1 = NULL);
    virtual error CreateC2();
};

// The information return from RTMP server.
struct ServerInfo
{
    std::string m_ip;
    std::string m_sig;
    int m_pid;
    int m_cid;
    int m_major;
    int m_minor;
    int m_revision;
    int m_build;

    ServerInfo();
};

// implements the client role protocol.
class RtmpClient
{
private:
    HandshakeBytes* m_hsBytes;
protected:
    Protocol* m_protocol;
    IProtocolReadWriter* m_io;
public:
    RtmpClient(IProtocolReadWriter* skt);
    virtual ~RtmpClient();
// Protocol methods proxy
public:
    virtual void SetRecvTimeout(utime_t tm);
    virtual void SetSendTimeout(utime_t tm);
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
    virtual error RecvMessage(CommonMessage** pmsg);
    virtual error DecodeMessage(CommonMessage* msg, Packet** ppacket);
    virtual error SendAndFreeMessage(SharedPtrMessage* msg, int stream_id);
    virtual error SendAndFreeMessages(SharedPtrMessage** msgs, int nb_msgs, int stream_id);
    virtual error SendAndFreePacket(Packet* packet, int stream_id);
public:
    // handshake with server, try complex, then simple handshake.
    virtual error Handshake();
    // only use simple handshake
    virtual error SimpleHandshaken();
    // only use complex handshake
    virtual error ComplexHandshaken();
    // Connect to RTMP tcUrl and app, get the server info.
    //
    // @param app, The app to connect at, for example, live.
    // @param tcUrl, The tcUrl to connect at, for example, rtmp://os.net/live.
    // @param req, the optional req object, use the swfUrl/pageUrl if specified. NULL to ignore.
    // @param dsu, Whether debug  upnode. For edge, set to true to send its info to upnode.
    // @param si, The server information, retrieve from response of connect app request. NULL to ignore.
    virtual error ConnectApp(std::string app, std::string tcUrl, Request* r, bool dsu, ServerInfo* si);
    // Create a stream, then play/publish data over this stream.
    virtual error CreateStream(int& stream_id);
    // start play stream.
    virtual error Play(std::string stream, int stream_id, int chunk_size);
    // start publish stream. use flash publish workflow:
    //       connect-app => create-stream => flash-publish
    virtual error Publish(std::string stream, int stream_id, int chunk_size);
    // start publish stream. use FMLE publish workflow:
    //       connect-app => FMLE publish
    virtual error FmlePublish(std::string stream, int& stream_id);
public:
    // Expect a specified message, drop others util got specified one.
    // @pmsg, user must free it. NULL if not success.
    // @ppacket, user must free it, which decode from payload of message. NULL if not success.
    // @remark, only when success, user can use and must free the pmsg and ppacket.
    // For example:
    //          CommonMessage* msg = NULL;
    //          ConnectAppResPacket* pkt = NULL;
    //          if ((ret = client->expect_message<ConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
    //              return ret;
    //          }
    //          // Use then free msg and pkt
    //          Freep(msg);
    //          Freep(pkt);
    // user should never recv message and convert it, use this method instead.
    // if need to set timeout, use set timeout of Protocol.
    template<class T>
    error ExpectMessage(CommonMessage** pmsg, T** ppacket)
    {
        return m_protocol->ExpectMessage<T>(pmsg, ppacket);
    }
};

// The rtmp provices rtmp-command-protocol services,
// a high level protocol, media stream oriented services,
// such as connect to vhost/app, play stream, get audio/video data.
class RtmpServer
{
private:
    HandshakeBytes* m_hsBytes;
    Protocol* m_protocol;
    IProtocolReadWriter* m_io;
public:
    RtmpServer(IProtocolReadWriter* skt);
    virtual ~RtmpServer();
public:
    // For RTMP proxy, the real IP. 0 if no proxy.
    // @doc https://github.com/os/go-oryx/wiki/RtmpProxy
    virtual uint32_t ProxyRealIp();
// Protocol methods proxy
public:
    // Set the auto response message when recv for protocol stack.
    // @param v, whether auto response message when recv message.
    virtual void SetAutoResponse(bool v);
#ifdef PERF_MERGED_READ
    // To improve read performance, merge some packets then read,
    // When it on and read small bytes, we sleep to wait more data.,
    // that is, we merge some data to read together.
    // @param v true to ename merged read.
    // @param handler the handler when merge read is enabled.
    virtual void SetMergeRead(bool v, IMergeReadHandler* handler);
    // Create buffer with specifeid size.
    // @param buffer the size of buffer.
    // @remark when MR(_PERF_MERGED_READ) disabled, always set to 8K.
    // @remark when buffer changed, the previous ptr maybe invalid.
    virtual void SetRecvBuffer(int buffer_size);
#endif
    // To set/get the recv timeout in utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void SetRecvTimeout(utime_t tm);
    virtual utime_t GetRecvTimeout();
    // To set/get the send timeout in utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void SetSendTimeout(utime_t tm);
    virtual utime_t GetSendTimeout();
    // Get recv/send bytes.
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
    // Recv a RTMP message, which is bytes oriented.
    // user can use decode_message to get the decoded RTMP packet.
    // @param pmsg, set the received message,
    //       always NULL if error,
    //       NULL for unknown packet but return success.
    //       never NULL if decode success.
    // @remark, drop message when msg is empty or payload length is empty.
    virtual error RecvMessage(CommonMessage** pmsg);
    // Decode bytes oriented RTMP message to RTMP packet,
    // @param ppacket, output decoded packet,
    //       always NULL if error, never NULL if success.
    // @return error when unknown packet, error when decode failed.
    virtual error DecodeMessage(CommonMessage* msg, Packet** ppacket);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msg, the msg to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual error SendAndFreeMessage(SharedPtrMessage* msg, int stream_id);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msgs, the msgs to send out, never be NULL.
    // @param nb_msgs, the size of msgs to send out.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    //
    // @remark performance issue, to support 6k+ 250kbps client,
    virtual error SendAndFreeMessages(SharedPtrMessage** msgs, int nb_msgs, int stream_id);
    // Send the RTMP packet and always free it.
    // user must never free or use the packet after this method,
    // For it will always free the packet.
    // @param packet, the packet to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual error SendAndFreePacket(Packet* packet, int stream_id);
public:
    // Do handshake with client, try complex then simple.
    virtual error Handshake();
    // Do connect app with client, to discovery tcUrl.
    virtual error ConnectApp(Request* req);
    // Set output ack size to client, client will send ack-size for each ack window
    virtual error SetWindowAckSize(int ack_size);
    // Set the default input ack size value.
    virtual error SetInWindowAckSize(int ack_size);
    // @type: The sender can mark this message hard (0), soft (1), or dynamic (2)
    // using the Limit type field.
    virtual error SetPeerBandwidth(int bandwidth, int type);
    // @param server_ip the ip of server.
    virtual error ResponseConnectApp(Request* req, const char* server_ip = NULL);
    // Redirect the connection to another rtmp server.
    // @param a RTMP url to redirect to.
    // @param whether the client accept the redirect.
    virtual error Redirect(Request* r, std::string url, bool& accepted);
    // Reject the connect app request.
    virtual void ResponseConnectReject(Request* req, const char* desc);
    // Response  client the onBWDone message.
    virtual error OnBwDone();
    // Recv some message to identify the client.
    // @stream_id, client will createStream to play or publish by flash,
    //         the stream_id used to response the createStream request.
    // @type, output the client type.
    // @stream_name, output the client publish/play stream name. @see: Request.stream
    // @duration, output the play client duration. @see: Request.duration
    virtual error IdentifyClient(int stream_id, RtmpConnType& type, std::string& stream_name, utime_t& duration);
    // Set the chunk size when client type identified.
    virtual error SetChunkSize(int chunk_size);
    // When client type is play, response with packets:
    // StreamBegin,
    // onStatus(NetStream.Play.Reset), onStatus(NetStream.Play.Start).,
    // |RtmpSampleAccess(false, false),
    // onStatus(NetStream.Data.Start).
    virtual error StartPlay(int stream_id);
    // When client(type is play) send pause message,
    // if is_pause, response the following packets:
    //     onStatus(NetStream.Pause.Notify)
    //     StreamEOF
    // if not is_pause, response the following packets:
    //     onStatus(NetStream.Unpause.Notify)
    //     StreamBegin
    virtual error OnPlayClientPause(int stream_id, bool is_pause);
    // When client type is publish, response with packets:
    // releaseStream response
    // FCPublish
    // FCPublish response
    // createStream response
    // onFCPublish(NetStream.Publish.Start)
    // onStatus(NetStream.Publish.Start)
    virtual error StartFmlePublish(int stream_id);
    // For encoder of Haivision, response the startup request.
    // @see https://github.com/os//issues/844
    virtual error StartHaivisionPublish(int stream_id);
    // process the FMLE unpublish event.
    // @unpublish_tid the unpublish request transaction id.
    virtual error FmleUnpublish(int stream_id, double unpublish_tid);
    // When client type is publish, response with packets:
    // onStatus(NetStream.Publish.Start)
    virtual error StartFlashPublish(int stream_id);
public:
    // Expect a specified message, drop others util got specified one.
    // @pmsg, user must free it. NULL if not success.
    // @ppacket, user must free it, which decode from payload of message. NULL if not success.
    // @remark, only when success, user can use and must free the pmsg and ppacket.
    // For example:
    //          CommonMessage* msg = NULL;
    //          ConnectAppResPacket* pkt = NULL;
    //          if ((ret = server->expect_message<ConnectAppResPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
    //              return ret;
    //          }
    //          // Use then free msg and pkt
    //          Freep(msg);
    //          Freep(pkt);
    // user should never recv message and convert it, use this method instead.
    // if need to set timeout, use set timeout of Protocol.
    template<class T>
    error ExpectMessage(CommonMessage** pmsg, T** ppacket)
    {
        return m_protocol->ExpectMessage<T>(pmsg, ppacket);
    }
private:
    virtual error IdentifyCreateStreamClient(CreateStreamPacket* req, int stream_id, int depth, RtmpConnType& type, std::string& stream_name, utime_t& duration);
    virtual error IdentifyFmlePublishClient(FMLEStartPacket* req, RtmpConnType& type, std::string& stream_name);
    virtual error IdentifyHaivisionPublishClient(FMLEStartPacket* req, RtmpConnType& type, std::string& stream_name);
    virtual error IdentifyFlashPublishClient(PublishPacket* req, RtmpConnType& type, std::string& stream_name);
private:
    virtual error IdentifyPlayClient(PlayPacket* req, RtmpConnType& type, std::string& stream_name, utime_t& duration);
};

// 4.1.1. connect
// The client sends the connect command to the server to request
// connection to a server application instance.
class ConnectAppPacket : public Packet
{
public:
    // Name of the command. Set to "connect".
    std::string m_commandName;
    // Always set to 1.
    double m_transactionId;
    // Command information object which has the name-value pairs.
    // @remark: alloc in packet constructor, user can directly use it,
    //       user should never alloc it again which will cause memory leak.
    // @remark, never be NULL.
    Amf0Object* m_commandObject;
    // Any optional information
    // @remark, optional, init to and maybe NULL.
    Amf0Object* m_args;
public:
    ConnectAppPacket();
    virtual ~ConnectAppPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};
// Response  for ConnectAppPacket.
class ConnectAppResPacket : public Packet
{
public:
    // The _result or _error; indicates whether the response is result or error.
    std::string m_commandName;
    // Transaction ID is 1 for call connect responses
    double m_transactionId;
    // Name-value pairs that describe the properties(fmsver etc.) of the connection.
    // @remark, never be NULL.
    Amf0Object* m_props;
    // Name-value pairs that describe the response from|the server. 'code',
    // 'level', 'description' are names of few among such information.
    // @remark, never be NULL.
    Amf0Object* m_info;
public:
    ConnectAppResPacket();
    virtual ~ConnectAppResPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 4.1.2. Call
// The call method of the NetConnection object runs remote procedure
// calls (RPC) at the receiving end. The called RPC name is passed as a
// parameter to the call command.
class CallPacket : public Packet
{
public:
    // Name of the remote procedure that is called.
    std::string m_commandName;
    // If a response is expected we give a transaction Id. Else we pass a value of 0
    double m_transactionId;
    // If there exists any command info this
    // is set, else this is set to null type.
    // @remark, optional, init to and maybe NULL.
    Amf0Any* m_commandObject;
    // Any optional arguments to be provided
    // @remark, optional, init to and maybe NULL.
    Amf0Any* m_arguments;
public:
    CallPacket();
    virtual ~CallPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};
// Response  for CallPacket.
class CallResPacket : public Packet
{
public:
    // Name of the command.
    std::string m_commandName;
    // ID of the command, to which the response belongs to
    double m_transactionId;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, optional, init to and maybe NULL.
    Amf0Any* m_commandObject;
    // Response from the method that was called.
    // @remark, optional, init to and maybe NULL.
    Amf0Any* m_response;
public:
    CallResPacket(double _transaction_id);
    virtual ~CallResPacket();
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 4.1.3. createStream
// The client sends this command to the server to create a logical
// channel for message communication The publishing of audio, video, and
// metadata is carried out over stream channel created using the
// createStream command.
class CreateStreamPacket : public Packet
{
public:
    // Name of the command. Set to "createStream".
    std::string m_commandName;
    // Transaction ID of the command.
    double m_transactionId;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
public:
    CreateStreamPacket();
    virtual ~CreateStreamPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};
// Response  for CreateStreamPacket.
class CreateStreamResPacket : public Packet
{
public:
    // The _result or _error; indicates whether the response is result or error.
    std::string m_commandName;
    // ID of the command that response belongs to.
    double m_transactionId;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
    // The return value is either a stream ID or an error information object.
    double m_streamId;
public:
    CreateStreamResPacket(double _transaction_id, double _stream_id);
    virtual ~CreateStreamResPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// client close stream packet.
class CloseStreamPacket : public Packet
{
public:
    // Name of the command, set to "closeStream".
    std::string m_commandName;
    // Transaction ID set to 0.
    double m_transactionId;
    // Command information object does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
public:
    CloseStreamPacket();
    virtual ~CloseStreamPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
};

// FMLE start publish: ReleaseStream/PublishStream/FCPublish/FCUnpublish
class FMLEStartPacket : public Packet
{
public:
    // Name of the command
    std::string m_commandName;
    // The transaction ID to get the response.
    double m_transactionId;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
    // The stream name to start publish or release.
    std::string m_streamName;
public:
    FMLEStartPacket();
    virtual ~FMLEStartPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
// Factory method to create specified FMLE packet.
public:
    static FMLEStartPacket* CreateReleaseStream(std::string stream);
    static FMLEStartPacket* CreateFCPublish(std::string stream);
};
// Response  for FMLEStartPacket.
class FMLEStartResPacket : public Packet
{
public:
    // Name of the command
    std::string m_commandName;
    // The transaction ID to get the response.
    double m_transactionId;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
    // The optional args, set to undefined.
    // @remark, never be NULL, an AMF0 undefined instance.
    Amf0Any* m_args; // undefined
public:
    FMLEStartResPacket(double _transaction_id);
    virtual ~FMLEStartResPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// FMLE/flash publish
// 4.2.6. Publish
// The client sends the publish command to publish a named stream to the
// server. Using this name, any client can play this stream and receive
// The published audio, video, and data messages.
class PublishPacket : public Packet
{
public:
    // Name of the command, set to "publish".
    std::string m_commandName;
    // Transaction ID set to 0.
    double m_transactionId;
    // Command information object does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
    // Name with which the stream is published.
    std::string m_streamName;
    // Type of publishing. Set to "live", "record", or "append".
    //   record: The stream is published and the data is recorded to a new file.The file
    //           is stored on the server in a subdirectory within the directory that
    //           contains the server application. If the file already exists, it is
    //           overwritten.
    //   append: The stream is published and the data is appended to a file. If no file
    //           is found, it is created.
    //   live: Live data is published without recording it in a file.
    // @remark,  only support live.
    // @remark, optional, default to live.
    std::string m_type;
public:
    PublishPacket();
    virtual ~PublishPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 4.2.8. pause
// The client sends the pause command to tell the server to pause or
// start playing.
class PausePacket : public Packet
{
public:
    // Name of the command, set to "pause".
    std::string m_commandName;
    // There is no transaction ID for this command. Set to 0.
    double m_transactionId;
    // Command information object does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
    // true or false, to indicate pausing or resuming play
    bool m_isPause;
    // Number of milliseconds at which the the stream is paused or play resumed.
    // This is the current stream time at the Client when stream was paused. When the
    // playback is resumed, the server will only send messages with timestamps
    // greater than this value.
    double m_timeMs;
public:
    PausePacket();
    virtual ~PausePacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
};

// 4.2.1. play
// The client sends this command to the server to play a stream.
class PlayPacket : public Packet
{
public:
    // Name of the command. Set to "play".
    std::string m_commandName;
    // Transaction ID set to 0.
    double m_transactionId;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
    // Name of the stream to play.
    // To play video (FLV) files, specify the name of the stream without a file
    //       extension (for example, "sample").
    // To play back MP3 or ID3 tags, you must precede the stream name with mp3:
    //       (for example, "mp3:sample".)
    // To play H.264/AAC files, you must precede the stream name with mp4: and specify the
    //       file extension. For example, to play the file sample.m4v, specify
    //       "mp4:sample.m4v"
    std::string m_streamName;
    // An optional parameter that specifies the start time in seconds.
    // The default value is -2, which means the subscriber first tries to play the live
    //       stream specified in the Stream Name field. If a live stream of that name is
    //       not found, it plays the recorded stream specified in the Stream Name field.
    // If you pass -1 in the Start field, only the live stream specified in the Stream
    //       Name field is played.
    // If you pass 0 or a positive number in the Start field, a recorded stream specified
    //       in the Stream Name field is played beginning from the time specified in the
    //       Start field.
    // If no recorded stream is found, the next item in the playlist is played.
    double m_start;
    // An optional parameter that specifies the duration of playback in seconds.
    // The default value is -1. The -1 value means a live stream is played until it is no
    //       longer available or a recorded stream is played until it ends.
    // If u pass 0, it plays the single frame since the time specified in the Start field
    //       from the beginning of a recorded stream. It is assumed that the value specified
    //       in the Start field is equal to or greater than 0.
    // If you pass a positive number, it plays a live stream for the time period specified
    //       in the Duration field. After that it becomes available or plays a recorded
    //       stream for the time specified in the Duration field. (If a stream ends before the
    //       time specified in the Duration field, playback ends when the stream ends.)
    // If you pass a negative number other than -1 in the Duration field, it interprets the
    //       value as if it were -1.
    double m_duration;
    // An optional Boolean value or number that specifies whether to flush any
    // previous playlist.
    bool m_reset;
public:
    PlayPacket();
    virtual ~PlayPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// Response  for PlayPacket.
// @remark, user must set the stream_id in header.
class PlayResPacket : public Packet
{
public:
    // Name of the command. If the play command is successful, the command
    // name is set to onStatus.
    std::string m_commandName;
    // Transaction ID set to 0.
    double m_transactionId;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_commandObject; // null
    // If the play command is successful, the client receives OnStatus message from
    // server which is NetStream.Play.Start. If the specified stream is not found,
    // NetStream.Play.StreamNotFound is received.
    // @remark, never be NULL, an AMF0 object instance.
    Amf0Object* m_desc;
public:
    PlayResPacket();
    virtual ~PlayResPacket();
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// When bandwidth test done, notice client.
class OnBWDonePacket : public Packet
{
public:
    // Name of command. Set to "onBWDone"
    std::string m_commandName;
    // Transaction ID set to 0.
    double m_transactionId;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_args; // null
public:
    OnBWDonePacket();
    virtual ~OnBWDonePacket();
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// onStatus command, AMF0 Call
// @remark, user must set the stream_id by CommonMessage.set_packet().
class OnStatusCallPacket : public Packet
{
public:
    // Name of command. Set to "onStatus"
    std::string m_commandName;
    // Transaction ID set to 0.
    double m_transactionId;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    Amf0Any* m_args; // null
    // Name-value pairs that describe the response from the server.
    // 'code','level', 'description' are names of few among such information.
    // @remark, never be NULL, an AMF0 object instance.
    Amf0Object* m_data;
public:
    OnStatusCallPacket();
    virtual ~OnStatusCallPacket();
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// onStatus data, AMF0 Data
// @remark, user must set the stream_id by CommonMessage.set_packet().
class OnStatusDataPacket : public Packet
{
public:
    // Name of command. Set to "onStatus"
    std::string m_commandName;
    // Name-value pairs that describe the response from the server.
    // 'code', are names of few among such information.
    // @remark, never be NULL, an AMF0 object instance.
    Amf0Object* m_data;
public:
    OnStatusDataPacket();
    virtual ~OnStatusDataPacket();
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// AMF0Data RtmpSampleAccess
// @remark, user must set the stream_id by CommonMessage.set_packet().
class SampleAccessPacket : public Packet
{
public:
    // Name of command. Set to "|RtmpSampleAccess".
    std::string m_commandName;
    // Whether allow access the sample of video.
    // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#videoSampleAccess
    bool m_videoSampleAccess;
    // Whether allow access the sample of audio.
    // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#audioSampleAccess
    bool m_audioSampleAccess;
public:
    SampleAccessPacket();
    virtual ~SampleAccessPacket();
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// The stream metadata.
// FMLE: @setDataFrame
// others: onMetaData
class OnMetaDataPacket : public Packet
{
public:
    // Name of metadata. Set to "onMetaData"
    std::string m_name;
    // Metadata of stream.
    // @remark, never be NULL, an AMF0 object instance.
    Amf0Object* m_metadata;
public:
    OnMetaDataPacket();
    virtual ~OnMetaDataPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 5.5. Window Acknowledgement Size (5)
// The client or the server sends this message to inform the peer which
// window size to use when sending acknowledgment.
class SetWindowAckSizePacket : public Packet
{
public:
    int32_t m_ackowledgementWindowSize;
public:
    SetWindowAckSizePacket();
    virtual ~SetWindowAckSizePacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 5.3. Acknowledgement (3)
// The client or the server sends the acknowledgment to the peer after
// receiving bytes equal to the window size.
class AcknowledgementPacket : public Packet
{
public:
    uint32_t m_sequenceNumber;
public:
    AcknowledgementPacket();
    virtual ~AcknowledgementPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 7.1. Set Chunk Size
// Protocol control message 1, Set Chunk Size, is used to notify the
// peer about the new maximum chunk size.
class SetChunkSizePacket : public Packet
{
public:
    // The maximum chunk size can be 65536 bytes. The chunk size is
    // maintained independently for each direction.
    int32_t m_chunkSize;
public:
    SetChunkSizePacket();
    virtual ~SetChunkSizePacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 5.6. Set Peer Bandwidth (6)
enum PeerBandwidthType
{
    // The sender can mark this message hard (0), soft (1), or dynamic (2)
    // using the Limit type field.
    PeerBandwidthHard = 0,
    PeerBandwidthSoft = 1,
    PeerBandwidthDynamic = 2,
};

// 5.6. Set Peer Bandwidth (6)
// The client or the server sends this message to update the output
// bandwidth of the peer.
class SetPeerBandwidthPacket : public Packet
{
public:
    int32_t m_bandwidth;
    // @see: PeerBandwidthType
    int8_t m_type;
public:
    SetPeerBandwidthPacket();
    virtual ~SetPeerBandwidthPacket();
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};

// 3.7. User Control message
enum SrcPCUCEventType
{
    // Generally, 4bytes event-data

    // The server sends this event to notify the client
    // that a stream has become functional and can be
    // used for communication. By default, this event
    // is sent on ID 0 after the application connect
    // command is successfully received from the
    // client. The event data is 4-byte and represents
    // The stream ID of the stream that became
    // Functional.
    SrcPCUCStreamBegin = 0x00,

    // The server sends this event to notify the client
    // that the playback of data is over as requested
    // on this stream. No more data is sent without
    // issuing additional commands. The client discards
    // The messages received for the stream. The
    // 4 bytes of event data represent the ID of the
    // stream on which playback has ended.
    SrcPCUCStreamEOF = 0x01,

    // The server sends this event to notify the client
    // that there is no more data on the stream. If the
    // server does not detect any message for a time
    // period, it can notify the subscribed clients
    // that the stream is dry. The 4 bytes of event
    // data represent the stream ID of the dry stream.
    SrcPCUCStreamDry = 0x02,

    // The client sends this event to inform the server
    // of the buffer size (in milliseconds) that is
    // used to buffer any data coming over a stream.
    // This event is sent before the server starts
    // processing the stream. The first 4 bytes of the
    // event data represent the stream ID and the next
    // 4 bytes represent the buffer length, in
    // milliseconds.
    SrcPCUCSetBufferLength = 0x03, // 8bytes event-data

    // The server sends this event to notify the client
    // that the stream is a recorded stream. The
    // 4 bytes event data represent the stream ID of
    // The recorded stream.
    SrcPCUCStreamIsRecorded = 0x04,

    // The server sends this event to test whether the
    // client is reachable. Event data is a 4-byte
    // timestamp, representing the local server time
    // When the server dispatched the command. The
    // client responds with kMsgPingResponse on
    // receiving kMsgPingRequest.
    SrcPCUCPingRequest = 0x06,

    // The client sends this event to the server in
    // Response  to the ping request. The event data is
    // a 4-byte timestamp, which was received with the
    // kMsgPingRequest request.
    SrcPCUCPingResponse = 0x07,

    // For PCUC size=3, for example the payload is "00 1A 01",
    // it's a FMS control event, where the event type is 0x001a and event data is 0x01,
    // please notice that the event data is only 1 byte for this event.
    PCUCFmsEvent0 = 0x1a,
};

// 5.4. User Control Message (4)
//
// For the EventData is 4bytes.
// Stream Begin(=0)              4-bytes stream ID
// Stream EOF(=1)                4-bytes stream ID
// StreamDry(=2)                 4-bytes stream ID
// SetBufferLength(=3)           8-bytes 4bytes stream ID, 4bytes buffer length.
// StreamIsRecorded(=4)          4-bytes stream ID
// PingRequest(=6)               4-bytes timestamp local server time
// PingResponse(=7)              4-bytes timestamp received ping request.
//
// 3.7. User Control message
// +------------------------------+-------------------------
// | Event Type ( 2- bytes ) | Event Data
// +------------------------------+-------------------------
// Figure 5 Pay load for the 'User Control Message'.
class UserControlPacket : public Packet
{
public:
    // Event type is followed by Event data.
    // @see: SrcPCUCEventType
    int16_t m_eventType;
    // The event data generally in 4bytes.
    // @remark for event type is 0x001a, only 1bytes.
    // @see PCUCFmsEvent0
    int32_t m_eventData;
    // 4bytes if event_type is SetBufferLength; otherwise 0.
    int32_t m_extraData;
public:
    UserControlPacket();
    virtual ~UserControlPacket();
// Decode functions for concrete packet to override.
public:
    virtual error Decode(Buffer* stream);
// Encode functions for concrete packet to override.
public:
    virtual int GetPreferCid();
    virtual int GetMessageType();
protected:
    virtual int GetSize();
    virtual error EncodePacket(Buffer* stream);
};
#endif // PROTOCOL_RTMP_STACK_H

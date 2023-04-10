#ifndef PROTOCOL_RTMP_CONN_H
#define PROTOCOL_RTMP_CONN_H


#include "core_time.h"
#include <string>

class Request;
class TcpClient;
class RtmpClient;
class CommonMessage;
class SharedPtrMessage;
class Packet;
class NetworkKbps;
class WallClock;
class Amf0Object;

// The simple RTMP client, provides friendly APIs.
// @remark Should never use client when closed.
// Usage:
//      SrsBasicRtmpClient client("rtmp://127.0.0.1:1935/live/livestream", 3000, 9000);
//      client.connect();
//      client.play();
//      client.close();
class BasicRtmpClient
{
private:
    std::string m_url;
    utime_t m_connectTimeout;
    utime_t m_streamTimeout;
protected:
    Request* m_req;
private:
    TcpClient* m_transport;
    RtmpClient* m_client;
    NetworkKbps* m_kbps;
    int m_streamId;
public:
    // Constructor.
    // @param r The RTMP url, for example, rtmp://ip:port/app/stream?domain=vhost
    // @param ctm The timeout in srs_utime_t to connect to server.
    // @param stm The timeout in srs_utime_t to delivery A/V stream.
    BasicRtmpClient(std::string r, utime_t ctm, utime_t stm);
    virtual ~BasicRtmpClient();
public:
    // Get extra args to carry more information.
    Amf0Object* ExtraArgs();
public:
    // Connect, handshake and connect app to RTMP server.
    // @remark We always close the transport.
    virtual error Connect();
    virtual void Close();
protected:
    virtual error ConnectApp();
    virtual error DoConnectApp(std::string local_ip, bool debug);
public:
    virtual error Publish(int chunk_size, bool with_vhost = true, std::string* pstream = NULL);
    virtual error Play(int chunk_size, bool with_vhost = true, std::string* pstream = NULL);
    virtual void KbpsSample(const char* label, utime_t age);
    virtual void KbpsSample(const char* label, utime_t age, int msgs);
    virtual int Sid();
public:
    virtual error RecvMessage(CommonMessage** pmsg);
    virtual error DecodeMessage(CommonMessage* msg, Packet** ppacket);
    virtual error SendAndFreeMessages(SharedPtrMessage** msgs, int nb_msgs);
    virtual error SendAndFreeMessage(SharedPtrMessage* msg);
public:
    virtual void SetRecvTimeout(utime_t timeout);
};


#endif // PROTOCOL_RTMP_CONN_H

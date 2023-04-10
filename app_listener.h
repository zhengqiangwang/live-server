#ifndef APP_LISTENER_H
#define APP_LISTENER_H


#include "app_st.h"
#include "protocol_st.h"
#include <map>
#include <netinet/in.h>
#include <string>
#include <vector>

struct sockaddr;

class Buffer;
class UdpMuxSocket;
class IListener;

// The udp packet handler.
class IUdpHandler
{
public:
    IUdpHandler();
    virtual ~IUdpHandler();
public:
    // When udp listener got a udp packet, notice server to process it.
    // @param type, the client type, used to create concrete connection,
    //       for instance RTMP connection to serve client.
    // @param from, the udp packet from address.
    // @param buf, the udp packet bytes, user should copy if need to use.
    // @param nb_buf, the size of udp packet bytes.
    // @remark user should never use the buf, for it's a shared memory bytes.
    virtual error OnUdpPacket(const sockaddr* from, const int fromlen, char* buf, int nb_buf) = 0;
};

// The UDP packet handler. TODO: FIXME: Merge with ISrsUdpHandler
class IUdpMuxHandler
{
public:
    IUdpMuxHandler();
    virtual ~IUdpMuxHandler();
public:
    virtual error OnUdpPacket(UdpMuxSocket* skt) = 0;
};

// All listener should support listen method.
class IListener
{
public:
    IListener();
    virtual ~IListener();
public:
    virtual error Listen() = 0;
};

// The tcp connection handler.
class ITcpHandler
{
public:
    ITcpHandler();
    virtual ~ITcpHandler();
public:
    // When got tcp client.
    virtual error OnTcpClient(IListener* listener, netfd_t stfd) = 0;
};

// Bind udp port, start thread to recv packet and handler it.
class UdpListener : public ICoroutineHandler
{
protected:
    std::string m_label;
    netfd_t m_lfd;
    Coroutine* m_trd;
protected:
    char* m_buf;
    int m_nbBuf;
protected:
    IUdpHandler* m_handler;
    std::string m_ip;
    int m_port;
public:
    UdpListener(IUdpHandler* h);
    virtual ~UdpListener();
public:
    UdpListener* SetLabel(const std::string& label);
    UdpListener* SetEndpoint(const std::string& i, int p);
private:
    virtual int Fd();
    virtual netfd_t Stfd();
private:
    void SetSocketBuffer();
public:
    virtual error Listen();
    void Close();
// Interface ISrsReusableThreadHandler.
public:
    virtual error Cycle();
};

// Bind and listen tcp port, use handler to process the client.
class TcpListener : public ICoroutineHandler, public IListener
{
private:
    std::string m_label;
    netfd_t m_lfd;
    Coroutine* m_trd;
private:
    ITcpHandler* m_handler;
    std::string m_ip;
    int m_port;
public:
    TcpListener(ITcpHandler* h);
    virtual ~TcpListener();
public:
    TcpListener* SetLabel(const std::string& label);
    TcpListener* SetEndpoint(const std::string& i, int p);
    TcpListener* SetEndpoint(const std::string& endpoint);
    int Port();
public:
    virtual error Listen();
    void Close();
// Interface ISrsReusableThreadHandler.
public:
    virtual error Cycle();
};

// Bind and listen tcp port, use handler to process the client.
class MultipleTcpListeners : public IListener, public ITcpHandler
{
private:
    ITcpHandler* m_handler;
    std::vector<TcpListener*> m_listeners;
public:
    MultipleTcpListeners(ITcpHandler* h);
    virtual ~MultipleTcpListeners();
public:
    MultipleTcpListeners* SetLabel(const std::string& label);
    MultipleTcpListeners* Add(const std::vector<std::string>& endpoints);
public:
    error Listen();
    void Close();
// Interface ISrsTcpHandler
public:
    virtual error OnTcpClient(IListener* listener, netfd_t stfd);
};

// TODO: FIXME: Rename it. Refine it for performance issue.
class UdpMuxSocket
{
private:
    // For sender yield only.
    uint32_t m_nnMsgsForYield;
    std::map<uint32_t, std::string> m_cache;
    Buffer* m_cacheBuffer;
private:
    char* m_buf;
    int m_nbBuf;
    int m_nread;
    netfd_t m_lfd;
    sockaddr_storage m_from;
    int m_fromlen;
private:
    std::string m_peerIp;
    int m_peerPort;
private:
    // Cache for peer id.
    std::string m_peerId;
    // If the address changed, we should generate the peer_id.
    bool m_addressChanged;
    // For IPv4 client, we use 8 bytes int id to find it fastly.
    uint64_t m_fastId;
public:
    UdpMuxSocket(netfd_t fd);
    virtual ~UdpMuxSocket();
public:
    int Recvfrom(utime_t timeout);
    error Sendto(void* data, int size, utime_t timeout);
    netfd_t Stfd();
    sockaddr_in* PeerAddr();
    socklen_t PeerAddrlen();
    char* Data();
    int Size();
    std::string GetPeerIp() const;
    int GetPeerPort() const;
    std::string PeerId();
    uint64_t FastId();
    Buffer* BufferS();
    UdpMuxSocket* CopySendonly();
};

class UdpMuxListener : public ICoroutineHandler
{
private:
    netfd_t m_lfd;
    Coroutine* m_trd;
    ContextId m_cid;
private:
    char* m_buf;
    int m_nbBuf;
private:
    IUdpMuxHandler* m_handler;
    std::string m_ip;
    int m_port;
public:
    UdpMuxListener(IUdpMuxHandler* h, std::string i, int p);
    virtual ~UdpMuxListener();
public:
    virtual int Fd();
    virtual netfd_t Stfd();
public:
    virtual error Listen();
// Interface ISrsReusableThreadHandler.
public:
    virtual error Cycle();
private:
    void SetSocketBuffer();
};

#endif // APP_LISTENER_H

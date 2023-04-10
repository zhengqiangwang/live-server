#include "app_listener.h"
#include "app_pithy_print.h"
#include "app_utility.h"
#include "kbps.h"
#include "app_st.h"
#include "utility.h"
#include "buffer.h"
#include "core_autofree.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <inttypes.h>


Pps* pps_rpkts = NULL;
Pps* pps_addrs = NULL;
Pps* pps_fast_addrs = NULL;

Pps* pps_spkts = NULL;

// set the max packet size.
#define UDP_MAX_PACKET_SIZE 65535

// sleep in srs_utime_t for udp recv packet.
#define UdpPacketRecvCycleInterval 0

IUdpHandler::IUdpHandler()
{

}

IUdpHandler::~IUdpHandler()
{

}

IUdpMuxHandler::IUdpMuxHandler()
{

}

IUdpMuxHandler::~IUdpMuxHandler()
{

}

IListener::IListener()
{

}

IListener::~IListener()
{

}

ITcpHandler::ITcpHandler()
{

}

ITcpHandler::~ITcpHandler()
{

}

UdpListener::UdpListener(IUdpHandler *h)
{
    m_handler = h;
    m_lfd = NULL;
    m_port = 0;
    m_label = "UDP";

    m_nbBuf = UDP_MAX_PACKET_SIZE;
    m_buf = new char[m_nbBuf];

    m_trd = new DummyCoroutine();
}

UdpListener::~UdpListener()
{
    Freep(m_trd);
    CloseStfd(m_lfd);
    Freepa(m_buf);
}

UdpListener *UdpListener::SetLabel(const std::string &label)
{
    m_label = label;
    return this;
}

UdpListener *UdpListener::SetEndpoint(const std::string &i, int p)
{
    m_ip = i;
    m_port = p;
    return this;
}

int UdpListener::Fd()
{
    return NetfdFileno(m_lfd);
}

netfd_t UdpListener::Stfd()
{
    return m_lfd;
}

void UdpListener::SetSocketBuffer()
{
    int default_sndbuf = 0;
    // TODO: FIXME: Config it.
    int expect_sndbuf = 1024*1024*10; // 10M
    int actual_sndbuf = expect_sndbuf;
    int r0_sndbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_sndbuf);
        // TODO: FIXME: check err
        getsockopt(Fd(), SOL_SOCKET, SO_SNDBUF, (void*)&default_sndbuf, &opt_len);

        if ((r0_sndbuf = setsockopt(Fd(), SOL_SOCKET, SO_SNDBUF, (void*)&actual_sndbuf, sizeof(actual_sndbuf))) < 0) {
            warn("set SO_SNDBUF failed, expect=%d, r0=%d", expect_sndbuf, r0_sndbuf);
        }

        opt_len = sizeof(actual_sndbuf);
        // TODO: FIXME: check err
        getsockopt(Fd(), SOL_SOCKET, SO_SNDBUF, (void*)&actual_sndbuf, &opt_len);
    }

    int default_rcvbuf = 0;
    // TODO: FIXME: Config it.
    int expect_rcvbuf = 1024*1024*10; // 10M
    int actual_rcvbuf = expect_rcvbuf;
    int r0_rcvbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_rcvbuf);
        // TODO: FIXME: check err
        getsockopt(Fd(), SOL_SOCKET, SO_RCVBUF, (void*)&default_rcvbuf, &opt_len);

        if ((r0_rcvbuf = setsockopt(Fd(), SOL_SOCKET, SO_RCVBUF, (void*)&actual_rcvbuf, sizeof(actual_rcvbuf))) < 0) {
            warn("set SO_RCVBUF failed, expect=%d, r0=%d", expect_rcvbuf, r0_rcvbuf);
        }

        opt_len = sizeof(actual_rcvbuf);
        // TODO: FIXME: check err
        getsockopt(Fd(), SOL_SOCKET, SO_RCVBUF, (void*)&actual_rcvbuf, &opt_len);
    }

    trace("UDP #%d LISTEN at %s:%d, SO_SNDBUF(default=%d, expect=%d, actual=%d, r0=%d), SO_RCVBUF(default=%d, expect=%d, actual=%d, r0=%d)",
        NetfdFileno(m_lfd), m_ip.c_str(), m_port, default_sndbuf, expect_sndbuf, actual_sndbuf, r0_sndbuf, default_rcvbuf, expect_rcvbuf, actual_rcvbuf, r0_rcvbuf);
}

error UdpListener::Listen()
{
    error err = SUCCESS;

    // Ignore if not configured.
    if (m_ip.empty() || !m_port) return err;

    CloseStfd(m_lfd);
    if ((err = UdpListen(m_ip, m_port, &m_lfd)) != SUCCESS) {
        return ERRORWRAP(err, "listen %s:%d", m_ip.c_str(), m_port);
    }

    SetSocketBuffer();

    Freep(m_trd);
    m_trd = new STCoroutine("udp", this, Context->GetId());
    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start thread");
    }

    return err;
}

void UdpListener::Close()
{
    m_trd->Stop();
}

error UdpListener::Cycle()
{
    error err = SUCCESS;

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "udp listener");
        }

        int nread = 0;
        sockaddr_storage from;
        int nb_from = sizeof(from);
        if ((nread = Recvfrom(m_lfd, m_buf, m_nbBuf, (sockaddr*)&from, &nb_from, UTIME_NO_TIMEOUT)) <= 0) {
            return ERRORNEW(ERROR_SOCKET_READ, "udp read, nread=%d", nread);
        }

        // Drop UDP health check packet of Aliyun SLB.
        //      Healthcheck udp check
        // @see https://help.aliyun.com/document_detail/27595.html
        if (nread == 21 && m_buf[0] == 0x48 && m_buf[1] == 0x65 && m_buf[2] == 0x61 && m_buf[3] == 0x6c
            && m_buf[19] == 0x63 && m_buf[20] == 0x6b) {
            continue;
        }

        if ((err = m_handler->OnUdpPacket((const sockaddr*)&from, nb_from, m_buf, nread)) != SUCCESS) {
            return ERRORWRAP(err, "handle packet %d bytes", nread);
        }

        if (UdpPacketRecvCycleInterval > 0) {
            Usleep(UdpPacketRecvCycleInterval);
        }
    }

    return err;
}

TcpListener::TcpListener(ITcpHandler *h)
{
    m_handler = h;
    m_port = 0;
    m_lfd = NULL;
    m_label = "TCP";
    m_trd = new DummyCoroutine();
}

TcpListener::~TcpListener()
{
    Freep(m_trd);
    CloseStfd(m_lfd);
}

TcpListener *TcpListener::SetLabel(const std::string &label)
{
    m_label = label;
    return this;
}

TcpListener *TcpListener::SetEndpoint(const std::string &i, int p)
{
    m_ip = i;
    m_port = p;
    return this;
}

TcpListener *TcpListener::SetEndpoint(const std::string &endpoint)
{
    std::string ip; int port_;
    ParseEndpoint(endpoint, ip, port_);
    return SetEndpoint(ip, port_);
}

int TcpListener::Port()
{
    return m_port;
}

error TcpListener::Listen()
{
    error err = SUCCESS;

    // Ignore if not configured.
    if (m_ip.empty() || !m_port) return err;

    CloseStfd(m_lfd);
    if ((err = TcpListen(m_ip, m_port, &m_lfd)) != SUCCESS) {
        return ERRORWRAP(err, "listen at %s:%d", m_ip.c_str(), m_port);
    }

    Freep(m_trd);
    m_trd = new STCoroutine("tcp", this);
    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start coroutine");
    }

    int fd = NetfdFileno(m_lfd);
    trace("%s listen at tcp://%s:%d, fd=%d", m_label.c_str(), m_ip.c_str(), m_port, fd);

    return err;
}

void TcpListener::Close()
{
    m_trd->Stop();
    CloseStfd(m_lfd);
}

error TcpListener::Cycle()
{
    error err = SUCCESS;

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "tcp listener");
        }

        netfd_t fd = Accept(m_lfd, NULL, NULL, UTIME_NO_TIMEOUT);
        if(fd == NULL){
            return ERRORNEW(ERROR_SOCKET_ACCEPT, "accept at fd=%d", NetfdFileno(m_lfd));
        }

        if ((err = FdCloseexec(NetfdFileno(fd))) != SUCCESS) {
            return ERRORWRAP(err, "set closeexec");
        }

        if ((err = m_handler->OnTcpClient(this, fd)) != SUCCESS) {
            return ERRORWRAP(err, "handle fd=%d", NetfdFileno(fd));
        }
    }

    return err;
}

MultipleTcpListeners::MultipleTcpListeners(ITcpHandler *h)
{
    m_handler = h;
}

MultipleTcpListeners::~MultipleTcpListeners()
{
    for (std::vector<TcpListener*>::iterator it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        TcpListener* l = *it;
        Freep(l);
    }
}

MultipleTcpListeners *MultipleTcpListeners::SetLabel(const std::string &label)
{
    for (std::vector<TcpListener*>::iterator it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        TcpListener* l = *it;
        l->SetLabel(label);
    }

    return this;
}

MultipleTcpListeners *MultipleTcpListeners::Add(const std::vector<std::string> &endpoints)
{
    for (int i = 0; i < (int) endpoints.size(); i++) {
        std::string ip; int port;
        ParseEndpoint(endpoints[i], ip, port);

        TcpListener* l = new TcpListener(this);
        m_listeners.push_back(l->SetEndpoint(ip, port));
    }

    return this;
}

error MultipleTcpListeners::Listen()
{
    error err = SUCCESS;

    for (std::vector<TcpListener*>::iterator it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        TcpListener* l = *it;

        if ((err = l->Listen()) != SUCCESS) {
            return ERRORWRAP(err, "listen");
        }
    }

    return err;
}

void MultipleTcpListeners::Close()
{
    for (std::vector<TcpListener*>::iterator it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        TcpListener* l = *it;
        Freep(l);
    }
    m_listeners.clear();
}

error MultipleTcpListeners::OnTcpClient(IListener *listener, netfd_t stfd)
{
    return m_handler->OnTcpClient(this, stfd);
}

UdpMuxSocket::UdpMuxSocket(netfd_t fd)
{
    m_nnMsgsForYield = 0;
    m_nbBuf = UDP_MAX_PACKET_SIZE;
    m_buf = new char[m_nbBuf];
    m_nread = 0;

    m_lfd = fd;

    m_fromlen = 0;
    m_peerPort = 0;

    m_fastId = 0;
    m_addressChanged = false;
    m_cacheBuffer = new Buffer(m_buf, m_nbBuf);
}

UdpMuxSocket::~UdpMuxSocket()
{
    Freepa(m_buf);
    Freep(m_cacheBuffer);
}

int UdpMuxSocket::Recvfrom(utime_t timeout)
{
    m_fromlen = sizeof(m_from);
    m_nread = ::Recvfrom(m_lfd, m_buf, m_nbBuf, (sockaddr*)&m_from, &m_fromlen, timeout);
    if (m_nread <= 0) {
        return m_nread;
    }

    // Reset the fast cache buffer size.
    m_cacheBuffer->SetSize(m_nread);
    m_cacheBuffer->Skip(-1 * m_cacheBuffer->Pos());

    // Drop UDP health check packet of Aliyun SLB.
    //      Healthcheck udp check
    // @see https://help.aliyun.com/document_detail/27595.html
    if (m_nread == 21 && m_buf[0] == 0x48 && m_buf[1] == 0x65 && m_buf[2] == 0x61 && m_buf[3] == 0x6c
        && m_buf[19] == 0x63 && m_buf[20] == 0x6b) {
        return 0;
    }

    // Parse address from cache.
    if (m_from.ss_family == AF_INET) {
        sockaddr_in* addr = (sockaddr_in*)&m_from;
        m_fastId = uint64_t(addr->sin_port)<<48 | uint64_t(addr->sin_addr.s_addr);
    }

    // We will regenerate the peer_ip, peer_port and peer_id.
    m_addressChanged = true;

    // Update the stat.
    ++pps_rpkts->m_sugar;

    return m_nread;
}

error UdpMuxSocket::Sendto(void *data, int size, utime_t timeout)
{
    error err = SUCCESS;

    ++pps_spkts->m_sugar;

    int nb_write = ::Sendto(m_lfd, data, size, (sockaddr*)&m_from, m_fromlen, timeout);

    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return ERRORNEW(ERROR_SOCKET_TIMEOUT, "sendto timeout %d ms", u2msi(timeout));
        }

        return ERRORNEW(ERROR_SOCKET_WRITE, "sendto");
    }

    // Yield to another coroutines.
    // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777542162
    if (++m_nnMsgsForYield > 20) {
        m_nnMsgsForYield = 0;
        ThreadYield();
    }

    return err;
}

netfd_t UdpMuxSocket::Stfd()
{
    return m_lfd;
}

sockaddr_in *UdpMuxSocket::PeerAddr()
{
    return (sockaddr_in*)&m_from;
}

socklen_t UdpMuxSocket::PeerAddrlen()
{
    return (socklen_t)m_fromlen;
}

char *UdpMuxSocket::Data()
{
    return m_buf;
}

int UdpMuxSocket::Size()
{
    return m_nread;
}

std::string UdpMuxSocket::GetPeerIp() const
{
    return m_peerIp;
}

int UdpMuxSocket::GetPeerPort() const
{
    return m_peerPort;
}

std::string UdpMuxSocket::PeerId()
{
    if (m_addressChanged) {
        m_addressChanged = false;

        // Parse address from cache.
        bool parsed = false;
        if (m_from.ss_family == AF_INET) {
            sockaddr_in* addr = (sockaddr_in*)&m_from;

            // Load from fast cache, previous ip.
            std::map<uint32_t, std::string>::iterator it = m_cache.find(addr->sin_addr.s_addr);
            if (it == m_cache.end()) {
                m_peerIp = inet_ntoa(addr->sin_addr);
                m_cache[addr->sin_addr.s_addr] = m_peerIp;
            } else {
                m_peerIp = it->second;
            }

            m_peerPort = ntohs(addr->sin_port);
            parsed = true;
        }

        if (!parsed) {
            // TODO: FIXME: Maybe we should not covert to string for each packet.
            char address_string[64];
            char port_string[16];
            if (getnameinfo((sockaddr*)&m_from, m_fromlen,
                           (char*)&address_string, sizeof(address_string),
                           (char*)&port_string, sizeof(port_string),
                           NI_NUMERICHOST|NI_NUMERICSERV)) {
                return "";
            }

            m_peerIp = std::string(address_string);
            m_peerPort = atoi(port_string);
        }

        // Build the peer id, reserve 1 byte for the trailing '\0'.
        static char id_buf[128 + 1];
        int len = snprintf(id_buf, sizeof(id_buf), "%s:%d", m_peerIp.c_str(), m_peerPort);
        if (len <= 0 || len >= (int)sizeof(id_buf)) {
            return "";
        }
        m_peerId = std::string(id_buf, len);

        // Update the stat.
        ++pps_addrs->m_sugar;
    }

    return m_peerId;
}

uint64_t UdpMuxSocket::FastId()
{
    ++pps_fast_addrs->m_sugar;
    return m_fastId;
}

Buffer *UdpMuxSocket::BufferS()
{
    return m_cacheBuffer;
}

UdpMuxSocket *UdpMuxSocket::CopySendonly()
{
    UdpMuxSocket* sendonly = new UdpMuxSocket(m_lfd);

    // Don't copy buffer
    Freepa(sendonly->m_buf);
    sendonly->m_nbBuf     = 0;
    sendonly->m_nread     = 0;
    sendonly->m_lfd       = m_lfd;
    sendonly->m_from      = m_from;
    sendonly->m_fromlen   = m_fromlen;
    sendonly->m_peerIp    = m_peerIp;
    sendonly->m_peerPort  = m_peerPort;

    // Copy the fast id.
    sendonly->m_peerId = m_peerId;
    sendonly->m_fastId = m_fastId;
    sendonly->m_addressChanged = m_addressChanged;

    return sendonly;
}

UdpMuxListener::UdpMuxListener(IUdpMuxHandler *h, std::string i, int p)
{
    m_handler = h;

    m_ip = i;
    m_port = p;
    m_lfd = NULL;

    m_nbBuf = UDP_MAX_PACKET_SIZE;
    m_buf = new char[m_nbBuf];

    m_trd = new DummyCoroutine();
    m_cid = Context->GenerateId();
}

UdpMuxListener::~UdpMuxListener()
{
    Freep(m_trd);
    CloseStfd(m_lfd);
    Freepa(m_buf);
}

int UdpMuxListener::Fd()
{
    return NetfdFileno(m_lfd);
}

netfd_t UdpMuxListener::Stfd()
{
    return m_lfd;
}

error UdpMuxListener::Listen()
{
    error err = SUCCESS;

    if ((err = UdpListen(m_ip, m_port, &m_lfd)) != SUCCESS) {
        return ERRORWRAP(err, "listen %s:%d", m_ip.c_str(), m_port);
    }

    Freep(m_trd);
    m_trd = new STCoroutine("udp", this, m_cid);

    //change stack size to 256K, fix crash when call some 3rd-part api.
    ((STCoroutine*)m_trd)->SetStackSize(1 << 18);

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start thread");
    }

    return err;
}

error UdpMuxListener::Cycle()
{
    error err = SUCCESS;

    PithyPrint* pprint = PithyPrint::CreateRtcRecv(NetfdFileno(m_lfd));
    AutoFree(PithyPrint, pprint);

    uint64_t nn_msgs = 0;
    uint64_t nn_msgs_stage = 0;
    uint64_t nn_msgs_last = 0;
    uint64_t nn_loop = 0;
    utime_t time_last = GetSystemTime();

    ErrorPithyPrint* pp_pkt_handler_err = new ErrorPithyPrint();
    AutoFree(ErrorPithyPrint, pp_pkt_handler_err);

    SetSocketBuffer();

    // Because we have to decrypt the cipher of received packet payload,
    // and the size is not determined, so we think there is at least one copy,
    // and we can reuse the plaintext h264/opus with players when got plaintext.
    UdpMuxSocket skt(m_lfd);

    // How many messages to run a yield.
    uint32_t nn_msgs_for_yield = 0;

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "udp listener");
        }

        nn_loop++;

        int nread = skt.Recvfrom(UTIME_NO_TIMEOUT);
        if (nread <= 0) {
            if (nread < 0) {
                warn("udp recv error nn=%d", nread);
            }
            // remux udp never return
            continue;
        }

        nn_msgs++;
        nn_msgs_stage++;

        // Handle the UDP packet.
        err = m_handler->OnUdpPacket(&skt);

        // Use pithy print to show more smart information.
        if (err != SUCCESS) {
            uint32_t nn = 0;
            if (pp_pkt_handler_err->CanPrint(err, &nn)) {
                // For performance, only restore context when output log.
                Context->SetId(m_cid);

                // Append more information.
                err = ERRORWRAP(err, "size=%u, data=[%s]", skt.Size(), StringDumpsHex(skt.Data(), skt.Size(), 8).c_str());
                warn("handle udp pkt, count=%u/%u, err: %s", pp_pkt_handler_err->m_nnCount, nn, ERRORDESC(err).c_str());
            }
            Freep(err);
        }

        pprint->Elapse();
        if (pprint->CanPrint()) {
            // For performance, only restore context when output log.
            Context->SetId(m_cid);

            int pps_average = 0; int pps_last = 0;
            if (true) {
                if (GetSystemTime() > GetSystemStartupTime()) {
                    pps_average = (int)(nn_msgs * UTIME_SECONDS / (GetSystemTime() - GetSystemStartupTime()));
                }
                if (GetSystemTime() > time_last) {
                    pps_last = (int)((nn_msgs - nn_msgs_last) * UTIME_SECONDS / (GetSystemTime() - time_last));
                }
            }

            std::string pps_unit = "";
            if (pps_last > 10000 || pps_average > 10000) {
                pps_unit = "(w)"; pps_last /= 10000; pps_average /= 10000;
            } else if (pps_last > 1000 || pps_average > 1000) {
                pps_unit = "(k)"; pps_last /= 1000; pps_average /= 1000;
            }

            trace("<- RTC RECV #%d, udp %" PRId64 ", pps %d/%d%s, schedule %" PRId64,
                NetfdFileno(m_lfd), nn_msgs_stage, pps_average, pps_last, pps_unit.c_str(), nn_loop);
            nn_msgs_last = nn_msgs; time_last = GetSystemTime();
            nn_loop = 0; nn_msgs_stage = 0;
        }

        if (UdpPacketRecvCycleInterval > 0) {
            Usleep(UdpPacketRecvCycleInterval);
        }

        // Yield to another coroutines.
        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777485531
        if (++nn_msgs_for_yield > 10) {
            nn_msgs_for_yield = 0;
            ThreadYield();
        }
    }

    return err;
}

void UdpMuxListener::SetSocketBuffer()
{
    int default_sndbuf = 0;
    // TODO: FIXME: Config it.
    int expect_sndbuf = 1024*1024*10; // 10M
    int actual_sndbuf = expect_sndbuf;
    int r0_sndbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_sndbuf);
        getsockopt(Fd(), SOL_SOCKET, SO_SNDBUF, (void*)&default_sndbuf, &opt_len);

        if ((r0_sndbuf = setsockopt(Fd(), SOL_SOCKET, SO_SNDBUF, (void*)&actual_sndbuf, sizeof(actual_sndbuf))) < 0) {
            warn("set SO_SNDBUF failed, expect=%d, r0=%d", expect_sndbuf, r0_sndbuf);
        }

        opt_len = sizeof(actual_sndbuf);
        getsockopt(Fd(), SOL_SOCKET, SO_SNDBUF, (void*)&actual_sndbuf, &opt_len);
    }

    int default_rcvbuf = 0;
    // TODO: FIXME: Config it.
    int expect_rcvbuf = 1024*1024*10; // 10M
    int actual_rcvbuf = expect_rcvbuf;
    int r0_rcvbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_rcvbuf);
        getsockopt(Fd(), SOL_SOCKET, SO_RCVBUF, (void*)&default_rcvbuf, &opt_len);

        if ((r0_rcvbuf = setsockopt(Fd(), SOL_SOCKET, SO_RCVBUF, (void*)&actual_rcvbuf, sizeof(actual_rcvbuf))) < 0) {
            warn("set SO_RCVBUF failed, expect=%d, r0=%d", expect_rcvbuf, r0_rcvbuf);
        }

        opt_len = sizeof(actual_rcvbuf);
        getsockopt(Fd(), SOL_SOCKET, SO_RCVBUF, (void*)&actual_rcvbuf, &opt_len);
    }

    trace("UDP #%d LISTEN at %s:%d, SO_SNDBUF(default=%d, expect=%d, actual=%d, r0=%d), SO_RCVBUF(default=%d, expect=%d, actual=%d, r0=%d)",
        NetfdFileno(m_lfd), m_ip.c_str(), m_port, default_sndbuf, expect_sndbuf, actual_sndbuf, r0_sndbuf, default_rcvbuf, expect_rcvbuf, actual_rcvbuf, r0_rcvbuf);
}

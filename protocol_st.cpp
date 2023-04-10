#include "protocol_st.h"
#include "core_autofree.h"
#include "st/st.h"
#include <cstring>
#include <fcntl.h>
#include <netdb.h>

// nginx also set to 512
#define SERVER_LISTEN_BACKLOG 512

#ifdef __linux__
#include <sys/epoll.h>

bool StEpollIsSupported(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);

    return (errno != ENOSYS);
}
#endif

error StInit()
{
#ifdef __linux__
    // check epoll, some old linux donot support epoll.
    if (!StEpollIsSupported()) {
        return ERRORNEW(ERROR_ST_SET_EPOLL, "linux epoll disabled");
    }
#endif

    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue.
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        return ERRORNEW(ERROR_ST_SET_EPOLL, "st enable st failed, current is %s", st_get_eventsys_name());
    }

    // Before ST init, we might have already initialized the background cid.
    ContextId cid = Context->GetId();
    if (cid.Empty()) {
        cid = Context->GenerateId();
    }

    int r0 = 0;
    if((r0 = st_init()) != 0){
        return ERRORNEW(ERROR_ST_INITIALIZE, "st initialize failed, r0=%d", r0);
    }

    // Switch to the background cid.
    Context->SetId(cid);
    info("st_init success, use %s", st_get_eventsys_name());

    return SUCCESS;
}

void CloseStfd(netfd_t &stfd)
{
    if (stfd) {
        // we must ensure the close is ok.
        int r0 = st_netfd_close((st_netfd_t)stfd);
        if (r0) {
            // By _st_epoll_fd_close or _st_kq_fd_close
            if (errno == EBUSY) Assert(!r0);
            // By close
            if (errno == EBADF) Assert(!r0);
            if (errno == EINTR) Assert(!r0);
            if (errno == EIO) Assert(!r0);
            // Others
            Assert(!r0);
        }
        stfd = NULL;
    }
}

error FdCloseexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    flags |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, flags) == -1) {
        return ERRORNEW(ERROR_SOCKET_SETCLOSEEXEC, "FD_CLOEXEC fd=%d", fd);
    }

    return SUCCESS;
}

error FdReuseaddr(int fd)
{
    int v = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int)) == -1) {
        return ERRORNEW(ERROR_SOCKET_SETREUSEADDR, "SO_REUSEADDR fd=%d", fd);
    }

    return SUCCESS;
}

error FdReuseport(int fd)
{
#if defined(SO_REUSEPORT)
    int v = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(int)) == -1) {
        warn("SO_REUSEPORT failed for fd=%d", fd);
    }
#else
    #warning "SO_REUSEPORT is not supported by your OS"
    srs_warn("SO_REUSEPORT is not supported util Linux kernel 3.9");
#endif

    return SUCCESS;
}

error FdKeepalive(int fd)
{
#ifdef SO_KEEPALIVE
    int v = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof(int)) == -1) {
        return ERRORNEW(ERROR_SOCKET_SETKEEPALIVE, "SO_KEEPALIVE fd=%d", fd);
    }
#endif

    return SUCCESS;
}

thread_t ThreadSelf()
{
    return (thread_t)st_thread_self();
}

void ThreadExit(void *retval)
{
    st_thread_exit(retval);
}

int ThreadJoin(thread_t thread, void **retvalp)
{
    return st_thread_join((st_thread_t)thread, retvalp);
}

void ThreadInterrupt(thread_t thread)
{
    st_thread_interrupt((st_thread_t)thread);
}

void ThreadYield()
{
    st_thread_yield();
}

_ST_THREAD_CREATE_PFN _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)st_thread_create;

error TcpConnect(std::string server, int port, utime_t tm, netfd_t *pstfd)
{
    st_utime_t timeout = ST_UTIME_NO_TIMEOUT;
    if (tm != UTIME_NO_TIMEOUT) {
        timeout = tm;
    }

    *pstfd = NULL;
    netfd_t stfd = NULL;

    char sport[8];
    int r0 = snprintf(sport, sizeof(sport), "%d", port);
    Assert(r0 > 0 && r0 < (int)sizeof(sport));

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* r  = NULL;
    AutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(server.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return ERRORNEW(ERROR_SYSTEM_IP_INVALID, "get address info");
    }

    int sock = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
    if(sock == -1){
        return ERRORNEW(ERROR_SOCKET_CREATE, "create socket");
    }

    Assert(!stfd);
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ::close(sock);
        return ERRORNEW(ERROR_ST_OPEN_SOCKET, "open socket");
    }

    if (st_connect((st_netfd_t)stfd, r->ai_addr, r->ai_addrlen, timeout) == -1){
        CloseStfd(stfd);
        return ERRORNEW(ERROR_ST_CONNECT, "connect to %s:%d", server.c_str(), port);
    }

    *pstfd = stfd;
    return SUCCESS;
}

error DoTcpListen(int fd, addrinfo* r, netfd_t* pfd)
{
    error err = SUCCESS;

    // Detect alive for TCP connection.
    // @see https://github.com/ossrs/srs/issues/1044
    if ((err = FdKeepalive(fd)) != SUCCESS) {
        return ERRORWRAP(err, "set keepalive");
    }

    if ((err = FdCloseexec(fd)) != SUCCESS) {
        return ERRORWRAP(err, "set closeexec");
    }

    if ((err = FdReuseaddr(fd)) != SUCCESS) {
        return ERRORWRAP(err, "set reuseaddr");
    }

    if ((err = FdReuseport(fd)) != SUCCESS) {
        return ERRORWRAP(err, "set reuseport");
    }

    if (::bind(fd, r->ai_addr, r->ai_addrlen) == -1) {
        return ERRORNEW(ERROR_SOCKET_BIND, "bind");
    }

    if (::listen(fd, SERVER_LISTEN_BACKLOG) == -1) {
        return ERRORNEW(ERROR_SOCKET_LISTEN, "listen");
    }

    if ((*pfd = NetfdOpenSocket(fd)) == NULL){
        return ERRORNEW(ERROR_ST_OPEN_SOCKET, "st open");
    }

    return err;
}

error TcpListen(std::string ip, int port, netfd_t *pfd)
{
    error err = SUCCESS;

    char sport[8];
    int r0 = snprintf(sport, sizeof(sport), "%d", port);
    Assert(r0 > 0 && r0 < (int)sizeof(sport));

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICHOST;

    addrinfo* r = NULL;
    AutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(ip.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return ERRORNEW(ERROR_SYSTEM_IP_INVALID, "getaddrinfo hints=(%d,%d,%d)",
            hints.ai_family, hints.ai_socktype, hints.ai_flags);
    }

    int fd = 0;
    if ((fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
        return ERRORNEW(ERROR_SOCKET_CREATE, "socket domain=%d, type=%d, protocol=%d",
            r->ai_family, r->ai_socktype, r->ai_protocol);
    }

    if ((err = DoTcpListen(fd, r, pfd)) != SUCCESS) {
        ::close(fd);
        return ERRORWRAP(err, "fd=%d", fd);
    }

    return err;
}

error DoUdpListen(int fd, addrinfo* r, netfd_t* pfd)
{
    error err = SUCCESS;

    if ((err = FdCloseexec(fd)) != SUCCESS) {
        return ERRORWRAP(err, "set closeexec");
    }

    if ((err = FdReuseaddr(fd)) != SUCCESS) {
        return ERRORWRAP(err, "set reuseaddr");
    }

    if ((err = FdReuseport(fd)) != SUCCESS) {
        return ERRORWRAP(err, "set reuseport");
    }

    if (::bind(fd, r->ai_addr, r->ai_addrlen) == -1) {
        return ERRORNEW(ERROR_SOCKET_BIND, "bind");
    }

    if ((*pfd = NetfdOpenSocket(fd)) == NULL){
        return ERRORNEW(ERROR_ST_OPEN_SOCKET, "st open");
    }

    return err;
}

error UdpListen(std::string ip, int port, netfd_t *pfd)
{
    error err = SUCCESS;

    char sport[8];
    int r0 = snprintf(sport, sizeof(sport), "%d", port);
    Assert(r0 > 0 && r0 < (int)sizeof(sport));

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_NUMERICHOST;

    addrinfo* r  = NULL;
    AutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(ip.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return ERRORNEW(ERROR_SYSTEM_IP_INVALID, "getaddrinfo hints=(%d,%d,%d)",
            hints.ai_family, hints.ai_socktype, hints.ai_flags);
    }

    int fd = 0;
    if ((fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
        return ERRORNEW(ERROR_SOCKET_CREATE, "socket domain=%d, type=%d, protocol=%d",
            r->ai_family, r->ai_socktype, r->ai_protocol);
    }

    if ((err = DoUdpListen(fd, r, pfd)) != SUCCESS) {
        ::close(fd);
        return ERRORWRAP(err, "fd=%d", fd);
    }

    return err;
}

cond_t CondNew()
{
    return (cond_t)st_cond_new();
}

int CondDestroy(cond_t cond)
{
    return st_cond_destroy((st_cond_t)cond);
}

int CondWait(cond_t cond)
{
    return st_cond_wait((st_cond_t)cond);
}

int CondTimedwait(cond_t cond, utime_t timeout)
{
    return st_cond_timedwait((st_cond_t)cond, (st_utime_t)timeout);
}

int CondSignal(cond_t cond)
{
    return st_cond_signal((st_cond_t)cond);
}

int CondBroadcast(cond_t cond)
{
    return st_cond_broadcast((st_cond_t)cond);
}

mutex_t MutexNew()
{
    return (mutex_t)st_mutex_new();
}

int MutexDestroy(mutex_t mutex)
{
    if (!mutex) {
        return 0;
    }
    return st_mutex_destroy((st_mutex_t)mutex);
}

int MutexLock(mutex_t mutex)
{
    return st_mutex_lock((st_mutex_t)mutex);
}

int MutexUnlock(mutex_t mutex)
{
    return st_mutex_unlock((st_mutex_t)mutex);
}

int KeyCreate(int *keyp, void (*destructor)(void *))
{
    return st_key_create(keyp, destructor);
}

int ThreadSetspecific(int key, void *value)
{
    return st_thread_setspecific(key, value);
}

int ThreadSetspecific2(thread_t thread, int key, void *value)
{
    return st_thread_setspecific2((st_thread_t)thread, key, value);
}

void *ThreadGetspecific(int key)
{
    return st_thread_getspecific(key);
}

int NetfdFileno(netfd_t stfd)
{
    return st_netfd_fileno((st_netfd_t)stfd);
}

int Usleep(utime_t usecs)
{
    return st_usleep((st_utime_t)usecs);
}

netfd_t NetfdOpenSocket(int osfd)
{
    return (netfd_t)st_netfd_open_socket(osfd);
}

netfd_t NetfdOpen(int osfd)
{
    return (netfd_t)st_netfd_open(osfd);
}

int Recvfrom(netfd_t stfd, void *buf, int len, sockaddr *from, int *fromlen, utime_t timeout)
{
    return st_recvfrom((st_netfd_t)stfd, buf, len, from, fromlen, (st_utime_t)timeout);
}

int Sendto(netfd_t stfd, void *buf, int len, const sockaddr *to, int tolen, utime_t timeout)
{
    return st_sendto((st_netfd_t)stfd, buf, len, to, tolen, (st_utime_t)timeout);
}

int Recvmsg(netfd_t stfd, msghdr *msg, int flags, utime_t timeout)
{
    return st_recvmsg((st_netfd_t)stfd, msg, flags, (st_utime_t)timeout);
}

int Sendmsg(netfd_t stfd, const msghdr *msg, int flags, utime_t timeout)
{
    return st_sendmsg((st_netfd_t)stfd, msg, flags, (st_utime_t)timeout);
}

netfd_t Accept(netfd_t stfd, sockaddr *addr, int *addrlen, utime_t timeout)
{
    return (netfd_t)st_accept((st_netfd_t)stfd, addr, addrlen, (st_utime_t)timeout);
}

ssize_t Read(netfd_t stfd, void *buf, size_t nbyte, utime_t timeout)
{
    return st_read((st_netfd_t)stfd, buf, nbyte, (st_utime_t)timeout);
}

bool IsNeverTimeout(utime_t tm)
{
    return tm == UTIME_NO_TIMEOUT;
}

StSocket::StSocket()
{
    Init(nullptr);
}

StSocket::StSocket(netfd_t fd)
{
    Init(fd);
}

StSocket::~StSocket()
{

}

void StSocket::Init(netfd_t fd)
{
    m_stfd = fd;
    m_stm = m_rtm = UTIME_NO_TIMEOUT;
    m_rbytes = m_sbytes = 0;
}

void StSocket::SetRecvTimeout(utime_t tm)
{
    m_rtm = tm;
}

utime_t StSocket::GetRecvTimeout()
{
    return m_rtm;
}

void StSocket::SetSendTimeout(utime_t tm)
{
    m_stm = tm;
}

utime_t StSocket::GetSendTimeout()
{
    return m_stm;
}

int64_t StSocket::GetRecvBytes()
{
    return m_rbytes;
}

int64_t StSocket::GetSendBytes()
{
    return m_sbytes;
}

error StSocket::Read(void *buf, size_t size, ssize_t *nread)
{
    error err = SUCCESS;

    Assert(m_stfd);

    ssize_t nb_read;
    if (m_rtm == UTIME_NO_TIMEOUT) {
        nb_read = st_read((st_netfd_t)m_stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read((st_netfd_t)m_stfd, buf, size, m_rtm);
    }

    if (nread) {
        *nread = nb_read;
    }

    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value of 0 means the network connection is closed or end of file is reached).
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read <= 0) {
        if (nb_read < 0 && errno == ETIME) {
            return ERRORNEW(ERROR_SOCKET_TIMEOUT, "timeout %d ms", u2msi(m_rtm));
        }

        if (nb_read == 0) {
            errno = ECONNRESET;
        }

        return ERRORNEW(ERROR_SOCKET_READ, "read");
    }

    m_rbytes += nb_read;

    return err;
}

error StSocket::ReadFully(void *buf, size_t size, ssize_t *nread)
{
    error err = SUCCESS;

    Assert(m_stfd);

    ssize_t nb_read;
    if (m_rtm == UTIME_NO_TIMEOUT) {
        nb_read = st_read_fully((st_netfd_t)m_stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read_fully((st_netfd_t)m_stfd, buf, size, m_rtm);
    }

    if (nread) {
        *nread = nb_read;
    }

    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value less than nbyte means the network connection is closed or end of file is reached)
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read != (ssize_t)size) {
        if (nb_read < 0 && errno == ETIME) {
            return ERRORNEW(ERROR_SOCKET_TIMEOUT, "timeout %d ms", u2msi(m_rtm));
        }

        if (nb_read >= 0) {
            errno = ECONNRESET;
        }

        return ERRORNEW(ERROR_SOCKET_READ_FULLY, "read fully, size=%d, nn=%d", size, nb_read);
    }

    m_rbytes += nb_read;

    return err;
}

error StSocket::Write(void *buf, size_t size, ssize_t *nwrite)
{
    error err = SUCCESS;

    Assert(m_stfd);

    ssize_t nb_write;
    if (m_stm == UTIME_NO_TIMEOUT) {
        nb_write = st_write((st_netfd_t)m_stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_write((st_netfd_t)m_stfd, buf, size, m_stm);
    }

    if (nwrite) {
        *nwrite = nb_write;
    }

    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return ERRORNEW(ERROR_SOCKET_TIMEOUT, "write timeout %d ms", u2msi(m_stm));
        }

        return ERRORNEW(ERROR_SOCKET_WRITE, "write");
    }

    m_sbytes += nb_write;

    return err;
}

error StSocket::Writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    error err = SUCCESS;

    Assert(m_stfd);

    ssize_t nb_write;
    if (m_stm == UTIME_NO_TIMEOUT) {
        nb_write = st_writev((st_netfd_t)m_stfd, iov, iov_size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_writev((st_netfd_t)m_stfd, iov, iov_size, m_stm);
    }

    if (nwrite) {
        *nwrite = nb_write;
    }

    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return ERRORNEW(ERROR_SOCKET_TIMEOUT, "writev timeout %d ms", u2msi(m_stm));
        }

        return ERRORNEW(ERROR_SOCKET_WRITE, "writev");
    }

    m_sbytes += nb_write;

    return err;
}

TcpClient::TcpClient(std::string h, int p, utime_t tm)
{
    m_stfd = NULL;
    m_io = new StSocket();

    m_host = h;
    m_port = p;
    m_timeout = tm;
}

TcpClient::~TcpClient()
{
    Freep(m_io);
    CloseStfd(m_stfd);
}

error TcpClient::Connect()
{
    error err = SUCCESS;

    netfd_t stfd = NULL;
    if ((err = TcpConnect(m_host, m_port, m_timeout, &stfd)) != SUCCESS) {
        return ERRORWRAP(err, "tcp: connect %s:%d to=%dms", m_host.c_str(), m_port, u2msi(m_timeout));
    }

    // TODO: FIMXE: The timeout set on io need to be set to new object.
    Freep(m_io);
    m_io = new StSocket(stfd);

    CloseStfd(m_stfd);
    m_stfd = stfd;

    return err;
}

void TcpClient::SetRecvTimeout(utime_t tm)
{
    m_io->SetRecvTimeout(tm);
}

utime_t TcpClient::GetRecvTimeout()
{
    return m_io->GetRecvTimeout();
}

void TcpClient::SetSendTimeout(utime_t tm)
{
    m_io->SetSendTimeout(tm);
}

utime_t TcpClient::GetSendTimeout()
{
    return m_io->GetSendTimeout();
}

int64_t TcpClient::GetRecvBytes()
{
    return m_io->GetRecvBytes();
}

int64_t TcpClient::GetSendBytes()
{
    return m_io->GetSendBytes();
}

error TcpClient::Read(void *buf, size_t size, ssize_t *nread)
{
    return m_io->Read(buf, size, nread);
}

error TcpClient::ReadFully(void *buf, size_t size, ssize_t *nread)
{
    return m_io->ReadFully(buf, size, nread);
}

error TcpClient::Write(void *buf, size_t size, ssize_t *nwrite)
{
    return m_io->Write(buf, size, nwrite);
}

error TcpClient::Writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return m_io->Writev(iov, iov_size, nwrite);
}

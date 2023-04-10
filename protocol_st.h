#ifndef PROTOCOL_ST_H
#define PROTOCOL_ST_H

#include "core_time.h"
#include "error.h"
#include "log.h"
#include "protocol_io.h"
#include <bits/types/struct_iovec.h>

typedef void* netfd_t;
typedef void* thread_t;
typedef void* cond_t;
typedef void* mutex_t;

//initialize st, requires epoll
extern error StInit();

//close the netfd, and close the underlayer fd.
extern void CloseStfd(netfd_t& stfd);

//set the FD_CLOEXEC of FD
extern error FdCloseexec(int fd);

//set the SO_REUSEADDR of fd
extern error FdReuseaddr(int fd);

//set the SO_REUSEPORT of fd
extern error FdReuseport(int fd);

//set the SO_KEEPALIVE of fd
extern error FdKeepalive(int fd);

//get current coroutine/thread
extern thread_t ThreadSelf();
extern void ThreadExit(void* retval);
extern int ThreadJoin(thread_t thread, void **retvalp);
extern void ThreadInterrupt(thread_t thread);
extern void ThreadYield();

//for utest to mock the thread create
typedef void* (*_ST_THREAD_CREATE_PFN)(void *(*start)(void *arg), void *arg, int joinable, int stack_size);
extern _ST_THREAD_CREATE_PFN _pfn_st_thread_create;

//for client, to open socket and connect to server.
//@param tm the timeout in utime_t
extern error TcpConnect(std::string server, int port, utime_t tm, netfd_t* pstfd);

//for server, listen at tcp endpoint
extern error TcpListen(std::string ip, int port, netfd_t* pfd);

//for server, listen at udp endpoint
extern error UdpListen(std::string ip, int port, netfd_t* pfd);

//wrap for coroutine
extern cond_t CondNew();
extern int CondDestroy(cond_t cond);
extern int CondWait(cond_t cond);
extern int CondTimedwait(cond_t cond, utime_t timeout);
extern int CondSignal(cond_t cond);
extern int CondBroadcast(cond_t cond);

extern mutex_t MutexNew();
extern int MutexDestroy(mutex_t mutex);
extern int MutexLock(mutex_t mutex);
extern int MutexUnlock(mutex_t mutex);

extern int KeyCreate(int* keyp, void (*destructor)(void*));
extern int ThreadSetspecific(int key, void* value);
extern int ThreadSetspecific2(thread_t thread, int key, void* value);
extern void* ThreadGetspecific(int key);

extern int NetfdFileno(netfd_t stfd);

extern int Usleep(utime_t usecs);

extern netfd_t NetfdOpenSocket(int osfd);
extern netfd_t NetfdOpen(int osfd);

extern int Recvfrom(netfd_t stfd, void *buf, int len, struct sockaddr *from, int *fromlen, utime_t timeout);
extern int Sendto(netfd_t stfd, void *buf, int len, const struct sockaddr *to, int tolen, utime_t timeout);
extern int Recvmsg(netfd_t stfd, struct msghdr *msg, int flags, utime_t timeout);
extern int Sendmsg(netfd_t stfd, const struct msghdr *msg, int flags, utime_t timeout);

extern netfd_t Accept(netfd_t stfd, struct sockaddr *addr, int *addrlen, utime_t timeout);

extern ssize_t Read(netfd_t stfd, void *buf, size_t nbyte, utime_t timeout);

extern bool IsNeverTimeout(utime_t tm);

// The mutex locker.
#define Locker(instance) \
    impl__Locker free_##instance(&instance)

class impl__Locker
{
private:
    mutex_t* m_lock;
public:
    impl__Locker(mutex_t* l) {
        m_lock = l;
        int r0 = MutexLock(*m_lock);
        Assert(!r0);
    }
    virtual ~impl__Locker() {
        int r0 = MutexUnlock(*m_lock);
        Assert(!r0);
    }
};

// the socket provides TCP socket over st,
// that is, the sync socket mechanism.
class StSocket : public IProtocolReadWriter
{
private:
    // The recv/send timeout in srs_utime_t.
    // @remark Use SRS_UTIME_NO_TIMEOUT for never timeout.
    utime_t m_rtm;
    utime_t m_stm;
    // The recv/send data in bytes
    int64_t m_rbytes;
    int64_t m_sbytes;
    // The underlayer st fd.
    netfd_t m_stfd;
public:
    StSocket();
    StSocket(netfd_t fd);
    virtual ~StSocket();
private:
    void Init(netfd_t fd);
public:
    virtual void SetRecvTimeout(utime_t tm);
    virtual utime_t GetRecvTimeout();
    virtual void SetSendTimeout(utime_t tm);
    virtual utime_t GetSendTimeout();
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
public:
    // @param nread, the actual read bytes, ignore if NULL.
    virtual error Read(void* buf, size_t size, ssize_t* nread);
    virtual error ReadFully(void* buf, size_t size, ssize_t* nread);
    // @param nwrite, the actual write bytes, ignore if NULL.
    virtual error Write(void* buf, size_t size, ssize_t* nwrite);
    virtual error Writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

// The client to connect to server over TCP.
// User must never reuse the client when close it.
// Usage:
//      SrsTcpClient client("127.0.0.1", 1935, 9 * SRS_UTIME_SECONDS);
//      client.connect();
//      client.write("Hello world!", 12, NULL);
//      client.read(buf, 4096, NULL);
// @remark User can directly free the object, which will close the fd.
class TcpClient : public IProtocolReadWriter
{
private:
    netfd_t m_stfd;
    StSocket* m_io;
private:
    std::string m_host;
    int m_port;
    // The timeout in srs_utime_t.
    utime_t m_timeout;
public:
    // Constructor.
    // @param h the ip or hostname of server.
    // @param p the port to connect to.
    // @param tm the timeout in srs_utime_t.
    TcpClient(std::string h, int p, utime_t tm);
    virtual ~TcpClient();
public:
    // Connect to server over TCP.
    // @remark We will close the exists connection before do connect.
    virtual error Connect();
// Interface ISrsProtocolReadWriter
public:
    virtual void SetRecvTimeout(utime_t tm);
    virtual utime_t GetRecvTimeout();
    virtual void SetSendTimeout(utime_t tm);
    virtual utime_t GetSendTimeout();
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
    virtual error Read(void* buf, size_t size, ssize_t* nread);
    virtual error ReadFully(void* buf, size_t size, ssize_t* nread);
    virtual error Write(void* buf, size_t size, ssize_t* nwrite);
    virtual error Writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

#endif // PROTOCOL_ST_H

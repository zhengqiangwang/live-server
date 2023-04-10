#ifndef APP_CONN_H
#define APP_CONN_H


#include "app_st.h"
#include "protocol_conn.h"
#include "protocol_st.h"
#include <map>
#include <string>
#include <vector>

#include <openssl/ssl.h>

class WallClock;
class Buffer;

// Hooks for connection manager, to handle the event when disposing connections.
class IDisposingHandler
{
public:
    IDisposingHandler();
    virtual ~IDisposingHandler();
public:
    // When before disposing resource, trigger when manager.remove(c), sync API.
    // @remark Recommend to unref c, after this, no other objects refs to c.
    virtual void OnBeforeDispose(IResource* c) = 0;
    // When disposing resource, async API, c is freed after it.
    // @remark Recommend to stop any thread/timer of c, after this, fields of c is able
    // to be deleted in any order.
    virtual void OnDisposing(IResource* c) = 0;
};

// The item to identify the fast id object.
class ResourceFastIdItem
{
public:
    // If available, use the resource in item.
    bool m_available;
    // How many resource have the same fast-id, which contribute a collision.
    int m_nnCollisions;
    // The first fast-id of resources.
    uint64_t m_fastId;
    // The first resource object.
    IResource* m_impl;
public:
    ResourceFastIdItem() {
        m_available = false;
        m_nnCollisions = 0;
        m_fastId = 0;
        m_impl = NULL;
    }
};

// The resource manager remove resource and delete it asynchronously.
class ResourceManager : public ICoroutineHandler, public IResourceManager
{
private:
    std::string m_label;
    ContextId m_cid;
    bool m_verbose;
private:
    Coroutine* m_trd;
    cond_t m_cond;
    // Callback handlers.
    std::vector<IDisposingHandler*> m_handlers;
    // Unsubscribing handlers, skip it for notifying.
    std::vector<IDisposingHandler*> m_unsubs;
    // Whether we are removing resources.
    bool m_removing;
    // The zombie connections, we will delete it asynchronously.
    std::vector<IResource*> m_zombies;
    std::vector<IResource*>* m_pDisposing;
private:
    // The connections without any id.
    std::vector<IResource*> m_conns;
    // The connections with resource id.
    std::map<std::string, IResource*> m_connsId;
    // The connections with resource fast(int) id.
    std::map<uint64_t, IResource*> m_connsFastId;
    // The level-0 fast cache for fast id.
    int m_nnLevel0Cache;
    ResourceFastIdItem* m_connsLevel0Cache;
    // The connections with resource name.
    std::map<std::string, IResource*> m_connsName;
public:
    ResourceManager(const std::string& label, bool verbose = false);
    virtual ~ResourceManager();
public:
    error Start();
    bool Empty();
    size_t Size();
// Interface ICoroutineHandler
public:
    virtual error Cycle();
public:
    void Add(IResource* conn, bool* exists = NULL);
    void AddWithId(const std::string& id, IResource* conn);
    void AddWithFastId(uint64_t id, IResource* conn);
    void AddWithName(const std::string& name, IResource* conn);
    IResource* At(int index);
    IResource* FindById(std::string id);
    IResource* FindByFastId(uint64_t id);
    IResource* FindByName(std::string name);
public:
    void Subscribe(IDisposingHandler* h);
    void Unsubscribe(IDisposingHandler* h);
// Interface IResourceManager
public:
    virtual void Remove(IResource* c);
private:
    void DoRemove(IResource* c);
    void CheckRemove(IResource* c, bool& in_zombie, bool& in_disposing);
    void Clear();
    void DoClear();
    void Dispose(IResource* c);
};

// A simple lazy-sweep GC, just wait for a long time to delete the disposable resources.
class LazySweepGc : public ILazyGc
{
public:
    LazySweepGc();
    virtual ~LazySweepGc();
public:
    virtual error Start();
    virtual void Remove(LazyObject* c);
};

extern ILazyGc* _gc;

// A wrapper template for lazy-sweep resource.
// See https://github.com/ossrs/srs/issues/3176#lazy-sweep
template<typename T>
class LazyObjectWrapper : public IResource
{
private:
    T* m_resource;
    bool m_isRoot;
public:
    LazyObjectWrapper(T* resource = NULL, IResource* wrapper = NULL) {
        m_resource = resource ? resource : new T();
        m_resource->GcUse();

        m_isRoot = !resource;
        if (!resource) {
            m_resource->gc_set_creator_wrapper(wrapper ? wrapper : this);
        }
    }
    virtual ~LazyObjectWrapper() {
        m_resource->gc_dispose();

        if (m_isRoot) {
            m_resource->gc_set_creator_wrapper(NULL);
        }

        if (m_resource->gc_ref() == 0) {
            _gc->Remove(m_resource);
        }
    }
public:
    ::LazyObjectWrapper<T>* Copy() {
        return new LazyObjectWrapper<T>(m_resource);
    }
    T* Resource() {
        return m_resource;
    }
// Interface IResource
public:
    virtual const ContextId& GetId() {
        return m_resource->get_id();
    }
    virtual std::string Desc() {
        return m_resource->desc();
    }
};

// Use macro to generate a wrapper class, because typedef will cause IDE incorrect tips.
// See https://github.com/ossrs/srs/issues/3176#lazy-sweep
#define LAZY_WRAPPER_GENERATOR(Resource, IWrapper, IResource) \
    private: \
        SrsLazyObjectWrapper<Resource> impl_; \
    public: \
        Resource##Wrapper(Resource* resource = NULL) : impl_(resource, this) { \
        } \
        virtual ~Resource##Wrapper() { \
        } \
    public: \
        IWrapper* copy() { \
            return new Resource##Wrapper(impl_.resource()); \
        } \
        IResource* resource() { \
            return impl_.resource(); \
        } \
    public: \
        virtual const SrsContextId& get_id() { \
            return impl_.get_id(); \
        } \
        virtual std::string desc() { \
            return impl_.desc(); \
        } \

// If a connection is able be expired, user can use HTTP-API to kick-off it.
class IExpire
{
public:
    IExpire();
    virtual ~IExpire();
public:
    // Set connection to expired to kick-off it.
    virtual void Expire() = 0;
};

// The basic connection of SRS, for TCP based protocols,
// all connections accept from listener must extends from this base class,
// server will add the connection to manager, and delete it when remove.
class TcpConnection : public IProtocolReadWriter
{
private:
    // The underlayer st fd handler.
    netfd_t m_stfd;
    // The underlayer socket.
    StSocket* m_skt;
public:
    TcpConnection(netfd_t c);
    virtual ~TcpConnection();
public:
    // Set socket option TCP_NODELAY.
    virtual error SetTcpNodelay(bool v);
    // Set socket option SO_SNDBUF in utime_t.
    virtual error SetSocketBuffer(utime_t buffer_v);
// Interface IProtocolReadWriter
public:
    virtual void SetRecvTimeout(utime_t tm);
    virtual utime_t GetRecvTimeout();
    virtual error ReadFully(void* buf, size_t size, ssize_t* nread);
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
    virtual error Read(void* buf, size_t size, ssize_t* nread);
    virtual void SetSendTimeout(utime_t tm);
    virtual utime_t GetSendTimeout();
    virtual error Write(void* buf, size_t size, ssize_t* nwrite);
    virtual error Writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

// With a small fast read buffer, to support peek for protocol detecting. Note that directly write to io without any
// cache or buffer.
class BufferedReadWriter : public IProtocolReadWriter
{
private:
    // The under-layer transport.
    IProtocolReadWriter* m_io;
    // Fixed, small and fast buffer. Note that it must be very small piece of cache, make sure matches all protocols,
    // because we will full fill it when peeking.
    char m_cache[16];
    // Current reading position.
    Buffer* m_buf;
public:
    BufferedReadWriter(IProtocolReadWriter* io);
    virtual ~BufferedReadWriter();
public:
    // Peek the head of cache to buf in size of bytes.
    error Peek(char* buf, int* size);
private:
    error ReloadBuffer();
// Interface IProtocolReadWriter
public:
    virtual error Read(void* buf, size_t size, ssize_t* nread);
    virtual error ReadFully(void* buf, size_t size, ssize_t* nread);
    virtual void SetRecvTimeout(utime_t tm);
    virtual utime_t GetRecvTimeout();
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
    virtual void SetSendTimeout(utime_t tm);
    virtual utime_t GetSendTimeout();
    virtual error Write(void* buf, size_t size, ssize_t* nwrite);
    virtual error Writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

// The SSL connection over TCP transport, in server mode.
class SslConnection : public IProtocolReadWriter
{
private:
    // The under-layer plaintext transport.
    IProtocolReadWriter* m_transport;
private:
    SSL_CTX* m_sslCtx;
    SSL* m_ssl;
    BIO* m_bioIn;
    BIO* m_bioOut;
public:
    SslConnection(IProtocolReadWriter* c);
    virtual ~SslConnection();
public:
    virtual error Handshake(std::string key_file, std::string crt_file);
// Interface IProtocolReadWriter
public:
    virtual void SetRecvTimeout(utime_t tm);
    virtual utime_t GetRecvTimeout();
    virtual error ReadFully(void* buf, size_t size, ssize_t* nread);
    virtual int64_t GetRecvBytes();
    virtual int64_t GetSendBytes();
    virtual error Read(void* buf, size_t size, ssize_t* nread);
    virtual void SetSendTimeout(utime_t tm);
    virtual utime_t GetSendTimeout();
    virtual error Write(void* buf, size_t size, ssize_t* nwrite);
    virtual error Writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

#endif // APP_CONN_H

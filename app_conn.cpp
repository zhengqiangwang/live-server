#include "app_conn.h"
#include "buffer.h"
#include "kbps.h"
#include "protocol_log.h"
#include "app_log.h"
#include "utility.h"
#include "core_autofree.h"

#include <algorithm>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>

Pps* pps_ids = NULL;
Pps* pps_fids = NULL;
Pps* pps_fids_level0 = NULL;
Pps* pps_dispose = NULL;


IDisposingHandler::IDisposingHandler()
{

}

IDisposingHandler::~IDisposingHandler()
{

}

ResourceManager::ResourceManager(const std::string &label, bool verbose)
{
    m_verbose = verbose;
    m_label = label;
    m_cond = CondNew();
    m_trd = NULL;
    m_pDisposing = NULL;
    m_removing = false;

    m_nnLevel0Cache = 100000;
    m_connsLevel0Cache = new ResourceFastIdItem[m_nnLevel0Cache];
}

ResourceManager::~ResourceManager()
{
    if (m_trd) {
        CondSignal(m_cond);
        m_trd->Stop();

        Freep(m_trd);
        CondDestroy(m_cond);
    }

    Clear();

    Freepa(m_connsLevel0Cache);
}

error ResourceManager::Start()
{
    error err = SUCCESS;

    m_cid = Context->GenerateId();
    m_trd = new STCoroutine("manager", this, m_cid);

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "conn manager");
    }

    return err;
}

bool ResourceManager::Empty()
{
    return m_conns.empty();
}

size_t ResourceManager::Size()
{
    return m_conns.size();
}

error ResourceManager::Cycle()
{
    error err = SUCCESS;

    trace("%s: connection manager run, conns=%d", m_label.c_str(), (int)m_conns.size());

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "conn manager");
        }

        // Clear all zombies, because we may switch context and lost signal
        // when we clear zombie connection.
        while (!m_zombies.empty()) {
            Clear();
        }

        CondWait(m_cond);
    }

    return err;
}

void ResourceManager::Add(IResource *conn, bool *exists)
{
    if (std::find(m_conns.begin(), m_conns.end(), conn) == m_conns.end()) {
        m_conns.push_back(conn);
    } else {
        if (exists) {
            *exists = true;
        }
    }
}

void ResourceManager::AddWithId(const std::string &id, IResource *conn)
{
    Add(conn);
    m_connsId[id] = conn;
}

void ResourceManager::AddWithFastId(uint64_t id, IResource *conn)
{
    bool exists = false;
    Add(conn, &exists);
    m_connsFastId[id] = conn;

    if (exists) {
        return;
    }

    // For new resource, build the level-0 cache for fast-id.
    ResourceFastIdItem* item = &m_connsLevel0Cache[(id | id>>32) % m_nnLevel0Cache];

    // Ignore if exits item.
    if (item->m_fastId && item->m_fastId == id) {
        return;
    }

    // Fresh one, create the item.
    if (!item->m_fastId) {
        item->m_fastId = id;
        item->m_impl = conn;
        item->m_nnCollisions = 1;
        item->m_available = true;
    }

    // Collision, increase the collisions.
    if (item->m_fastId != id) {
        item->m_nnCollisions++;
        item->m_available = false;
    }
}

void ResourceManager::AddWithName(const std::string &name, IResource *conn)
{
    Add(conn);
    m_connsName[name] = conn;
}

IResource *ResourceManager::At(int index)
{
    return (index < (int)m_conns.size())? m_conns.at(index) : NULL;
}

IResource *ResourceManager::FindById(std::string id)
{
    ++pps_ids->m_sugar;
    std::map<std::string, IResource*>::iterator it = m_connsId.find(id);
    return (it != m_connsId.end())? it->second : NULL;
}

IResource *ResourceManager::FindByFastId(uint64_t id)
{
    ResourceFastIdItem* item = &m_connsLevel0Cache[(id | id>>32) % m_nnLevel0Cache];
    if (item->m_available && item->m_fastId == id) {
        ++pps_fids_level0->m_sugar;
        return item->m_impl;
    }

    ++pps_fids->m_sugar;
    std::map<uint64_t, IResource*>::iterator it = m_connsFastId.find(id);
    return (it != m_connsFastId.end())? it->second : NULL;
}

IResource *ResourceManager::FindByName(std::string name)
{
    ++pps_ids->m_sugar;
    std::map<std::string, IResource*>::iterator it = m_connsName.find(name);
    return (it != m_connsName.end())? it->second : NULL;
}

void ResourceManager::Subscribe(IDisposingHandler *h)
{

    if (std::find(m_handlers.begin(), m_handlers.end(), h) == m_handlers.end()) {
        m_handlers.push_back(h);
    }

    // Restore the handler from unsubscribing handlers.
    std::vector<IDisposingHandler*>::iterator it;
    if ((it = std::find(m_unsubs.begin(), m_unsubs.end(), h)) != m_unsubs.end()) {
        it = m_unsubs.erase(it);
    }
}

void ResourceManager::Unsubscribe(IDisposingHandler *h)
{
    std::vector<IDisposingHandler*>::iterator it = find(m_handlers.begin(), m_handlers.end(), h);
    if (it != m_handlers.end()) {
        it = m_handlers.erase(it);
    }

    // Put it to the unsubscribing handlers.
    if (std::find(m_unsubs.begin(), m_unsubs.end(), h) == m_unsubs.end()) {
        m_unsubs.push_back(h);
    }
}

void ResourceManager::Remove(IResource *c)
{
    ContextRestore(Context->GetId());

    m_removing = true;
    DoRemove(c);
    m_removing = false;
}

void ResourceManager::DoRemove(IResource *c)
{
    bool in_zombie = false;
    bool in_disposing = false;
    CheckRemove(c, in_zombie, in_disposing);
    bool ignored = in_zombie || in_disposing;

    if (m_verbose) {
        Context->SetId(c->GetId());
        trace("%s: before dispose resource(%s)(%p), conns=%d, zombies=%d, ign=%d, inz=%d, ind=%d",
            m_label.c_str(), c->Desc().c_str(), c, (int)m_conns.size(), (int)m_zombies.size(), ignored,
            in_zombie, in_disposing);
    }
    if (ignored) {
        return;
    }

    // Push to zombies, we will free it in another coroutine.
    m_zombies.push_back(c);

    // We should copy all handlers, because it may change during callback.
    std::vector<IDisposingHandler*> handlers = m_handlers;

    // Notify other handlers to handle the before-dispose event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        IDisposingHandler* h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!m_unsubs.empty() && std::find(m_unsubs.begin(), m_unsubs.end(), h) != m_unsubs.end()) {
            warn2(TAG_RESOURCE_UNSUB, "%s: ignore before-dispose resource(%s)(%p) for %p, conns=%d",
                m_label.c_str(), c->Desc().c_str(), c, h, (int)m_conns.size());
            continue;
        }

        h->OnBeforeDispose(c);
    }

    // Notify the coroutine to free it.
    CondSignal(m_cond);
}

void ResourceManager::CheckRemove(IResource *c, bool &in_zombie, bool &in_disposing)
{
    // Only notify when not removed(in m_zombies).
    std::vector<IResource*>::iterator it = std::find(m_zombies.begin(), m_zombies.end(), c);
    if (it != m_zombies.end()) {
        in_zombie = true;
    }

    // Also ignore when we are disposing it.
    if (m_pDisposing) {
        it = std::find(m_pDisposing->begin(), m_pDisposing->end(), c);
        if (it != m_pDisposing->end()) {
            in_disposing = true;
        }
    }
}

void ResourceManager::Clear()
{
    if (m_zombies.empty()) {
        return;
    }

    ContextRestore(m_cid);
    if (m_verbose) {
        trace("%s: clear zombies=%d resources, conns=%d, removing=%d, unsubs=%d",
            m_label.c_str(), (int)m_zombies.size(), (int)m_conns.size(), m_removing, (int)m_unsubs.size());
    }

    // Clear all unsubscribing handlers, if not removing any resource.
    if (!m_removing && !m_unsubs.empty()) {
        std::vector<IDisposingHandler*>().swap(m_unsubs);
    }

    DoClear();
}

void ResourceManager::DoClear()
{
    // To prevent thread switch when delete connection,
    // we copy all connections then free one by one.
    std::vector<IResource*> copy;
    copy.swap(m_zombies);
    m_pDisposing = &copy;

    for (int i = 0; i < (int)copy.size(); i++) {
        IResource* conn = copy.at(i);

        if (m_verbose) {
            Context->SetId(conn->GetId());
            trace("%s: disposing #%d resource(%s)(%p), conns=%d, disposing=%d, zombies=%d", m_label.c_str(),
                i, conn->Desc().c_str(), conn, (int)m_conns.size(), (int)copy.size(), (int)m_zombies.size());
        }

        ++pps_dispose->m_sugar;

        Dispose(conn);
    }

    // Reset it for it points to a local object.
    // @remark We must set the disposing to NULL to avoid reusing address,
    // because the context might switch.
    m_pDisposing = NULL;

    // We should free the resources when finished all disposing callbacks,
    // which might cause context switch and reuse the freed addresses.
    for (int i = 0; i < (int)copy.size(); i++) {
        IResource* conn = copy.at(i);
        Freep(conn);
    }
}

void ResourceManager::Dispose(IResource *c)
{
    for (std::map<std::string, IResource*>::iterator it = m_connsName.begin(); it != m_connsName.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            m_connsName.erase(it++);
        }
    }

    for (std::map<std::string, IResource*>::iterator it = m_connsId.begin(); it != m_connsId.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            m_connsId.erase(it++);
        }
    }

    for (std::map<uint64_t, IResource*>::iterator it = m_connsFastId.begin(); it != m_connsFastId.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Update the level-0 cache for fast-id.
            uint64_t id = it->first;
            ResourceFastIdItem* item = &m_connsLevel0Cache[(id | id>>32) % m_nnLevel0Cache];
            item->m_nnCollisions--;
            if (!item->m_nnCollisions) {
                item->m_fastId = 0;
                item->m_available = false;
            }

            // Use C++98 style: https://stackoverflow.com/a/4636230
            m_connsFastId.erase(it++);
        }
    }

    std::vector<IResource*>::iterator it = std::find(m_conns.begin(), m_conns.end(), c);
    if (it != m_conns.end()) {
        it = m_conns.erase(it);
    }

    // We should copy all handlers, because it may change during callback.
    std::vector<IDisposingHandler*> handlers = m_handlers;

    // Notify other handlers to handle the disposing event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        IDisposingHandler* h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!m_unsubs.empty() && std::find(m_unsubs.begin(), m_unsubs.end(), h) != m_unsubs.end()) {
            warn2(TAG_RESOURCE_UNSUB, "%s: ignore disposing resource(%s)(%p) for %p, conns=%d",
                m_label.c_str(), c->Desc().c_str(), c, h, (int)m_conns.size());
            continue;
        }

        h->OnDisposing(c);
    }
}

LazySweepGc::LazySweepGc()
{

}

LazySweepGc::~LazySweepGc()
{

}

error LazySweepGc::Start()
{
    error err = SUCCESS;
    return err;
}

void LazySweepGc::Remove(LazyObject *c)
{
    // TODO: FIXME: MUST lazy sweep.
    Freep(c);
}

ILazyGc* _gc = nullptr;

IExpire::IExpire()
{

}

IExpire::~IExpire()
{

}

TcpConnection::TcpConnection(netfd_t c)
{
    m_stfd = c;
    m_skt = new StSocket(c);
}

TcpConnection::~TcpConnection()
{
    Freep(m_skt);
    CloseStfd(m_stfd);
}

error TcpConnection::SetTcpNodelay(bool v)
{
    error err = SUCCESS;

    int r0 = 0;
    socklen_t nb_v = sizeof(int);
    int fd = NetfdFileno(m_stfd);

    int ov = 0;
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &ov, &nb_v)) != 0) {
        return ERRORNEW(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }

#ifndef SRS_PERF_TCP_NODELAY
    warn("ignore TCP_NODELAY, fd=%d, ov=%d", fd, ov);
    return err;
#endif

    int iv = (v? 1:0);
    if ((r0 = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &iv, nb_v)) != 0) {
        return ERRORNEW(ERROR_SOCKET_NO_NODELAY, "setsockopt fd=%d, r0=%d", fd, r0);
    }
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &iv, &nb_v)) != 0) {
        return ERRORNEW(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }

    trace("set fd=%d TCP_NODELAY %d=>%d", fd, ov, iv);

    return err;
}

error TcpConnection::SetSocketBuffer(utime_t buffer_v)
{
    error err = SUCCESS;

    int r0 = 0;
    int fd = NetfdFileno(m_stfd);
    socklen_t nb_v = sizeof(int);

    int ov = 0;
    if ((r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &ov, &nb_v)) != 0) {
        return ERRORNEW(ERROR_SOCKET_SNDBUF, "getsockopt fd=%d, r0=%d", fd, r0);
    }

#ifndef SRS_PERF_MW_SO_SNDBUF
    warn("ignore SO_SNDBUF, fd=%d, ov=%d", fd, ov);
    return err;
#endif

    // the bytes:
    //      4KB=4096, 8KB=8192, 16KB=16384, 32KB=32768, 64KB=65536,
    //      128KB=131072, 256KB=262144, 512KB=524288
    // the buffer should set to sleep*kbps/8,
    // for example, your system delivery stream in 1000kbps,
    // sleep 800ms for small bytes, the buffer should set to:
    //      800*1000/8=100000B(about 128KB).
    // other examples:
    //      2000*3000/8=750000B(about 732KB).
    //      2000*5000/8=1250000B(about 1220KB).
    int kbps = 4000;
    int iv = u2ms(buffer_v) * kbps / 8;

    // socket send buffer, system will double it.
    iv = iv / 2;

    // override the send buffer by macro.
#ifdef PERF_SO_SNDBUF_SIZE
    iv = PERF_SO_SNDBUF_SIZE / 2;
#endif

    // set the socket send buffer when required larger buffer
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, nb_v) < 0) {
        return ERRORNEW(ERROR_SOCKET_SNDBUF, "setsockopt fd=%d, r0=%d", fd, r0);
    }
    if ((r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, &nb_v)) != 0) {
        return ERRORNEW(ERROR_SOCKET_SNDBUF, "getsockopt fd=%d, r0=%d", fd, r0);
    }

    trace("set fd=%d, SO_SNDBUF=%d=>%d, buffer=%dms", fd, ov, iv, u2ms(buffer_v));

    return err;
}

void TcpConnection::SetRecvTimeout(utime_t tm)
{
    m_skt->SetRecvTimeout(tm);
}

utime_t TcpConnection::GetRecvTimeout()
{
    return m_skt->GetRecvTimeout();
}

error TcpConnection::ReadFully(void *buf, size_t size, ssize_t *nread)
{
    return m_skt->ReadFully(buf, size, nread);
}

int64_t TcpConnection::GetRecvBytes()
{
    return m_skt->GetRecvBytes();
}

int64_t TcpConnection::GetSendBytes()
{
    return m_skt->GetSendBytes();
}

error TcpConnection::Read(void *buf, size_t size, ssize_t *nread)
{
    return m_skt->Read(buf, size, nread);
}

void TcpConnection::SetSendTimeout(utime_t tm)
{
    m_skt->SetSendTimeout(tm);
}

utime_t TcpConnection::GetSendTimeout()
{
    return m_skt->GetSendTimeout();
}

error TcpConnection::Write(void *buf, size_t size, ssize_t *nwrite)
{
    return m_skt->Write(buf, size, nwrite);
}

error TcpConnection::Writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return m_skt->Writev(iov, iov_size, nwrite);
}

BufferedReadWriter::BufferedReadWriter(IProtocolReadWriter *io)
{
    m_io = io;
    m_buf = NULL;
}

BufferedReadWriter::~BufferedReadWriter()
{
    Freep(m_buf);
}

error BufferedReadWriter::Peek(char *buf, int *size)
{
    error err = SUCCESS;

    if ((err = ReloadBuffer()) != SUCCESS) {
        return ERRORWRAP(err, "reload buffer");
    }

    int nn = MIN(m_buf->Remain(), *size);
    *size = nn;

    if (nn) {
        memcpy(buf, m_buf->Head(), nn);
    }

    return err;
}

error BufferedReadWriter::ReloadBuffer()
{
    error err = SUCCESS;

    if (m_buf && !m_buf->Empty()) {
        return err;
    }

    // We use ReadFully to always full fill the cache, to avoid peeking failed.
    ssize_t nread = 0;
    if ((err = m_io->ReadFully(m_cache, sizeof(m_cache), &nread)) != SUCCESS) {
        return ERRORWRAP(err, "read");
    }

    Freep(m_buf);
    m_buf = new Buffer(m_cache, nread);

    return err;
}

error BufferedReadWriter::Read(void *buf, size_t size, ssize_t *nread)
{
    if (!m_buf || m_buf->Empty()) {
        return m_io->Read(buf, size, nread);
    }

    int nn = MIN(m_buf->Remain(), (int)size);
    *nread = nn;

    if (nn) {
        m_buf->ReadBytes((char*)buf, nn);
    }
    return SUCCESS;
}

error BufferedReadWriter::ReadFully(void *buf, size_t size, ssize_t *nread)
{
    if (!m_buf || m_buf->Empty()) {
        return m_io->ReadFully(buf, size, nread);
    }

    int nn = MIN(m_buf->Remain(), (int)size);
    if (nn) {
        m_buf->ReadBytes((char*)buf, nn);
    }

    int left = size - nn;
    *nread = size;

    if (left) {
        return m_io->ReadFully((char*)buf + nn, left, NULL);
    }
    return SUCCESS;
}

void BufferedReadWriter::SetRecvTimeout(utime_t tm)
{
    return m_io->SetRecvTimeout(tm);
}

utime_t BufferedReadWriter::GetRecvTimeout()
{
    return m_io->GetRecvTimeout();
}

int64_t BufferedReadWriter::GetRecvBytes()
{
    return m_io->GetRecvBytes();
}

int64_t BufferedReadWriter::GetSendBytes()
{
    return m_io->GetSendBytes();
}

void BufferedReadWriter::SetSendTimeout(utime_t tm)
{
    return m_io->SetSendTimeout(tm);
}

utime_t BufferedReadWriter::GetSendTimeout()
{
    return m_io->GetSendTimeout();
}

error BufferedReadWriter::Write(void *buf, size_t size, ssize_t *nwrite)
{
    return m_io->Write(buf, size, nwrite);
}

error BufferedReadWriter::Writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return m_io->Writev(iov, iov_size, nwrite);
}

SslConnection::SslConnection(IProtocolReadWriter *c)
{
    m_transport = c;
    m_sslCtx = NULL;
    m_ssl = NULL;
}

SslConnection::~SslConnection()
{
    if (m_ssl) {
        // this function will free bm_ioin and bio_out
        SSL_free(m_ssl);
        m_ssl = NULL;
    }

    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = NULL;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

error SslConnection::Handshake(std::string key_file, std::string crt_file)
{
    error err = SUCCESS;

    // For HTTPS, try to connect over security transport.
#if (OPENSSL_VERSION_NUMBER < 0x10002000L) // v1.0.2
    ssl_ctx = SSL_CTX_new(TLS_method());
#else
    m_sslCtx = SSL_CTX_new(TLSv1_2_method());
#endif
    SSL_CTX_set_verify(m_sslCtx, SSL_VERIFY_NONE, NULL);
    Assert(SSL_CTX_set_cipher_list(m_sslCtx, "ALL") == 1);

    // TODO: Setup callback, see SSL_set_ex_data and SSL_set_info_callback
    if ((m_ssl = SSL_new(m_sslCtx)) == NULL) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "SSL_new ssl");
    }

    if ((m_bioIn = BIO_new(BIO_s_mem())) == NULL) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_new in");
    }

    if ((m_bioOut = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(m_bioIn);
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_new out");
    }

    SSL_set_bio(m_ssl, m_bioIn, m_bioOut);

    // SSL setup active, as server role.
    SSL_set_accept_state(m_ssl);
    SSL_set_mode(m_ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);

    uint8_t* data = NULL;
    int r0, r1, size;

    // Setup the key and cert file for server.
    if ((r0 = SSL_use_certificate_file(m_ssl, crt_file.c_str(), SSL_FILETYPE_PEM)) != 1) {
        return ERRORNEW(ERROR_HTTPS_KEY_CRT, "use cert %s", crt_file.c_str());
    }

    if ((r0 = SSL_use_RSAPrivateKey_file(m_ssl, key_file.c_str(), SSL_FILETYPE_PEM)) != 1) {
        return ERRORNEW(ERROR_HTTPS_KEY_CRT, "use key %s", key_file.c_str());
    }

    if ((r0 = SSL_check_private_key(m_ssl)) != 1) {
        return ERRORNEW(ERROR_HTTPS_KEY_CRT, "check key %s with cert %s",
            key_file.c_str(), crt_file.c_str());
    }
    info("ssl: use key %s and cert %s", key_file.c_str(), crt_file.c_str());

    // Receive ClientHello
    while (true) {
        char buf[1024]; ssize_t nn = 0;
        if ((err = m_transport->Read(buf, sizeof(buf), &nn)) != SUCCESS) {
            return ERRORWRAP(err, "handshake: read");
        }

        if ((r0 = BIO_write(m_bioIn, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(m_ssl); r1 = SSL_get_error(m_ssl, r0);
        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(m_bioOut, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(m_bioIn)) != 1) {
                return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    info("https: ClientHello done");

    // Send ServerHello, Certificate, Server Key Exchange, Server Hello Done
    size = BIO_get_mem_data(m_bioOut, &data);
    if (!data || size <= 0) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = m_transport->Write(data, size, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(m_bioOut)) != 1) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    info("https: ServerHello done");

    // Receive Client Key Exchange, Change Cipher Spec, Encrypted Handshake Message
    while (true) {
        char buf[1024]; ssize_t nn = 0;
        if ((err = m_transport->Read(buf, sizeof(buf), &nn)) != SUCCESS) {
            return ERRORWRAP(err, "handshake: read");
        }

        if ((r0 = BIO_write(m_bioIn, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(m_ssl); r1 = SSL_get_error(m_ssl, r0);
        if (r0 == 1 && r1 == SSL_ERROR_NONE) {
            break;
        }

        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(m_bioOut, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(m_bioIn)) != 1) {
                return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    info("https: Client done");

    // Send New Session Ticket, Change Cipher Spec, Encrypted Handshake Message
    size = BIO_get_mem_data(m_bioOut, &data);
    if (!data || size <= 0) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = m_transport->Write(data, size, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(m_bioOut)) != 1) {
        return ERRORNEW(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    info("https: Server done");

    return err;
}

#pragma GCC diagnostic pop

void SslConnection::SetRecvTimeout(utime_t tm)
{
    m_transport->SetRecvTimeout(tm);
}

utime_t SslConnection::GetRecvTimeout()
{
    return m_transport->GetRecvTimeout();
}

error SslConnection::ReadFully(void *buf, size_t size, ssize_t *nread)
{
    return m_transport->ReadFully(buf, size, nread);
}

int64_t SslConnection::GetRecvBytes()
{
    return m_transport->GetRecvBytes();
}

int64_t SslConnection::GetSendBytes()
{
    return m_transport->GetSendBytes();
}

error SslConnection::Read(void *plaintext, size_t nn_plaintext, ssize_t *nread)
{
    error err = SUCCESS;

    while (true) {
        int r0 = SSL_read(m_ssl, plaintext, nn_plaintext); int r1 = SSL_get_error(m_ssl, r0);
        int r2 = BIO_ctrl_pending(m_bioIn); int r3 = SSL_is_init_finished(m_ssl);

        // OK, got data.
        if (r0 > 0) {
            Assert(r0 <= (int)nn_plaintext);
            if (nread) {
                *nread = r0;
            }
            return err;
        }

        // Need to read more data to feed SSL.
        if (r0 == -1 && r1 == SSL_ERROR_WANT_READ) {
            // TODO: Can we avoid copy?
            int nn_cipher = nn_plaintext;
            char* cipher = new char[nn_cipher];
            AutoFreeA(char, cipher);

            // Read the cipher from SSL.
            ssize_t nn = 0;
            if ((err = m_transport->Read(cipher, nn_cipher, &nn)) != SUCCESS) {
                return ERRORWRAP(err, "https: read");
            }

            int r0 = BIO_write(m_bioIn, cipher, nn);
            if (r0 <= 0) {
                // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
                return ERRORNEW(ERROR_HTTPS_READ, "BIO_write r0=%d, cipher=%p, size=%d", r0, cipher, nn);
            }
            continue;
        }

        // Fail for error.
        if (r0 <= 0) {
            return ERRORNEW(ERROR_HTTPS_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                r0, r1, r2, r3);
        }
    }
}

void SslConnection::SetSendTimeout(utime_t tm)
{
    m_transport->SetSendTimeout(tm);
}

utime_t SslConnection::GetSendTimeout()
{
    return m_transport->GetSendTimeout();
}

error SslConnection::Write(void *plaintext, size_t nn_plaintext, ssize_t *nwrite)
{
    error err = SUCCESS;

    for (char* p = (char*)plaintext; p < (char*)plaintext + nn_plaintext;) {
        int left = (int)nn_plaintext - (p - (char*)plaintext);
        int r0 = SSL_write(m_ssl, (const void*)p, left);
        int r1 = SSL_get_error(m_ssl, r0);
        if (r0 <= 0) {
            return ERRORNEW(ERROR_HTTPS_WRITE, "https: write data=%p, size=%d, r0=%d, r1=%d", p, left, r0, r1);
        }

        // Move p to the next writing position.
        p += r0;
        if (nwrite) {
            *nwrite += (ssize_t)r0;
        }

        uint8_t* data = NULL;
        int size = BIO_get_mem_data(m_bioOut, &data);
        if ((err = m_transport->Write(data, size, NULL)) != SUCCESS) {
            return ERRORWRAP(err, "https: write data=%p, size=%d", data, size);
        }
        if ((r0 = BIO_reset(m_bioOut)) != 1) {
            return ERRORNEW(ERROR_HTTPS_WRITE, "BIO_reset r0=%d", r0);
        }
    }

    return err;
}

error SslConnection::Writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    error err = SUCCESS;

    for (int i = 0; i < iov_size; i++) {
        const iovec* p = iov + i;
        if ((err = Write((void*)p->iov_base, (size_t)p->iov_len, nwrite)) != SUCCESS) {
            return ERRORWRAP(err, "write iov #%d base=%p, size=%d", i, p->iov_base, p->iov_len);
        }
    }

    return err;
}

#include "app_server.h"
#include "app_config.h"
#include "consts.h"
#include "database.h"
#include "protocol_http_stack.h"
#include "utility.h"
#include "app_heartbeat.h"
#include "log.h"
#include "app_utility.h"
#include "protocol_log.h"
#include "app_statistic.h"
#include "app_rtmp_conn.h"
#include "app_http_conn.h"
#include "app_coworkers.h"
#include "app_service_conn.h"

#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>

SignalManager* SignalManager::instance = NULL;

SignalManager::SignalManager(Server *s)
{
    SignalManager::instance = this;

    m_server = s;
    m_sigPipe[0] = m_sigPipe[1] = -1;
    m_trd = new STCoroutine("signal", this, Context->GetId());
    m_signalReadStfd = NULL;
}

SignalManager::~SignalManager()
{
    Freep(m_trd);

    CloseStfd(m_signalReadStfd);

    if (m_sigPipe[0] > 0) {
        ::close(m_sigPipe[0]);
    }
    if (m_sigPipe[1] > 0) {
        ::close(m_sigPipe[1]);
    }
}

error SignalManager::Initialize()
{
    /* Create signal pipe */
    if (pipe(m_sigPipe) < 0) {
        return ERRORNEW(ERROR_SYSTEM_CREATE_PIPE, "create pipe");
    }

    if ((m_signalReadStfd = NetfdOpen(m_sigPipe[0])) == NULL) {
        return ERRORNEW(ERROR_SYSTEM_CREATE_PIPE, "open pipe");
    }

    return SUCCESS;
}

error SignalManager::Start()
{
    error err = SUCCESS;

    /**
     * Note that if multiple processes are used (see below),
     * the signal pipe should be initialized after the fork(2) call
     * so that each process has its own private pipe.
     */
    struct sigaction sa;

    /* Install sig_catcher() as a signal handler */
    sa.sa_handler = SignalManager::SigCatcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGNAL_RELOAD, &sa, NULL);

    sa.sa_handler = SignalManager::SigCatcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGNAL_FAST_QUIT, &sa, NULL);

    sa.sa_handler = SignalManager::SigCatcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGNAL_GRACEFULLY_QUIT, &sa, NULL);

    sa.sa_handler = SignalManager::SigCatcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGNAL_ASSERT_ABORT, &sa, NULL);

    sa.sa_handler = SignalManager::SigCatcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = SignalManager::SigCatcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGNAL_REOPEN_LOG, &sa, NULL);

    trace("signal installed, reload=%d, reopen=%d, fast_quit=%d, grace_quit=%d",
              SIGNAL_RELOAD, SIGNAL_REOPEN_LOG, SIGNAL_FAST_QUIT, SIGNAL_GRACEFULLY_QUIT);

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "signal manager");
    }

    return err;
}

error SignalManager::Cycle()
{
    error err = SUCCESS;

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "signal manager");
        }

        int signo;

        /* Read the next signal from the pipe */
        Read(m_signalReadStfd, &signo, sizeof(int), UTIME_NO_TIMEOUT);

        /* Process signal synchronously */
        m_server->OnSignal(signo);
    }

    return err;
}

void SignalManager::SigCatcher(int signo)
{
    int err;

    /* Save errno to restore it after the write() */
    err = errno;

    /* write() is reentrant/async-safe */
    int fd = SignalManager::instance->m_sigPipe[1];
    write(fd, &signo, sizeof(int));

    errno = err;
}

// Whether we are in docker, defined in main module.
extern bool in_docker;

InotifyWorker::InotifyWorker(Server *s)
{
    m_server = s;
    m_trd = new STCoroutine("inotify", this);
    m_inotifyFd = NULL;
}

InotifyWorker::~InotifyWorker()
{
    Freep(m_trd);
    CloseStfd(m_inotifyFd);
}

bool in_docker = false;

error InotifyWorker::Start()
{
    error err = SUCCESS;

#ifndef SRS_OSX
    // Whether enable auto reload config.
    bool auto_reload = config->InotifyAutoReload();
    if (!auto_reload && in_docker && config->AutoReloadForDocker()) {
        warn("enable auto reload for docker");
        auto_reload = true;
    }

    if (!auto_reload) {
        return err;
    }

    // Create inotify to watch config file.
    int fd = ::inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        return ERRORNEW(ERROR_INOTIFY_CREATE, "create inotify");
    }

    // Open as stfd to read by ST.
    if ((m_inotifyFd = NetfdOpen(fd)) == NULL) {
        ::close(fd);
        return ERRORNEW(ERROR_INOTIFY_OPENFD, "open fd=%d", fd);
    }

    if (((err = FdCloseexec(fd))) != SUCCESS) {
        return ERRORWRAP(err, "closeexec fd=%d", fd);
    }

    // /* the following are legal, implemented events that user-space can watch for */
    // #define IN_ACCESS               0x00000001      /* File was accessed */
    // #define IN_MODIFY               0x00000002      /* File was modified */
    // #define IN_ATTRIB               0x00000004      /* Metadata changed */
    // #define IN_CLOSE_WRITE          0x00000008      /* Writtable file was closed */
    // #define IN_CLOSE_NOWRITE        0x00000010      /* Unwrittable file closed */
    // #define IN_OPEN                 0x00000020      /* File was opened */
    // #define IN_MOVED_FROM           0x00000040      /* File was moved from X */
    // #define IN_MOVED_TO             0x00000080      /* File was moved to Y */
    // #define IN_CREATE               0x00000100      /* Subfile was created */
    // #define IN_DELETE               0x00000200      /* Subfile was deleted */
    // #define IN_DELETE_SELF          0x00000400      /* Self was deleted */
    // #define IN_MOVE_SELF            0x00000800      /* Self was moved */
    //
    // /* the following are legal events.  they are sent as needed to any watch */
    // #define IN_UNMOUNT              0x00002000      /* Backing fs was unmounted */
    // #define IN_Q_OVERFLOW           0x00004000      /* Event queued overflowed */
    // #define IN_IGNORED              0x00008000      /* File was ignored */
    //
    // /* helper events */
    // #define IN_CLOSE                (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE) /* close */
    // #define IN_MOVE                 (IN_MOVED_FROM | IN_MOVED_TO) /* moves */
    //
    // /* special flags */
    // #define IN_ONLYDIR              0x01000000      /* only watch the path if it is a directory */
    // #define IN_DONT_FOLLOW          0x02000000      /* don't follow a sym link */
    // #define IN_EXCL_UNLINK          0x04000000      /* exclude events on unlinked objects */
    // #define IN_MASK_ADD             0x20000000      /* add to the mask of an already existing watch */
    // #define IN_ISDIR                0x40000000      /* event occurred against dir */
    // #define IN_ONESHOT              0x80000000      /* only send event once */

    // Watch the config directory events.
    std::string config_dir = PathDirname(config->ConfigPath());
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_MOVED_TO; int watch_conf = 0;
    if ((watch_conf = ::inotify_add_watch(fd, config_dir.c_str(), mask)) < 0) {
        return ERRORNEW(ERROR_INOTIFY_WATCH, "watch file=%s, fd=%d, watch=%d, mask=%#x",
            config_dir.c_str(), fd, watch_conf, mask);
    }
    trace("auto reload watching fd=%d, watch=%d, file=%s", fd, watch_conf, config_dir.c_str());

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "inotify");
    }
#endif

    return err;
}

error InotifyWorker::Cycle()
{
    error err = SUCCESS;

#ifndef SRS_OSX
    std::string config_path = config->ConfigPath();
    std::string config_file = PathBasename(config_path);
    std::string k8s_file = "..data";

    while (true) {
        char buf[4096];
        ssize_t nn = Read(m_inotifyFd, buf, (size_t)sizeof(buf), UTIME_NO_TIMEOUT);
        if (nn < 0) {
            warn("inotify ignore read failed, nn=%d", (int)nn);
            break;
        }

        // Whether config file changed.
        bool do_reload = false;

        // Parse all inotify events.
        inotify_event* ie = NULL;
        for (char* ptr = buf; ptr < buf + nn; ptr += sizeof(inotify_event) + ie->len) {
            ie = (inotify_event*)ptr;

            if (!ie->len || !ie->name) {
                continue;
            }

            std::string name = ie->name;
            if ((name == k8s_file || name == config_file) && ie->mask & (IN_MODIFY|IN_CREATE|IN_MOVED_TO)) {
                do_reload = true;
            }

            trace("inotify event wd=%d, mask=%#x, len=%d, name=%s, reload=%d", ie->wd, ie->mask, ie->len, ie->name, do_reload);
        }

        // Notify server to do reload.
        if (do_reload && PathExists(config_path)) {
            m_server->OnSignal(SIGNAL_RELOAD);
        }

        Usleep(3000 * UTIME_MILLISECONDS);
    }
#endif

    return err;
}

Server::Server()
{
    m_signalReload = false;
    m_signalPersistenceConfig = false;
    m_signalGmcStop = false;
    m_signalFastQuit = false;
    m_signalGracefullyQuit = false;
    m_pidFd = -1;

    m_signalManager = new SignalManager(this);
    m_connManager = new ResourceManager("TCP", true);
    m_latestVersion = nullptr; //new LatestVersion();
    m_ppid = ::getppid();

    m_rtmpListener = new MultipleTcpListeners(this);
    m_apiListener = new TcpListener(this);
    m_apisListener = new TcpListener(this);
    m_httpListener = new TcpListener(this);
    m_httpsListener = new TcpListener(this);
    m_webrtcListener = new TcpListener(this);
    m_serviceListener = new TcpListener(this);
    m_streamCasterFlvListener = nullptr; //new HttpFlvListener();
    m_streamCasterMpegts = nullptr; //new UdpCasterListener();
    m_exporterListener = new TcpListener(this);

    // donot new object in constructor,
    // for some global instance is not ready now,
    // new these objects in initialize instead.
    m_httpApiMux = nullptr; //new IHttpServeMux();
    m_httpServer = nullptr; //new HttpServer(this);
    m_reuseApiOverServer = false;
    m_reuseRtcOverServer = false;

    m_httpHeartbeat = new HttpHeartbeat();
    m_ingester = nullptr; //new Ingester();
    m_trd = new STCoroutine("srs", this, Context->GetId());
    m_timer = NULL;
    m_wg = NULL;
    m_database = Database::Instance();
}

Server::~Server()
{
    Destroy();
}

void Server::Destroy()
{
    warn("start destroy server");

    Freep(m_trd);
    Freep(m_timer);

    Dispose();

    // If api reuse the same port of server, they're the same object.
    if (!m_reuseApiOverServer) {
        Freep(m_httpApiMux);
    }
    Freep(m_httpServer);

    Freep(m_httpHeartbeat);
    Freep(m_ingester);

    if (m_pidFd > 0) {
        ::close(m_pidFd);
        m_pidFd = -1;
    }

    Freep(m_signalManager);
    Freep(m_latestVersion);
    Freep(m_connManager);
    Freep(m_rtmpListener);
    Freep(m_apiListener);
    Freep(m_apisListener);
    Freep(m_httpListener);
    Freep(m_httpsListener);
    Freep(m_webrtcListener);
    Freep(m_serviceListener);
    Freep(m_streamCasterFlvListener);
    Freep(m_streamCasterMpegts);
    Freep(m_exporterListener);
#ifdef SRS_GB28181
    Freep(stream_caster_gb28181_);
#endif
}

void Server::Dispose()
{
    config->Unsubscribe(this);

    // Destroy all listeners.
    m_rtmpListener->Close();
    m_serviceListener->Close();
    m_apiListener->Close();
    m_apisListener->Close();
    m_httpListener->Close();
    m_httpsListener->Close();
    m_webrtcListener->Close();
//    m_streamCasterFlvListener->Close();
    //m_streamCasterMpegts->Close();
    m_exporterListener->Close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
//    m_ingester->Dispose();

    // dispose the source for hls and dvr.
    sources->Dispose();

    // @remark don't dispose all connections, for too slow.
}

void Server::GracefullyDispose()
{
    config->Unsubscribe(this);

    // Always wait for a while to start.
    Usleep(config->GetGraceStartWait());
    trace("start wait for %dms", u2msi(config->GetGraceStartWait()));

    // Destroy all listeners.
    m_rtmpListener->Close();
    m_apiListener->Close();
    m_apisListener->Close();
    m_httpListener->Close();
    m_httpsListener->Close();
    m_webrtcListener->Close();
    m_serviceListener->Close();
//    m_streamCasterFlvListener->Close();
//    m_streamCasterMpegts->Close();
    m_exporterListener->Close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif
    trace("listeners closed");

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
//    m_ingester->Stop();
    trace("ingesters stopped");

    // Wait for connections to quit.
    // While gracefully quiting, user can requires SRS to fast quit.
    int wait_step = 1;
    while (!m_connManager->Empty() && !m_signalFastQuit) {
        for (int i = 0; i < wait_step && !m_connManager->Empty() && !m_signalFastQuit; i++) {
            st_usleep(1000 * UTIME_MILLISECONDS);
        }

        wait_step = (wait_step * 2) % 33;
        trace("wait for %d conns to quit", (int)m_connManager->Size());
    }

    // dispose the source for hls and dvr.
    sources->Dispose();
    trace("source disposed");

    Usleep(config->GetGraceFinalWait());
    trace("final wait for %dms", u2msi(config->GetGraceFinalWait()));
}

error Server::Initialize()
{
    error err = SUCCESS;

    // for the main objects(server, config, log, context),
    // never subscribe handler in constructor,
    // instead, subscribe handler in initialize method.
    Assert(config);
    config->Subscribe(this);

    bool stream = config->GetHttpStreamEnabled();
    std::string http_listen = config->GetHttpStreamListen();
    std::string https_listen = config->GetHttpsStreamListen();

#ifdef RTC
    bool rtc = config->get_rtc_server_enabled();
    bool rtc_tcp = config->get_rtc_server_tcp_enabled();
    string rtc_listen = srs_int2str(config->get_rtc_server_tcp_listen());
    // If enabled and listen is the same value, resue port for WebRTC over TCP.
    if (stream && rtc && rtc_tcp && http_listen == rtc_listen) {
        srs_trace("WebRTC tcp=%s reuses http=%s server", rtc_listen.c_str(), http_listen.c_str());
        reuse_rtc_over_server_ = true;
    }
    if (stream && rtc && rtc_tcp && https_listen == rtc_listen) {
        srs_trace("WebRTC tcp=%s reuses https=%s server", rtc_listen.c_str(), https_listen.c_str());
        reuse_rtc_over_server_ = true;
    }
#endif

    // If enabled and the listen is the same value, reuse port.
    bool api = config->GetHttpApiEnabled();
    std::string api_listen = config->GetHttpApiListen();
    std::string apis_listen = config->GetHttpsApiListen();
    if (stream && api && api_listen == http_listen && apis_listen == https_listen) {
        trace("API reuses http=%s and https=%s server", http_listen.c_str(), https_listen.c_str());
        m_reuseApiOverServer = true;
    }

    // Only init HTTP API when not reusing HTTP server.
//    if (!m_reuseApiOverServer) {
//        HttpServeMux *api = dynamic_cast<HttpServeMux*>(m_httpApiMux);
//        Assert(api);

//        if ((err = api->Initialize()) != SUCCESS) {
//            return ERRORWRAP(err, "http api initialize");
//        }
//    } else {
//        Freep(m_httpApiMux);
////        m_httpApiMux = m_httpServer;
//    }

//    if ((err = m_httpServer->initialize()) != SUCCESS) {
//        return ERRORWRAP(err, "http server initialize");
//    }

    return err;
}

error Server::InitializeSt()
{
    error err = SUCCESS;

    // check asprocess.
    bool asprocess = config->GetAsprocess();
    if (asprocess && m_ppid == 1) {
        return ERRORNEW(ERROR_SYSTEM_ASSERT_FAILED, "ppid=%d illegal for asprocess", m_ppid);
    }

    trace("server main cid=%s, pid=%d, ppid=%d, asprocess=%d",
        Context->GetId().Cstr(), ::getpid(), m_ppid, asprocess);

    return err;
}

error Server::InitializeSignal()
{
    error err = SUCCESS;

    if ((err = m_signalManager->Initialize()) != SUCCESS) {
        return ERRORWRAP(err, "init signal manager");
    }

    // Start the version query coroutine.
//    if ((err = m_latestVersion->Start()) != SUCCESS) {
//        return ERRORWRAP(err, "start version query");
//    }

    return err;
}

error Server::Listen()
{
    error err = SUCCESS;

    // Create RTMP listeners.
    m_rtmpListener->Add(config->GetListens())->SetLabel("RTMP");
    if ((err = m_rtmpListener->Listen()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp listen");
    }

    if(config->GetServiceEnabled())
    {
        m_serviceListener->SetEndpoint(config->GetServiceListen())->SetLabel("SERVICE");
        if((err = m_serviceListener->Listen()) != SUCCESS){
            return ERRORWRAP(err, "service listen");
        }
    }


    // Create HTTP API listener.
    if (config->GetHttpApiEnabled()) {
        if (m_reuseApiOverServer) {
            trace("HTTP-API: Reuse listen to http server %s", config->GetHttpStreamListen().c_str());
        } else {
            m_apiListener->SetEndpoint(config->GetHttpApiListen())->SetLabel("HTTP-API");
            if ((err = m_apiListener->Listen()) != SUCCESS) {
                return ERRORWRAP(err, "http api listen");
            }
        }
    }

    // Create HTTPS API listener.
    if (config->GetHttpsApiEnabled()) {
        if (m_reuseApiOverServer) {
            trace("HTTPS-API: Reuse listen to http server %s", config->GetHttpStreamListen().c_str());
        } else {
            m_apisListener->SetEndpoint(config->GetHttpsApiListen())->SetLabel("HTTPS-API");
            if ((err = m_apisListener->Listen()) != SUCCESS) {
                return ERRORWRAP(err, "https api listen");
            }
        }
    }

    // Create HTTP server listener.
    if (config->GetHttpStreamEnabled()) {
        m_httpListener->SetEndpoint(config->GetHttpStreamListen())->SetLabel("HTTP-Server");
        if ((err = m_httpListener->Listen()) != SUCCESS) {
            return ERRORWRAP(err, "http server listen");
        }
    }

    // Create HTTPS server listener.
    if (config->GetHttpsStreamEnabled()) {
        m_httpsListener->SetEndpoint(config->GetHttpsStreamListen())->SetLabel("HTTPS-Server");
        if ((err = m_httpsListener->Listen()) != SUCCESS) {
            return ERRORWRAP(err, "https server listen");
        }
    }

    // Start WebRTC over TCP listener.
#ifdef SRS_RTC
    if (!reuse_rtc_over_server_ && config->get_rtc_server_tcp_enabled()) {
        webrtc_listener_->set_endpoint(srs_int2str(config->get_rtc_server_tcp_listen()))->set_label("WebRTC");
        if ((err = webrtc_listener_->listen()) != SUCCESS) {
            return ERRORWRAP(err, "webrtc tcp listen");
        }
    }
#endif

    // Start all listeners for stream caster.
    std::vector<ConfDirective*> confs = config->GetStreamCasters();
    for (std::vector<ConfDirective*>::iterator it = confs.begin(); it != confs.end(); ++it) {
        ConfDirective* conf = *it;
        if (!config->GetStreamCasterEnabled(conf)) {
            continue;
        }

        IListener* listener = NULL;
        std::string caster = config->GetStreamCasterEngine(conf);
        if (StreamCasterIsUdp(caster)) {
//            listener = m_streamCasterMpegts;
//            if ((err = m_streamCasterMpegts->Initialize(conf)) != SUCCESS) {
//                return ERRORWRAP(err, "initialize");
//            }
        } else if (StreamCasterIsFlv(caster)) {
//            listener = m_streamCasterFlvListener;
//            if ((err = m_streamCasterFlvListener->initialize(conf)) != SUCCESS) {
//                return ERRORWRAP(err, "initialize");
//            }
        } else if (StreamCasterIsGb28181(caster)) {
        #ifdef SRS_GB28181
            listener = stream_caster_gb28181_;
            if ((err = stream_caster_gb28181_->initialize(conf)) != SUCCESS) {
                return ERRORWRAP(err, "initialize");
            }
        #else
            return ERRORNEW(ERROR_STREAM_CASTER_ENGINE, "Please enable GB by: ./configure --gb28181=on");
        #endif
        } else {
            return ERRORNEW(ERROR_STREAM_CASTER_ENGINE, "invalid caster %s", caster.c_str());
        }

        Assert(listener);
        if ((err = listener->Listen()) != SUCCESS) {
            return ERRORWRAP(err, "listen");
        }
    }

    // Create exporter server listener.
    if (config->GetExporterEnabled()) {
        m_exporterListener->SetEndpoint(config->GetExporterListen())->SetLabel("Exporter-Server");
        if ((err = m_exporterListener->Listen()) != SUCCESS) {
            return ERRORWRAP(err, "exporter server listen");
        }
    }

    if ((err = m_connManager->Start()) != SUCCESS) {
        return ERRORWRAP(err, "connection manager");
    }

    return err;
}

error Server::RegisterSignal()
{
    error err = SUCCESS;

    if ((err = m_signalManager->Start()) != SUCCESS) {
        return ERRORWRAP(err, "signal manager start");
    }

    return err;
}

error Server::HttpHandle()
{
    error err = SUCCESS;

//    // Ignore / and /api/v1/versions for already handled by HTTP server.
//    if (!m_reuseApiOverServer) {
//        if ((err = m_httpApiMux->Handle("/", new GoApiRoot())) != SUCCESS) {
//            return ERRORWRAP(err, "handle /");
//        }
//        if ((err = m_httpApiMux->Handle("/api/v1/versions", new GoApiVersion())) != SUCCESS) {
//            return ERRORWRAP(err, "handle versions");
//        }
//    }

//    if ((err = m_httpApiMux->Handle("/api/", new GoApiApi())) != SUCCESS) {
//        return ERRORWRAP(err, "handle api");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/", new GoApiV1())) != SUCCESS) {
//        return ERRORWRAP(err, "handle v1");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/summaries", new GoApiSummaries())) != SUCCESS) {
//        return ERRORWRAP(err, "handle summaries");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/rusages", new GoApiRusages())) != SUCCESS) {
//        return ERRORWRAP(err, "handle rusages");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/self_proc_stats", new GoApiSelfProcStats())) != SUCCESS) {
//        return ERRORWRAP(err, "handle self proc stats");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/system_proc_stats", new GoApiSystemProcStats())) != SUCCESS) {
//        return ERRORWRAP(err, "handle system proc stats");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/meminfos", new GoApiMemInfos())) != SUCCESS) {
//        return ERRORWRAP(err, "handle meminfos");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/authors", new GoApiAuthors())) != SUCCESS) {
//        return ERRORWRAP(err, "handle authors");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/features", new GoApiFeatures())) != SUCCESS) {
//        return ERRORWRAP(err, "handle features");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/vhosts/", new GoApiVhosts())) != SUCCESS) {
//        return ERRORWRAP(err, "handle vhosts");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/streams/", new GoApiStreams())) != SUCCESS) {
//        return ERRORWRAP(err, "handle streams");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/clients/", new GoApiClients())) != SUCCESS) {
//        return ERRORWRAP(err, "handle clients");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/raw", new GoApiRaw(this))) != SUCCESS) {
//        return ERRORWRAP(err, "handle raw");
//    }
//    if ((err = m_httpApiMux->Handle("/api/v1/clusters", new GoApiClusters())) != SUCCESS) {
//        return ERRORWRAP(err, "handle clusters");
//    }

//    // test the request info.
//    if ((err = m_httpApiMux->Handle("/api/v1/tests/requests", new GoApiRequests())) != SUCCESS) {
//        return ERRORWRAP(err, "handle tests requests");
//    }
//    // test the error code response.
//    if ((err = m_httpApiMux->Handle("/api/v1/tests/errors", new GoApiError())) != SUCCESS) {
//        return ERRORWRAP(err, "handle tests errors");
//    }
//    // test the redirect mechenism.
//    if ((err = m_httpApiMux->Handle("/api/v1/tests/redirects", new HttpRedirectHandler("/api/v1/tests/errors", CONSTS_HTTP_MovedPermanently))) != SUCCESS) {
//        return ERRORWRAP(err, "handle tests redirects");
//    }
//    // test the http vhost.
//    if ((err = m_httpApiMux->Handle("error.srs.com/api/v1/tests/errors", new GoApiError())) != SUCCESS) {
//        return ERRORWRAP(err, "handle tests errors for error.srs.com");
//    }

//#ifdef SRS_GPERF
//    // The test api for get tcmalloc stats.
//    // @see Memory Introspection in https://gperftools.github.io/gperftools/tcmalloc.html
//    if ((err = m_httpApiMux->Handle("/api/v1/tcmalloc", new GoApiTcmalloc())) != SUCCESS) {
//        return ERRORWRAP(err, "handle tests errors");
//    }
//#endif
//    // metrics by prometheus
//    if ((err = m_httpApiMux->Handle("/metrics", new GoApiMetrics())) != SUCCESS) {
//        return ERRORWRAP(err, "handle tests errors");
//    }

//    // TODO: FIXME: for console.
//    // TODO: FIXME: support reload.
//    std::string dir = config->GetHttpStreamDir() + "/console";
//    if ((err = m_httpApiMux->Handle("/console/", new HttpFileServer(dir))) != SUCCESS) {
//        return ERRORWRAP(err, "handle console at %s", dir.c_str());
//    }
//    trace("http: api mount /console to %s", dir.c_str());

    return err;
}

error Server::Ingest()
{
    error err = SUCCESS;

//    if ((err = m_ingester->Start()) != SUCCESS) {
//        return ERRORWRAP(err, "ingest start");
//    }

    return err;
}

error Server::Start(WaitGroup *wg)
{
    error err = SUCCESS;

    if ((err = sources->Initialize()) != SUCCESS) {
        return ERRORWRAP(err, "sources");
    }

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start");
    }

    if ((err = SetupTicks()) != SUCCESS) {
        return ERRORWRAP(err, "tick");
    }

    // OK, we start SRS server.
    m_wg = wg;
    wg->Add(1);

    return err;
}

void Server::Stop()
{
#ifdef SRS_GPERF_MC
    dispose();

    // remark, for gmc, never invoke the exit().
    warn("sleep a long time for system st-threads to cleanup.");
    Usleep(3 * 1000 * 1000);
    warn("system quit");

    // For GCM, cleanup done.
    return;
#endif

    // quit normally.
    warn("main cycle terminated, system quit normally.");

    // fast quit, do some essential cleanup.
    if (m_signalFastQuit) {
        Dispose(); // TODO: FIXME: Rename to essential_dispose.
        trace("srs disposed");
    }

    // gracefully quit, do carefully cleanup.
    if (m_signalGracefullyQuit) {
        GracefullyDispose();
        trace("srs gracefully quit");
    }

    trace("srs terminated");

    // for valgrind to detect.
    Freep(config);
    Freep(Log);
}

error Server::Cycle()
{
    error err = SUCCESS;

    // Start the inotify auto reload by watching config file.
    InotifyWorker inotify(this);
    if ((err = inotify.Start()) != SUCCESS) {
        return ERRORWRAP(err, "start inotify");
    }

    // Do server main cycle.
     err = DoCycle();

    // OK, SRS server is done.
    m_wg->Done();

    return err;
}

void Server::OnSignal(int signo)
{
    // For signal to quit with coredump.
    if (signo == SIGNAL_ASSERT_ABORT) {
        trace("abort with coredump, signo=%d", signo);
        Assert(false);
        return;
    }

    if (signo == SIGNAL_RELOAD) {
        trace("reload config, signo=%d", signo);
        m_signalReload = true;
        return;
    }

#ifndef SRS_GPERF_MC
    if (signo == SIGNAL_REOPEN_LOG) {
        Log->Reopen();

        warn("reopen log file, signo=%d", signo);
        return;
    }
#endif

#ifdef SRS_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        signal_gmc_stop = true;
        warn("for gmc, the SIGUSR1 used as SIGINT, signo=%d", signo);
        return;
    }
#endif

    if (signo == SIGNAL_PERSISTENCE_CONFIG) {
        m_signalPersistenceConfig = true;
        return;
    }

    if (signo == SIGINT) {
#ifdef SRS_GPERF_MC
        srs_trace("gmc is on, main cycle will terminate normally, signo=%d", signo);
        signal_gmc_stop = true;
#endif
    }

    // For K8S, force to gracefully quit for gray release or canary.
    // @see https://github.com/ossrs/srs/issues/1595#issuecomment-587473037
    if (signo == SIGNAL_FAST_QUIT && config->IsForceGraceQuit()) {
        trace("force gracefully quit, signo=%d", signo);
        signo = SIGNAL_GRACEFULLY_QUIT;
    }

    if ((signo == SIGINT || signo == m_signalFastQuit) && !m_signalFastQuit) {
        trace("sig=%d, user terminate program, fast quit", signo);
        m_signalFastQuit = true;
        return;
    }

    if (signo == SIGNAL_GRACEFULLY_QUIT && !m_signalGracefullyQuit) {
        trace("sig=%d, user start gracefully quit", signo);
        m_signalGracefullyQuit = true;
        return;
    }
}

error Server::DoCycle()
{
    error err = SUCCESS;

    // for asprocess.
    bool asprocess = config->GetAsprocess();

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "pull");
        }

        // asprocess check.
        if (asprocess && ::getppid() != m_ppid) {
            return ERRORNEW(ERROR_ASPROCESS_PPID, "asprocess ppid changed from %d to %d", m_ppid, ::getppid());
        }

        // gracefully quit for SIGINT or SIGTERM or SIGQUIT.
        if (m_signalFastQuit || m_signalGracefullyQuit) {
            trace("cleanup for quit signal fast=%d, grace=%d", m_signalFastQuit, m_signalGracefullyQuit);
            return err;
        }

        // for gperf heap checker,
        // @see: research/gperftools/heap-checker/heap_checker.cc
        // if user interrupt the program, exit to check mem leak.
        // but, if gperf, use reload to ensure main return normally,
        // because directly exit will cause core-dump.
#ifdef SRS_GPERF_MC
        if (signal_gmc_stop) {
            warn("gmc got singal to stop server.");
            return err;
        }
#endif

        // do persistence config to file.
        if (m_signalPersistenceConfig) {
            m_signalPersistenceConfig = false;
            info("get signal to persistence config to file.");

            if ((err = config->Persistence()) != SUCCESS) {
                return ERRORWRAP(err, "config persistence to file");
            }
            trace("persistence config to file success.");
        }

        // do reload the config.
        if (m_signalReload) {
            m_signalReload = false;
            info("get signal to reload the config.");

            if ((err = config->Reload()) != SUCCESS) {
                return ERRORWRAP(err, "config reload");
            }
            trace("reload config success.");
        }

        Usleep(1 * UTIME_SECONDS);
    }

    return err;
}

error Server::SetupTicks()
{
    error err = SUCCESS;

    Freep(m_timer);
    m_timer = new HourGlass("srs", this, 1 * UTIME_SECONDS);

    if (config->GetStatsEnabled()) {
        if ((err = m_timer->Tick(2, 3 * UTIME_SECONDS)) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }
        if ((err = m_timer->Tick(4, 6 * UTIME_SECONDS)) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }
        if ((err = m_timer->Tick(5, 6 * UTIME_SECONDS)) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }
        if ((err = m_timer->Tick(6, 9 * UTIME_SECONDS)) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }
        if ((err = m_timer->Tick(7, 9 * UTIME_SECONDS)) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }

        if ((err = m_timer->Tick(8, 3 * UTIME_SECONDS)) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }

        if ((err = m_timer->Tick(10, 9 * UTIME_SECONDS)) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }
    }

    if (config->GetHeartbeatEnabled()) {
        if ((err = m_timer->Tick(9, config->GetHeartbeatInterval())) != SUCCESS) {
            return ERRORWRAP(err, "tick");
        }
    }

    if ((err = m_timer->Start()) != SUCCESS) {
        return ERRORWRAP(err, "timer");
    }

    return err;
}

error Server::Notify(int event, utime_t interval, utime_t tick)
{
    error err = SUCCESS;

    switch (event) {
        case 2: UpdateSystemRusage(); break;
        case 4: UpdateDiskStat(); break;
        case 5: UpdateMeminfo(); break;
        case 6: UpdatePlatformInfo(); break;
        case 7: UpdateNetworkDevices(); break;
        case 8: ResampleKbps(); break;
        case 9: m_httpHeartbeat->Heartbeat(); break;
        case 10:UpdateUdpSnmpStatistic(); break;
    }

    return err;
}

void Server::ResampleKbps()
{
   Statistic* stat = Statistic::Instance();

    // collect delta from all clients.
    for (int i = 0; i < (int)m_connManager->Size(); i++) {
        IResource* c = m_connManager->At(i);

        RtmpConn* rtmp = dynamic_cast<RtmpConn*>(c);
        if (rtmp) {
            stat->KbpsAddDelta(c->GetId().Cstr(), rtmp->Delta());
            continue;
        }

        HttpxConn* httpx = dynamic_cast<HttpxConn*>(c);
        if (httpx) {
            stat->KbpsAddDelta(c->GetId().Cstr(), httpx->Delta());
            continue;
        }

        ServiceConn* service = dynamic_cast<ServiceConn*>(c);
        if(service){
            stat->KbpsAddDelta(c->GetId().Cstr(), service->Delta());
            continue;
        }

#ifdef SRS_RTC
        SrsRtcTcpConn* tcp = dynamic_cast<SrsRtcTcpConn*>(c);
        if (tcp) {
            stat->kbps_add_delta(c->GetId().c_str(), tcp->delta());
            continue;
        }
#endif

        // Impossible path, because we only create these connections above.
        Assert(false);
    }

    // Update the global server level statistics.
    stat->KbpsSample();
}

IHttpServeMux *Server::ApiServer()
{
    return m_httpApiMux;
}

error Server::OnTcpClient(IListener *listener, netfd_t stfd)
{
    error err = DoOnTcpClient(listener, stfd);

    // We always try to close the stfd, because it should be NULL if it has been handled or closed.
    CloseStfd(stfd);

    return err;
}

error Server::DoOnTcpClient(IListener *listener, netfd_t &stfd)
{
    error err = SUCCESS;

    int fd = NetfdFileno(stfd);
    std::string ip = GetPeerIp(fd);
    int port = GetPeerPort(fd);

    // Ignore if ip is empty, for example, load balancer keepalive.
    if (ip.empty()) {
        if (config->EmptyIpOk()) return err;
        return ERRORNEW(ERROR_SOCKET_GET_PEER_IP, "ignore empty ip, fd=%d", fd);
    }

    // Security or system flow control check.
    if ((err = OnBeforeConnection(stfd, ip, port)) != SUCCESS) {
        return ERRORWRAP(err, "check");
    }

    // Covert handler to resource.
    IResource* resource = NULL;

    // The context id may change during creating the bellow objects.
    ContextRestore(Context->GetId());

    // From now on, we always handle the stfd, so we set the original one to NULL.
    netfd_t stfd2 = stfd;
    stfd = NULL;

#ifdef SRS_RTC
    // If reuse HTTP server with WebRTC TCP, peek to detect the client.
    if (reuse_rtc_over_server_ && (listener == http_listener_ || listener == https_listener_)) {
        SrsTcpConnection* skt = new SrsTcpConnection(stfd2);
        SrsBufferedReadWriter* io = new SrsBufferedReadWriter(skt);

        // Peek first N bytes to finger out the real client type.
        uint8_t b[10]; int nn = sizeof(b);
        if ((err = io->peek((char*)b, &nn)) != SUCCESS) {
            Freep(io); Freep(skt);
            return ERRORWRAP(err, "peek");
        }

        // If first message is BindingRequest(00 01), prefixed with length(2B), it's WebRTC client. Generally, the frame
        // length minus message length should be 20, that is the header size of STUN is 20 bytes. For example:
        //      00 6c # Frame length: 0x006c = 108
        //      00 01 # Message Type: Binding Request(0x0001)
        //      00 58 # Message Length: 0x005 = 88
        //      21 12 a4 42 # Message Cookie: 0x2112a442
        //      48 32 6c 61 6b 42 35 71 42 35 4a 71 # Message Transaction ID: 12 bytes
        if (nn == 10 && b[0] == 0 && b[2] == 0 && b[3] == 1 && b[1] - b[5] == 20
            && b[6] == 0x21 && b[7] == 0x12 && b[8] == 0xa4 && b[9] == 0x42
        ) {
            // TODO: FIXME: Should manage this connection by _srs_rtc_manager
            resource = new SrsRtcTcpConn(io, ip, port, this);
        } else {
            resource = new SrsHttpxConn(listener == http_listener_, this, io, http_server, ip, port);
        }
    }
#endif

    // Create resource by normal listeners.
    if (!resource) {
        if (listener == m_rtmpListener) {
            resource = new RtmpConn(this, stfd2, ip, port);
        } else if (listener == m_apiListener || listener == m_apisListener) {
            bool is_https = listener == m_apisListener;
            resource = new HttpxConn(is_https, this, new TcpConnection(stfd2), m_httpApiMux, ip, port);
        } else if (listener == m_httpListener || listener == m_httpsListener) {
            bool is_https = listener == m_httpsListener;
            resource = new HttpxConn(is_https, this, new TcpConnection(stfd2), m_httpServer, ip, port);
#ifdef SRS_RTC
        } else if (listener == webrtc_listener_) {
            // TODO: FIXME: Should manage this connection by _srs_rtc_manager
            resource = new SrsRtcTcpConn(new SrsTcpConnection(stfd2), ip, port, this);
#endif
        } else if (listener == m_exporterListener) {
            // TODO: FIXME: Maybe should support https metrics.
            bool is_https = false;
            resource = new HttpxConn(is_https, this, new TcpConnection(stfd2), m_httpApiMux, ip, port);
        } else if(listener == m_serviceListener){
            resource = new ServiceConn(this, stfd2, ip, port);
        } else {
            CloseStfd(stfd2);
            warn("Close for invalid fd=%d, ip=%s:%d", fd, ip.c_str(), port);
            return err;
        }
    }

    // Use connection manager to manage all the resources.
    m_connManager->Add(resource);

    // If connection is a resource to start, start a coroutine to handle it.
    IStartable* conn = dynamic_cast<IStartable*>(resource);
    if ((err = conn->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start conn coroutine");
    }

    return err;
}

error Server::OnBeforeConnection(netfd_t &stfd, const std::string &ip, int port)
{
    error err = SUCCESS;

    int fd = NetfdFileno(stfd);

    // Failed if exceed the connection limitation.
    int max_connections = config->GetMaxConnections();

    if ((int)m_connManager->Size() >= max_connections) {
        return ERRORNEW(ERROR_EXCEED_CONNECTIONS, "drop fd=%d, ip=%s:%d, max=%d, cur=%d for exceed connection limits",
            fd, ip.c_str(), port, max_connections, (int)m_connManager->Size());
    }

    // Set to close the fd when forking, to avoid fd leak when start a process.
    // See https://github.com/ossrs/srs/issues/518
    if (true) {
        int val;
        if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
            return ERRORNEW(ERROR_SYSTEM_PID_GET_FILE_INFO, "fnctl F_GETFD error! fd=%d", fd);
        }
        val |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, val) < 0) {
            return ERRORNEW(ERROR_SYSTEM_PID_SET_FILE_INFO, "fcntl F_SETFD error! fd=%d", fd);
        }
    }

    return err;
}

void Server::Remove(IResource *c)
{
    // use manager to free it async.
    m_connManager->Remove(c);
}

error Server::OnReloadListen()
{
    error err = SUCCESS;

    if ((err = Listen()) != SUCCESS) {
        return ERRORWRAP(err, "reload listen");
    }

    return err;
}

error Server::OnPublish(LiveSource *s, Request *r)
{
    error err = SUCCESS;

//    if ((err = m_httpServer->HttpMount(s, r)) != SUCCESS) {
//        return ERRORWRAP(err, "http mount");
//    }

    CoWorkers* coworkers = CoWorkers::Instance();
    if ((err = coworkers->OnPublish(s, r)) != SUCCESS) {
        return ERRORWRAP(err, "coworkers");
    }

    return err;
}

void Server::OnUnpublish(LiveSource *s, Request *r)
{
//    m_httpServer->HttpUnmount(s, r);

    CoWorkers* coworkers = CoWorkers::Instance();
    coworkers->OnUnpublish(s, r);
}

ServerAdapter::ServerAdapter()
{
    m_srs = new Server();
}

ServerAdapter::~ServerAdapter()
{
    Freep(m_srs);
}

error ServerAdapter::Initialize()
{
    error err = SUCCESS;
    return err;
}

error ServerAdapter::Run(WaitGroup *wg)
{
    error err = SUCCESS;

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = m_srs->Initialize()) != SUCCESS) {
        return ERRORWRAP(err, "server initialize");
    }

    if ((err = m_srs->InitializeSt()) != SUCCESS) {
        return ERRORWRAP(err, "initialize st");
    }

    if ((err = m_srs->InitializeSignal()) != SUCCESS) {
        return ERRORWRAP(err, "initialize signal");
    }

    if ((err = m_srs->Listen()) != SUCCESS) {
        return ERRORWRAP(err, "listen");
    }

    if ((err = m_srs->RegisterSignal()) != SUCCESS) {
        return ERRORWRAP(err, "register signal");
    }

    if ((err = m_srs->HttpHandle()) != SUCCESS) {
        return ERRORWRAP(err, "http handle");
    }

    if ((err = m_srs->Ingest()) != SUCCESS) {
        return ERRORWRAP(err, "ingest");
    }

    if ((err = m_srs->Start(wg)) != SUCCESS) {
        return ERRORWRAP(err, "start");
    }

    LazySweepGc* gc = dynamic_cast<LazySweepGc*>(_gc);
    if ((err = gc->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start gc");
    }

    return err;
}

void ServerAdapter::Stop()
{

}

Server *ServerAdapter::Instance()
{
    return m_srs;
}

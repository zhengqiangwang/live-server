#ifndef APP_SERVER_H
#define APP_SERVER_H

#include "app_conn.h"
#include "app_hourglass.h"
#include "app_source.h"
#include "app_st.h"
#include "protocol_st.h"
#include "app_hybrid.h"
#include "app_reload.h"
#include "app_listener.h"

class Server;
class IHttpServeMux;
class HttpServer;
class Ingester;
class HttpHeartbeat;
class Kbps;
class ConfDirective;
class ITcpHandler;
class IUdpHandler;
class UdpListener;
class TcpListener;
class AppCasterFlv;
class ResourceManager;
class LatestVersion;
class WaitGroup;
class MultipleTcpListeners;
class HttpFlvListener;
class UdpCasterListener;
class GbListener;
class Database;


//convert signal to io,
//@see: st-1.9/docs/notes.html
class SignalManager : public ICoroutineHandler
{
private:
    //per-process pipe which is used as a signal queue
    //up to PIPE_BUF/sizeof(int) signals can be queued up
    int m_sigPipe[2];
    netfd_t m_signalReadStfd;
private:
    Server* m_server;
    Coroutine* m_trd;
public:
    SignalManager(Server* s);
    virtual ~SignalManager();
public:
    virtual error Initialize();
    virtual error Start();
//interface IEndlessThreadHandler
public:
    virtual error Cycle();
private:
    //Global singleton instance
    static SignalManager* instance;
    //signal catching function
    //converts signal event to I/O event
    static void SigCatcher(int signo);
};

//auto reload by inotify
class InotifyWorker : public ICoroutineHandler
{
private:
    Server* m_server;
    Coroutine* m_trd;
    netfd_t m_inotifyFd;
public:
    InotifyWorker(Server* s);
    virtual ~InotifyWorker();
public:
    virtual error Start();
    //interface IEndlessThreadHandler
public:
    virtual error Cycle();
};

// TODO: FIXME: Rename to SrsLiveServer.
// SRS RTMP server, initialize and listen, start connection service thread, destroy client.
class Server : public IReloadHandler, public ILiveSourceHandler, public ITcpHandler
    , public IResourceManager, public ICoroutineHandler, public IHourGlass
{
private:
    // TODO: FIXME: Extract an HttpApiServer.
    IHttpServeMux* m_httpApiMux;
    HttpServer* m_httpServer;
private:
    HttpHeartbeat* m_httpHeartbeat;
    Ingester* m_ingester;
    ResourceManager* m_connManager;
    Coroutine* m_trd;
    HourGlass* m_timer;
    WaitGroup* m_wg;
private:
    // The pid file fd, lock the file write when server is running.
    // @remark the init.d script should cleanup the pid file, when stop service,
    //       for the server never delete the file; when system startup, the pid in pid file
    //       maybe valid but the process is not SRS, the init.d script will never start server.
    int m_pidFd;
private:
    // If reusing, HTTP API use the same port of HTTP server.
    bool m_reuseApiOverServer;
    // If reusing, WebRTC TCP use the same port of HTTP server.
    bool m_reuseRtcOverServer;
    // RTMP stream listeners, over TCP.
    MultipleTcpListeners* m_rtmpListener;
    // HTTP API listener, over TCP. Please note that it might reuse with stream listener.
    TcpListener* m_apiListener;
    // HTTPS API listener, over TCP. Please note that it might reuse with stream listener.
    TcpListener* m_apisListener;
    // HTTP server listener, over TCP. Please note that request of both HTTP static and stream are served by this
    // listener, and it might be reused by HTTP API and WebRTC TCP.
    TcpListener* m_httpListener;
    // HTTPS server listener, over TCP. Please note that request of both HTTP static and stream are served by this
    // listener, and it might be reused by HTTP API and WebRTC TCP.
    TcpListener* m_httpsListener;
    // WebRTC over TCP listener. Please note that there is always a UDP listener by RTC server.
    TcpListener* m_webrtcListener;
    // Service server listener, over TCP.
    TcpListener* m_serviceListener;
    // Stream Caster for push over HTTP-FLV.
    HttpFlvListener* m_streamCasterFlvListener;
    // Stream Caster for push over MPEGTS-UDP
    UdpCasterListener* m_streamCasterMpegts;
    // Exporter server listener, over TCP. Please note that metrics request of HTTP is served by this
    // listener, and it might be reused by HTTP API.
    TcpListener* m_exporterListener;

private:
    // Signal manager which convert gignal to io message.
    SignalManager* m_signalManager;
    // To query the latest available version of SRS.
    LatestVersion* m_latestVersion;
    // User send the signal, convert to variable.
    bool m_signalReload;
    bool m_signalPersistenceConfig;
    bool m_signalGmcStop;
    bool m_signalFastQuit;
    bool m_signalGracefullyQuit;
    // Parent pid for asprocess.
    int m_ppid;
    Database* m_database = nullptr;
public:
    Server();
    virtual ~Server();
private:
    // The destroy is for gmc to analysis the memory leak,
    // if not destroy global/static data, the gmc will warning memory leak.
    // In service, server never destroy, directly exit when restart.
    virtual void Destroy();
    // When SIGTERM, SRS should do cleanup, for example,
    // to stop all ingesters, cleanup HLS and dvr.
    virtual void Dispose();
    // Close listener to stop accepting new connections,
    // then wait and quit when all connections finished.
    virtual void GracefullyDispose();
// server startup workflow, @see run_master()
public:
    // Initialize server with callback handler ch.
    // @remark user must free the handler.
    virtual error Initialize();
    virtual error InitializeSt();
    virtual error InitializeSignal();
    virtual error Listen();
    virtual error RegisterSignal();
    virtual error HttpHandle();
    virtual error Ingest();
public:
    virtual error Start(WaitGroup* wg);
    void Stop();
// interface ISrsCoroutineHandler
public:
    virtual error Cycle();
// server utilities.
public:
    // The callback for signal manager got a signal.
    // The signal manager convert signal to io message,
    // whatever, we will got the signo like the orignal signal(int signo) handler.
    // @param signo the signal number from user, where:
    //      SRS_SIGNAL_FAST_QUIT, the SIGTERM, do essential dispose then quit.
    //      SRS_SIGNAL_GRACEFULLY_QUIT, the SIGQUIT, do careful dispose then quit.
    //      SRS_SIGNAL_REOPEN_LOG, the SIGUSR1, reopen the log file.
    //      SRS_SIGNAL_RELOAD, the SIGHUP, reload the config.
    //      SRS_SIGNAL_PERSISTENCE_CONFIG, application level signal, persistence config to file.
    // @remark, for SIGINT:
    //       no gmc, fast quit, do essential dispose then quit.
    //       for gmc, set the variable signal_gmc_stop, the cycle will return and cleanup for gmc.
    // @remark, maybe the HTTP RAW API will trigger the on_signal() also.
    virtual void OnSignal(int signo);
private:
    // The server thread main cycle,
    // update the global static data, for instance, the current time,
    // the cpu/mem/network statistic.
    virtual error DoCycle();
// interface ISrsHourGlass
private:
    virtual error SetupTicks();
    virtual error Notify(int event, utime_t interval, utime_t tick);
private:
    // Resample the server kbs.
    virtual void ResampleKbps();
// For internal only
public:
    // TODO: FIXME: Fetch from hybrid server manager.
    virtual IHttpServeMux* ApiServer();
// Interface ISrsTcpHandler
public:
    virtual error OnTcpClient(IListener* listener, netfd_t stfd);
private:
    virtual error DoOnTcpClient(IListener* listener, netfd_t& stfd);
    virtual error OnBeforeConnection(netfd_t& stfd, const std::string& ip, int port);
// Interface ISrsResourceManager
public:
    // A callback for connection to remove itself.
    // When connection thread cycle terminated, callback this to delete connection.
    // @see SrsTcpConnection.on_thread_stop().
    virtual void Remove(IResource* c);
// Interface ISrsReloadHandler.
public:
    virtual error OnReloadListen();
// Interface ISrsLiveSourceHandler
public:
    virtual error OnPublish(LiveSource* s, Request* r);
    virtual void OnUnpublish(LiveSource* s, Request* r);
};

// The SRS server adapter, the master server.
class ServerAdapter : public IHybridServer
{
private:
    Server* m_srs;
public:
    ServerAdapter();
    virtual ~ServerAdapter();
public:
    virtual error Initialize();
    virtual error Run(WaitGroup* wg);
    virtual void Stop();
public:
    virtual Server* Instance();
};

#endif // APP_SERVER_H

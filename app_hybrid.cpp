#include "app_hybrid.h"
#include "app_utility.h"
#include "kbps.h"
#include "app_server.h"

extern Pps* pps_cids_get;
extern Pps* pps_cids_set;

extern Pps* pps_timer;
extern Pps* pps_conn;
extern Pps* pps_pub;
extern Pps* pps_dispose;

Pps* pps_clock_15ms = NULL;
Pps* pps_clock_20ms = NULL;
Pps* pps_clock_25ms = NULL;
Pps* pps_clock_30ms = NULL;
Pps* pps_clock_35ms = NULL;
Pps* pps_clock_40ms = NULL;
Pps* pps_clock_80ms = NULL;
Pps* pps_clock_160ms = NULL;
Pps* pps_timer_s = NULL;

extern Pps* pps_objs_rtps;
extern Pps* pps_objs_rraw;
extern Pps* pps_objs_rfua;
extern Pps* pps_objs_rbuf;
extern Pps* pps_objs_msgs;
extern Pps* pps_objs_rothers;

IHybridServer::IHybridServer()
{

}

IHybridServer::~IHybridServer()
{

}

HybridServer::HybridServer()
{
    //create global shared timer
    m_timer20ms = new FastTimer("hybrid", 20 * UTIME_MILLISECONDS);
    m_timer100ms = new FastTimer("hybrid", 100 * UTIME_MILLISECONDS);
    m_timer1s = new FastTimer("hybrid", 1 * UTIME_SECONDS);
    m_timer5s = new FastTimer("hybrid", 5 * UTIME_SECONDS);

    m_clockMonitor = new ClockWallMonitor();
}

HybridServer::~HybridServer()
{
    Freep(m_clockMonitor);

    Freep(m_timer20ms);
    Freep(m_timer100ms);
    Freep(m_timer1s);
    Freep(m_timer5s);

    std::vector<IHybridServer*>::iterator it;
    for(it = m_servers.begin(); it != m_servers.end(); ++it){
        IHybridServer* server = *it;
        Freep(server);
    }

    m_servers.clear();
}

void HybridServer::RegisterServer(IHybridServer *svr)
{
    m_servers.push_back(svr);
}

error HybridServer::Initialize()
{
    error err = SUCCESS;

    //start the timer first
    if((err = m_timer20ms->Start()) != SUCCESS){
        return ERRORWRAP(err, "start timer");
    }

    if((err = m_timer100ms->Start()) != SUCCESS){
        return ERRORWRAP(err, "start timer");
    }

    if((err = m_timer1s->Start()) != SUCCESS){
        return ERRORWRAP(err, "start timer");
    }

    if((err = m_timer5s->Start()) != SUCCESS){
        return ERRORWRAP(err, "start timer");
    }

    //    //start the dvr async call
    //    if((err = dvr_async->Start()) != SUCCESS){
    //        return ERRORWRAP(err, "dvr async");
    //    }

    //    // Initialize TencentCloud CLS object.
    //    if ((err = _srs_cls->initialize()) != srs_success) {
    //        return srs_error_wrap(err, "cls client");
    //    }
    //    if ((err = _srs_apm->initialize()) != srs_success) {
    //        return srs_error_wrap(err, "apm client");
    //    }

    //register some timers
    m_timer20ms->Subscribe(m_clockMonitor);
    m_timer5s->Subscribe(this);

    //initialize all hybrid servers
    std::vector<IHybridServer*>::iterator it;
    for(it = m_servers.begin(); it != m_servers.end(); ++it){
        IHybridServer* server = *it;

        if((err = server->Initialize()) != SUCCESS){
            return ERRORWRAP(err, "init server");
        }
    }

    return err;
}

error HybridServer::Run()
{
    error err = SUCCESS;

    //wait for all servers which need to do cleanup
    WaitGroup wg;

    std::vector<IHybridServer*>::iterator it;
    for(it = m_servers.begin(); it != m_servers.end(); ++it){
        IHybridServer* server = *it;
        if((err = server->Run(&wg)) != SUCCESS){
            return ERRORWRAP(err, "run server");
        }
    }

    //wait for all server to quit
    wg.Wait();

    return err;
}

void HybridServer::Stop()
{
    std::vector<IHybridServer*>::iterator it;
    for(it = m_servers.begin(); it != m_servers.end(); ++it){
        IHybridServer* server = *it;
        server->Stop();
    }
}

ServerAdapter *HybridServer::Server()
{
    for(std::vector<IHybridServer*>::iterator it = m_servers.begin(); it != m_servers.end(); ++it){
        if(dynamic_cast<ServerAdapter*>(*it)){
            return dynamic_cast<ServerAdapter*>(*it);
        }
    }

    return nullptr;
}

FastTimer *HybridServer::timer20ms()
{
    return m_timer20ms;
}

FastTimer *HybridServer::timer100ms()
{
    return m_timer100ms;
}

FastTimer *HybridServer::timer1s()
{
    return m_timer1s;
}

FastTimer *HybridServer::timer5s()
{
    return m_timer5s;
}

error HybridServer::OnTimer(utime_t interval)
{
    error err = SUCCESS;

    //show statistics for rtc server
    ProcSelfStat* u = GetSelfProcStat();
    //resident set size: number of pages the process has in real memory

    int memory = (int)(u->m_rss * 4 / 1024);

    static char buf[128];

    std::string cid_desc;
    pps_cids_get->Update();
    pps_cids_set->Update();
    if(pps_cids_get->R10s() || pps_cids_set->R10s()){
        snprintf(buf, sizeof(buf), ", cid=%d,%d", pps_cids_get->R10s(), pps_cids_set->R10s());
        cid_desc = buf;
    }

    std::string timer_desc;
    pps_timer->Update();
    pps_pub->Update();
    pps_conn->Update();
    if(pps_timer->R10s() || pps_pub->R10s() || pps_conn->R10s()){
        snprintf(buf, sizeof(buf), ", timer=%d,%d,%d", pps_timer->R10s(), pps_pub->R10s(), pps_conn->R10s());
        timer_desc = buf;
    }

    std::string free_desc;
    pps_dispose->Update();
    if (pps_dispose->R10s()) {
        snprintf(buf, sizeof(buf), ", free=%d", pps_dispose->R10s());
        free_desc = buf;
    }

    std::string recvfrom_desc;

    std::string io_desc;

    std::string msg_desc;

    std::string epoll_desc;

    std::string sched_desc;

    std::string clock_desc;
    pps_clock_15ms->Update();
    pps_clock_20ms->Update();
    pps_clock_25ms->Update();
    pps_clock_30ms->Update();
    pps_clock_35ms->Update();
    pps_clock_40ms->Update();
    pps_clock_80ms->Update();
    pps_clock_160ms->Update();
    pps_timer_s->Update();
    if (pps_clock_15ms->R10s() || pps_clock_20ms->R10s() || pps_clock_25ms->R10s() || pps_clock_30ms->R10s() || pps_clock_35ms->R10s() || pps_clock_40ms->R10s() || pps_clock_80ms->R10s() || pps_clock_160ms->R10s()){
        snprintf(buf, sizeof(buf), ", clock=%d,%d,%d,%d,%d,%d,%d,%d,%d", pps_clock_15ms->R10s(), pps_clock_20ms->R10s(), pps_clock_25ms->R10s(), pps_clock_30ms->R10s(), pps_clock_35ms->R10s(), pps_clock_40ms->R10s(), pps_clock_80ms->R10s(), pps_clock_160ms->R10s(), pps_timer_s->R10s());
        clock_desc = buf;
    }

    std::string thread_desc;

    std::string objs_desc;

    trace("Hybrid cpu=%.2f%%,%dMB%s%s%s%s%s%s%s%s%s%s%s", u->m_percent * 100, memory, cid_desc.c_str(), timer_desc.c_str(), recvfrom_desc.c_str(), io_desc.c_str(), msg_desc.c_str(), epoll_desc.c_str(), sched_desc.c_str(), clock_desc.c_str(), thread_desc.c_str(), free_desc.c_str(), objs_desc.c_str());

//    // Report logs to CLS if enabled.
//    if ((err = _srs_cls->report()) != srs_success) {
//        srs_warn("ignore cls err %s", srs_error_desc(err).c_str());
//        srs_freep(err);
//    }

//    // Report logs to APM if enabled.
//    if ((err = _srs_apm->report()) != srs_success) {
//        srs_warn("ignore apm err %s", srs_error_desc(err).c_str());
//        srs_freep(err);
//    }

    return err;
}

HybridServer* hybrid = nullptr;

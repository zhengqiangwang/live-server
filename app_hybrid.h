#ifndef APP_HYBRID_H
#define APP_HYBRID_H

#include "core.h"
#include "app_hourglass.h"
#include <vector>

class Server;
class ServerAdapter;
class WaitGroup;

//the hybrid server interfaces, we could register many servers.
class IHybridServer
{
public:
    IHybridServer();
    virtual ~IHybridServer();
public:
    //only st initialized before each server, we could fork processes as such.
    virtual error Initialize() = 0;
    //run each server, should never block except the master sever.
    virtual error Run(WaitGroup* wg) = 0;
    //stop each server, should do cleanup, for example, kill processes forked by server.
    virtual void Stop() = 0;
};

//the hybrid server manager
class HybridServer : public IFastTimer
{
private:
    std::vector<IHybridServer*> m_servers;
    FastTimer* m_timer20ms;
    FastTimer* m_timer100ms;
    FastTimer* m_timer1s;
    FastTimer* m_timer5s;
    ClockWallMonitor* m_clockMonitor;
public:
    HybridServer();
    virtual ~HybridServer();
public:
    virtual void RegisterServer(IHybridServer* svr);
public:
    virtual error Initialize();
    virtual error Run();
    virtual void Stop();
public:
    virtual ServerAdapter* Server();
    FastTimer* timer20ms();
    FastTimer* timer100ms();
    FastTimer* timer1s();
    FastTimer* timer5s();
//interface IFastTimer
private:
    error OnTimer(utime_t interval);
};

extern HybridServer* hybrid;

#endif // APP_HYBRID_H

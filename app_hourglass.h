#ifndef APP_HOURGLASS_H
#define APP_HOURGLASS_H


#include "core_time.h"
#include "app_st.h"
#include <map>
#include <vector>

class Coroutine;

//the handler for the tick
class IHourGlass
{
public:
    IHourGlass();
    virtual ~IHourGlass();
public:
    //when time is ticked, this function is called.
    virtual error Notify(int event, utime_t interval, utime_t tick) = 0;
};

// The hourglass(timer or Timer) for special tasks,
// while these tasks are attached to some intervals, for example,
// there are N=3 tasks bellow:
//          1. A heartbeat every 3s.
//          2. A print message every 5s.
//          3. A notify backend every 7s.
// The hourglass will call back when ticks:
//          1. Got notify(event=1, time=3)
//          2. Got notify(event=2, time=5)
//          3. Got notify(event=1, time=6)
//          4. Got notify(event=3, time=7)
//          5. Got notify(event=1, time=9)
//          6. Got notify(event=2, time=10)
// It's a complex and high-performance timer.
//
// Usage:
//      HourGlass* hg = new HourGlass("nack", handler, 100 * UTIME_MILLISECONDS);
//
//      hg->tick(1, 300 * UTIME_MILLISECONDS);
//      hg->tick(2, 500 * UTIME_MILLISECONDS);
//      hg->tick(3, 700 * UTIME_MILLISECONDS);
//
//      // The hg will create a thread for timer.
//      hg->start();

class HourGlass : public ICoroutineHandler
{
private:
    std::string m_label;
    Coroutine* m_trd;
    IHourGlass* m_handler;
    utime_t m_resolution;
    //key: the event of tick; value: the interval of tick
    std::map<int, utime_t> m_ticks;
    //the total elapsed time, for each cycle, we increase it with a resolution.
    utime_t m_totalElapse;
public:
    HourGlass(std::string label, IHourGlass* h, utime_t resolution);
    virtual ~HourGlass();
public:
    //start or stop the hourglass
    virtual error Start();
    virtual void Stop();
public:
    //add a pair of tick(event, interval)
    //event default is 0
    virtual error Tick(utime_t interval);
    virtual error Tick(int event, utime_t interval);
    //remove the tick by event.
    void UnTick(int event);
public:
    //cycle the hourglass, which will sleep resolution every time.
    //and call handler when ticked
    virtual error Cycle();
};

//the handler for fast timer
class IFastTimer
{
public:
    IFastTimer();
    virtual ~IFastTimer();
public:
    //tick when timer is active
    virtual error OnTimer(utime_t interval) = 0;
};

// the fast timer, shared by objects, for high performance.
// For example, we should never start a timer for each connection or publisher or player,
// instead, we should start only one fast timer in server.
class FastTimer : public ICoroutineHandler
{
private:
    Coroutine* m_trd;
    utime_t m_interval;
    std::vector<IFastTimer*> m_handlers;
public:
    FastTimer(std::string label, utime_t interval);
    virtual ~FastTimer();
public:
    error Start();
public:
    void Subscribe(IFastTimer* timer);
    void UnSubscribe(IFastTimer* timer);
//interface ICoroutineHandler
private:
    //cycle the hourglass, which will sleep resolution every time.
    //and call handler when ticked
    virtual error Cycle();
};

//to monitor the system wall clock timer deviation.
class ClockWallMonitor : public IFastTimer
{
public:
    ClockWallMonitor();
    virtual ~ClockWallMonitor();
//interface IFastTimer
private:
    error OnTimer(utime_t interval);
};

#endif // APP_HOURGLASS_H

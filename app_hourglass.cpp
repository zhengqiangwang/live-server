#include "app_hourglass.h"
#include "kbps.h"
#include "log.h"
#include "utility.h"
#include <algorithm>

Pps* pps_timer = nullptr;
Pps* pps_conn = nullptr;
Pps* pps_pub = nullptr;

extern Pps* pps_clock_15ms;
extern Pps* pps_clock_20ms;
extern Pps* pps_clock_25ms;
extern Pps* pps_clock_30ms;
extern Pps* pps_clock_35ms;
extern Pps* pps_clock_40ms;
extern Pps* pps_clock_80ms;
extern Pps* pps_clock_160ms;
extern Pps* pps_timer_s;


IHourGlass::IHourGlass()
{

}

IHourGlass::~IHourGlass()
{

}

HourGlass::HourGlass(std::string label, IHourGlass *h, utime_t resolution)
{
    m_label = label;
    m_handler = h;
    m_resolution = resolution;
    m_totalElapse = 0;
    m_trd = new STCoroutine("timer-" + label, this, Context->GetId());
}

HourGlass::~HourGlass()
{
    Freep(m_trd);
}

error HourGlass::Start()
{
    error err = SUCCESS;

    if((err = m_trd->Start()) != SUCCESS){
        return ERRORWRAP(err, "start timer");
    }

    return err;
}

void HourGlass::Stop()
{
    m_trd->Stop();
}

error HourGlass::Tick(utime_t interval)
{
    return Tick(0, interval);
}

error HourGlass::Tick(int event, utime_t interval)
{
    error err = SUCCESS;
    if(m_resolution > 0 && (interval % m_resolution) != 0){
        return ERRORNEW(ERROR_SYSTEM_HOURGLASS_RESOLUTION, "invalid interval=%dms, resolution=%dms", u2msi(interval), u2msi(m_resolution));
    }

    m_ticks[event] = interval;

    return err;
}

void HourGlass::UnTick(int event)
{
    std::map<int, utime_t>::iterator it = m_ticks.find(event);
    if(it != m_ticks.end())
    {
        m_ticks.erase(it);
    }
}

error HourGlass::Cycle()
{
    error err = SUCCESS;

    while (true) {
        if((err = m_trd->Pull()) != SUCCESS){
            return ERRORWRAP(err, "quit");
        }

        std::map<int, utime_t>::iterator it;
        for(it = m_ticks.begin(); it != m_ticks.end(); ++it){
            int event = it->first;
            utime_t interval = it->second;

            if(interval == 0 || (m_totalElapse % interval) == 0){
                ++pps_timer->m_sugar;

                if((err = m_handler->Notify(event, interval, m_totalElapse)) != SUCCESS){
                    return ERRORWRAP(err, "notify");
                }
            }
        }

        m_totalElapse += m_resolution;
        st_usleep(m_resolution);
    }

    return err;
}



IFastTimer::IFastTimer()
{

}

IFastTimer::~IFastTimer()
{

}

FastTimer::FastTimer(std::string label, utime_t interval)
{
    m_interval = interval;
    m_trd = new STCoroutine(label, this, Context->GetId());
}

FastTimer::~FastTimer()
{
    Freep(m_trd);
}

error FastTimer::Start()
{
    error err = SUCCESS;

    if((err = m_trd->Start()) != SUCCESS){
        return ERRORWRAP(err, "start timer");
    }

    return err;
}

void FastTimer::Subscribe(IFastTimer *timer)
{
    if(std::find(m_handlers.begin(), m_handlers.end(), timer) == m_handlers.end()){
        m_handlers.push_back(timer);
    }
}

void FastTimer::UnSubscribe(IFastTimer *timer)
{
    std::vector<IFastTimer*>::iterator it = std::find(m_handlers.begin(), m_handlers.end(), timer);
    if(it != m_handlers.end()){
        it = m_handlers.erase(it);
    }
}

error FastTimer::Cycle()
{
    error err = SUCCESS;

    while(true){
        if((err = m_trd->Pull()) != SUCCESS){
            return ERRORWRAP(err, "quit");
        }

        ++pps_timer->m_sugar;

        for(int i = 0; i < (int)m_handlers.size(); i++){
            IFastTimer* timer = m_handlers.at(i);

            if((err == timer->OnTimer(m_interval)) != SUCCESS){
                Freep(err); //ignore any error for shared timer
            }
        }

        st_usleep(m_interval);
    }

    return err;
}

ClockWallMonitor::ClockWallMonitor()
{

}

ClockWallMonitor::~ClockWallMonitor()
{

}

error ClockWallMonitor::OnTimer(utime_t interval)
{
    error err = SUCCESS;
    static utime_t clock = 0;
    utime_t now = UpdateSystemTime();
    if(!clock){
        clock = now;
        return err;
    }

    utime_t elapsed = now - clock;
    clock = now;

    if(elapsed <= 15 * UTIME_MILLISECONDS){
        ++pps_clock_15ms->m_sugar;
    } else if(elapsed <= 21 * UTIME_MILLISECONDS)
    {
        ++pps_clock_20ms->m_sugar;
    } else if(elapsed <= 25 * UTIME_MILLISECONDS)
    {
        ++pps_clock_25ms->m_sugar;
    } else if(elapsed <= 30 * UTIME_MILLISECONDS)
    {
        ++pps_clock_30ms->m_sugar;
    } else if(elapsed <= 35 * UTIME_MILLISECONDS)
    {
        ++pps_clock_35ms->m_sugar;
    } else if(elapsed <= 40 * UTIME_MILLISECONDS)
    {
        ++pps_clock_40ms->m_sugar;
    } else if(elapsed <= 80 * UTIME_MILLISECONDS)
    {
        ++pps_clock_80ms->m_sugar;
    } else if(elapsed <= 160 * UTIME_MILLISECONDS)
    {
        ++pps_clock_160ms->m_sugar;
    } else {
        ++pps_timer_s->m_sugar;
    }

    return err;
}

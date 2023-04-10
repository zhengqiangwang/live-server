#include "kbps.h"
#include "error.h"
#include "utility.h"

RateSample::RateSample()
{
    m_total = m_time = -1;
    m_rate = 0;
}

RateSample::~RateSample()
{

}

RateSample *RateSample::Update(int64_t nn, utime_t t, int k)
{
    m_total = nn;
    m_time = t;
    m_rate = k;

    return this;
}

void PpsInit(RateSample& sample, int64_t nn, utime_t now)
{
    if(sample.m_time < 0 || nn < sample.m_total){
        sample.Update(nn, now, 0);
    }
}

void PpsUpdate(RateSample& sample, int64_t nn, utime_t now)
{
    int pps = (int)((nn - sample.m_total) * 1000 / u2ms(now - sample.m_time));
    if(pps == 0 && nn > sample.m_total){
        pps = 1;    //for pps in (0, 1), we set to 1
    }
    sample.Update(nn, now, pps);
}

Pps::Pps()
{
    m_clk = _clock;
    m_sugar = 0;
}

Pps::~Pps()
{

}

void Pps::Update()
{
    Update(m_sugar);
}

void Pps::Update(int64_t nn)
{
    Assert(m_clk);

    utime_t now = m_clk->Now();

    PpsInit(m_sample10s, nn, now);
    PpsInit(m_sample30s, nn, now);
    PpsInit(m_sample1m, nn, now);
    PpsInit(m_sample5m, nn, now);
    PpsInit(m_sample60m, nn, now);

    if(now - m_sample10s.m_time >= 10 * UTIME_SECONDS){
        PpsUpdate(m_sample10s, nn, now);
    }
    if(now - m_sample30s.m_time >= 30 * UTIME_SECONDS){
        PpsUpdate(m_sample30s, nn, now);
    }
    if(now - m_sample1m.m_time >= 60 * UTIME_SECONDS)
    {
        PpsUpdate(m_sample1m, nn, now);
    }
    if(now - m_sample5m.m_time >= 300 * UTIME_SECONDS)
    {
        PpsUpdate(m_sample5m, nn, now);
    }
    if(now - m_sample60m.m_time >= 3600 * UTIME_SECONDS)
    {
        PpsUpdate(m_sample60m, nn, now);
    }

}

int Pps::R10s()
{
    return m_sample10s.m_rate;
}

int Pps::R30s()
{
    return m_sample30s.m_rate;
}

WallClock::WallClock()
{

}

WallClock::~WallClock()
{

}

utime_t WallClock::Now()
{
    return GetSystemTime();
}

WallClock* _clock = nullptr;

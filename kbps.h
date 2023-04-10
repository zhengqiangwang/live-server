#ifndef KBPS_H
#define KBPS_H

#include "core.h"
#include "core_time.h"

class WallClock;

//A sample for rate-based stat, such as kbps or kps.
class RateSample
{
public:
    int64_t m_total;
    utime_t m_time;
    //kbps or kps
    int m_rate;
public:
    RateSample();
    virtual ~RateSample();
public:
    virtual RateSample* Update(int64_t nn, utime_t t, int k);
};

//a pps manager every some duration
class Pps
{
private:
    WallClock* m_clk;
private:
    //samples
    RateSample m_sample10s;
    RateSample m_sample30s;
    RateSample m_sample1m;
    RateSample m_sample5m;
    RateSample m_sample60m;
public:
    //sugar for target to stat
    int64_t m_sugar;
public:
    Pps();
    virtual ~Pps();
public:
    //Update with the nn which is target.
    void Update();
    //update with the nn
    void Update(int64_t nn);
    //get the 10s average stat
    int R10s();
    //get the 30s average stat
    int R30s();
};

//a time source to provide wall clock
class WallClock
{
public:
    WallClock();
    virtual ~WallClock();
public:
    //current time in utime_t
    virtual utime_t Now();
};

//the global clock
extern WallClock* _clock;

#endif // KBPS_H

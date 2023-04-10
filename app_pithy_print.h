#ifndef APP_PITHY_PRINT_H
#define APP_PITHY_PRINT_H

#include "core_time.h"
#include "app_reload.h"
#include <map>
// The stage info to calc the age.

class StageInfo : public IReloadHandler
{
public:
    int m_stageId;
    utime_t m_interval;
    int m_nbClients;
    // The number of call of can_print().
    uint32_t m_nnCount;
    // The ratio for interval, 1.0 means no change.
    double m_intervalRatio;
public:
    utime_t m_age;
public:
    StageInfo(int _stage_id, double ratio = 1.0);
    virtual ~StageInfo();
    virtual void UpdatePrintTime();
public:
    virtual void Elapse(utime_t diff);
    virtual bool CanPrint();
public:
    virtual error OnReloadPithyPrint();
};

// The manager for stages, it's used for a single client stage.
// Of course, we can add the multiple user support, which is SrsPithyPrint.
class StageManager
{
private:
    std::map<int, StageInfo*> m_stages;
public:
    StageManager();
    virtual ~StageManager();
public:
    // Fetch a stage, create one if not exists.
    StageInfo* FetchOrCreate(int stage_id, bool* pnew = NULL);
};

// The error pithy print is a single client stage manager, each stage only has one client.
// For example, we use it for error pithy print for each UDP packet processing.
class ErrorPithyPrint
{
public:
    // The number of call of can_print().
    uint32_t m_nnCount;
private:
    double m_ratio;
    StageManager m_stages;
    std::map<int, utime_t> m_ticks;
public:
    ErrorPithyPrint(double ratio = 1.0);
    virtual ~ErrorPithyPrint();
public:
    // Whether specified stage is ready for print.
    bool CanPrint(error err, uint32_t* pnn = NULL);
    // We also support int error code.
    bool CanPrint(int err, uint32_t* pnn = NULL);
};

// An standalone pithy print, without shared stages.
class AlonePithyPrint
{
private:
    StageInfo m_info;
    utime_t m_previousTick;
public:
    AlonePithyPrint();
    virtual ~AlonePithyPrint();
public:
    virtual void Elapse();
    virtual bool CanPrint();
};

// The stage is used for a collection of object to do print,
// the print time in a stage is constant and not changed,
// that is, we always got one message to print every specified time.
//
// For example, stage #1 for all play clients, print time is 3s,
// if there is 1client, it will print every 3s.
// if there is 10clients, random select one to print every 3s.
// Usage:
//        SrsPithyPrint* pprint = SrsPithyPrint::create_rtmp_play();
//        SrsAutoFree(SrsPithyPrint, pprint);
//        while (true) {
//            pprint->elapse();
//            if (pprint->can_print()) {
//                // print pithy message.
//                // user can get the elapse time by: pprint->age()
//            }
//            // read and write RTMP messages.
//        }
class PithyPrint
{
private:
    int m_clientId;
    StageInfo* m_cache;
    int m_stageId;
    utime_t m_age;
    utime_t m_previousTick;
private:
    PithyPrint(int _stage_id);
public:
    static PithyPrint* CreateRtmpPlay();
    static PithyPrint* CreateRtmpPublish();
    static PithyPrint* CreateHls();
    static PithyPrint* CreateForwarder();
    static PithyPrint* CreateEncoder();
    static PithyPrint* CreateExec();
    static PithyPrint* CreateIngester();
    static PithyPrint* CreateEdge();
    static PithyPrint* CreateCaster();
    static PithyPrint* CreateHttpStream();
    static PithyPrint* CreateHttpStreamCache();
    static PithyPrint* CreateRtcPlay();
    // For RTC sender and receiver, we create printer for each fd.
    static PithyPrint* CreateRtcSend(int fd);
    static PithyPrint* CreateRtcRecv(int fd);
#ifdef SRS_SRT
    static PithyPrint* create_srt_play();
    static PithyPrint* create_srt_publish();
#endif
    virtual ~PithyPrint();
private:
    // Enter the specified stage, return the client id.
    virtual int EnterStage();
    // Leave the specified stage, release the client id.
    virtual void LeaveStage();
public:
    // Auto calc the elapse time
    virtual void Elapse();
    // Whether current client can print.
    virtual bool CanPrint();
    // Get the elapsed time in srs_utime_t.
    virtual utime_t Age();
};

#endif // APP_PITHY_PRINT_H

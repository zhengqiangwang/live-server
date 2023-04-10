#include "app_pithy_print.h"
#include "app_config.h"
#include "log.h"
#include "utility.h"

StageInfo::StageInfo(int _stage_id, double ratio)
{
    m_stageId = _stage_id;
    m_nbClients = 0;
    m_age = 0;
    m_nnCount = 0;
    m_intervalRatio = ratio;

    UpdatePrintTime();

    config->Subscribe(this);
}

StageInfo::~StageInfo()
{
    config->Unsubscribe(this);
}

void StageInfo::UpdatePrintTime()
{
    m_interval = config->GetPithyPrint();
}

void StageInfo::Elapse(utime_t diff)
{
    m_age += diff;
}

bool StageInfo::CanPrint()
{
    utime_t can_print_age = m_nbClients * (utime_t)(m_intervalRatio * m_interval);

    bool can_print = m_age >= can_print_age;
    if (can_print) {
        m_age = 0;
    }

    return can_print;
}

error StageInfo::OnReloadPithyPrint()
{
    UpdatePrintTime();
    return SUCCESS;
}

StageManager::StageManager()
{

}

StageManager::~StageManager()
{
    std::map<int, StageInfo*>::iterator it;
    for (it = m_stages.begin(); it != m_stages.end(); ++it) {
        StageInfo* stage = it->second;
        Freep(stage);
    }
}

StageInfo *StageManager::FetchOrCreate(int stage_id, bool *pnew)
{
    std::map<int, StageInfo*>::iterator it = m_stages.find(stage_id);

    // Create one if not exists.
    if (it == m_stages.end()) {
        StageInfo* stage = new StageInfo(stage_id);
        m_stages[stage_id] = stage;

        if (pnew) {
            *pnew = true;
        }

        return stage;
    }

    // Exists, fetch it.
    StageInfo* stage = it->second;

    if (pnew) {
        *pnew = false;
    }

    return stage;
}

ErrorPithyPrint::ErrorPithyPrint(double ratio)
{
    m_nnCount = 0;
    m_ratio = ratio;
}

ErrorPithyPrint::~ErrorPithyPrint()
{

}

bool ErrorPithyPrint::CanPrint(error err, uint32_t *pnn)
{
    int error_code = ERRORCODE(err);
    return CanPrint(error_code, pnn);
}

bool ErrorPithyPrint::CanPrint(int error_code, uint32_t *pnn)
{
    bool new_stage = false;
    StageInfo* stage = m_stages.FetchOrCreate(error_code, &new_stage);

    // Increase the count.
    stage->m_nnCount++;
    m_nnCount++;

    if (pnn) {
        *pnn = stage->m_nnCount;
    }

    // Always and only one client.
    if (new_stage) {
        stage->m_nbClients = 1;
        stage->m_intervalRatio = m_ratio;
    }

    utime_t tick = m_ticks[error_code];
    if (!tick) {
        m_ticks[error_code] = tick = GetSystemTime();
    }

    utime_t diff = GetSystemTime() - tick;
    diff = MAX(0, diff);

    stage->Elapse(diff);
    m_ticks[error_code] = GetSystemTime();

    return new_stage || stage->CanPrint();
}

AlonePithyPrint::AlonePithyPrint() : m_info(0)
{
    //stage work for one print
    m_info.m_nbClients = 1;

    m_previousTick = GetSystemTime();
}

AlonePithyPrint::~AlonePithyPrint()
{

}

void AlonePithyPrint::Elapse()
{
    utime_t diff = GetSystemTime() - m_previousTick;
    m_previousTick = GetSystemTime();

    diff = MAX(0, diff);

    m_info.Elapse(diff);
}

bool AlonePithyPrint::CanPrint()
{
    return m_info.CanPrint();
}

// The global stage manager for pithy print, multiple stages.
StageManager* _stages = NULL;

PithyPrint::PithyPrint(int _stage_id)
{
    _stage_id = _stage_id;
    m_cache = NULL;
    m_clientId = EnterStage();
    m_previousTick = GetSystemTime();
    m_age = 0;
}

///////////////////////////////////////////////////////////
// pithy-print consts values
///////////////////////////////////////////////////////////
// the pithy stage for all play clients.
#define CONSTS_STAGE_PLAY_USER 1
// the pithy stage for all publish clients.
#define CONSTS_STAGE_PUBLISH_USER 2
// the pithy stage for all forward clients.
#define CONSTS_STAGE_FORWARDER 3
// the pithy stage for all encoders.
#define CONSTS_STAGE_ENCODER 4
// the pithy stage for all hls.
#define CONSTS_STAGE_HLS 5
// the pithy stage for all ingesters.
#define CONSTS_STAGE_INGESTER 6
// the pithy stage for all edge.
#define CONSTS_STAGE_EDGE 7
// the pithy stage for all stream caster.
#define CONSTS_STAGE_CASTER 8
// the pithy stage for all http stream.
#define CONSTS_STAGE_HTTP_STREAM 9
// the pithy stage for all http stream cache.
#define CONSTS_STAGE_HTTP_STREAM_CACHE 10
// for the ng-exec stage.
#define CONSTS_STAGE_EXEC 11
// for the rtc play
#define CONSTS_STAGE_RTC_PLAY 12
// for the rtc send
#define CONSTS_STAGE_RTC_SEND 13
// for the rtc recv
#define CONSTS_STAGE_RTC_RECV 14

#ifdef SRS_SRT
// the pithy stage for srt play clients.
#define SRS_CONSTS_STAGE_SRT_PLAY 15
// the pithy stage for srt publish clients.
#define SRS_CONSTS_STAGE_SRT_PUBLISH 16
#endif

PithyPrint *PithyPrint::CreateRtmpPlay()
{
    return new PithyPrint(CONSTS_STAGE_PLAY_USER);
}

PithyPrint *PithyPrint::CreateRtmpPublish()
{
    return new PithyPrint(CONSTS_STAGE_PUBLISH_USER);
}

PithyPrint *PithyPrint::CreateHls()
{
    return new PithyPrint(CONSTS_STAGE_HLS);
}

PithyPrint *PithyPrint::CreateForwarder()
{
    return new PithyPrint(CONSTS_STAGE_FORWARDER);
}

PithyPrint *PithyPrint::CreateEncoder()
{
    return new PithyPrint(CONSTS_STAGE_ENCODER);
}

PithyPrint *PithyPrint::CreateExec()
{
    return new PithyPrint(CONSTS_STAGE_EXEC);
}

PithyPrint *PithyPrint::CreateIngester()
{
    return new PithyPrint(CONSTS_STAGE_INGESTER);
}

PithyPrint *PithyPrint::CreateEdge()
{
    return new PithyPrint(CONSTS_STAGE_EDGE);
}

PithyPrint *PithyPrint::CreateCaster()
{
    return new PithyPrint(CONSTS_STAGE_CASTER);
}

PithyPrint *PithyPrint::CreateHttpStream()
{
    return new PithyPrint(CONSTS_STAGE_HTTP_STREAM);
}

PithyPrint *PithyPrint::CreateHttpStreamCache()
{
    return new PithyPrint(CONSTS_STAGE_HTTP_STREAM_CACHE);
}

PithyPrint *PithyPrint::CreateRtcPlay()
{
    return new PithyPrint(CONSTS_STAGE_RTC_PLAY);
}

PithyPrint *PithyPrint::CreateRtcSend(int fd)
{
    return new PithyPrint(fd<<16 | CONSTS_STAGE_RTC_SEND);
}

PithyPrint *PithyPrint::CreateRtcRecv(int fd)
{
    return new PithyPrint(fd<<16 | CONSTS_STAGE_RTC_RECV);
}

#ifdef SRS_SRT
PithyPrint* PithyPrint::create_srt_play()
{
    return new PithyPrint(SRS_CONSTS_STAGE_SRT_PLAY);
}

PithyPrint* PithyPrint::create_srt_publish()
{
    return new PithyPrint(SRS_CONSTS_STAGE_SRT_PUBLISH);
}
#endif

PithyPrint::~PithyPrint()
{
    LeaveStage();
}

int PithyPrint::EnterStage()
{
    StageInfo* stage = _stages->FetchOrCreate(m_stageId);
    Assert(stage != NULL);
    m_clientId = stage->m_nbClients++;

    verbose("enter stage, stage_id=%d, client_id=%d, nb_clients=%d",
                stage->m_stageId, m_clientId, stage->m_nbClients);

    return m_clientId;
}

void PithyPrint::LeaveStage()
{
    StageInfo* stage = _stages->FetchOrCreate(m_stageId);
    Assert(stage != NULL);

    stage->m_nbClients--;

    verbose("leave stage, stage_id=%d, client_id=%d, nb_clients=%d",
                stage->m_stageId, m_clientId, stage->m_nbClients);
}

void PithyPrint::Elapse()
{
    StageInfo* stage = m_cache;
    if (!stage) {
        stage = m_cache = _stages->FetchOrCreate(m_stageId);
    }
    Assert(stage != NULL);

    utime_t diff = GetSystemTime() - m_previousTick;
    diff = MAX(0, diff);

    stage->Elapse(diff);
    m_age += diff;
    m_previousTick = GetSystemTime();
}

bool PithyPrint::CanPrint()
{
    StageInfo* stage = m_cache;
    if (!stage) {
        stage = m_cache = _stages->FetchOrCreate(m_stageId);
    }
    Assert(stage != NULL);

    return stage->CanPrint();
}

utime_t PithyPrint::Age()
{
    return m_age;
}

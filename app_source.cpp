#include "app_source.h"
#include "flv.h"
#include "app_config.h"
#include "app_dash.h"
#include "app_statistic.h"
#include "app_forward.h"
#include "app_http_hooks.h"
#include "app_dvr.h"
#include "consts.h"
#include "protocol_amf0.h"
#include "protocol_rtmp_msg_array.h"
#include "protocol_rtmp_stack.h"
#include "protocol_st.h"
#include "utility.h"
#include "flv.h"
#include "codec.h"
#include "buffer.h"
#include "core_autofree.h"
#include "protocol_format.h"
#include "app_encoder.h"
#include <algorithm>
#include <cstring>
#define PERF_MW_MSGS 128

#define CONST_MAX_JITTER_MS         250
#define CONST_MAX_JITTER_MS_NEG         -250
#define DEFAULT_FRAME_TIME_MS         10

// for 26ms per audio packet,
// 115 packets is 3s.
#define PURE_AUDIO_GUESS_COUNT 115

// when got these videos or audios, pure audio or video, mix ok.
#define MIX_CORRECT_PURE_AV 10

// the time to cleanup source.
#define SOURCE_CLEANUP (30 * UTIME_SECONDS)

int TimeJitterString2int(std::string time_jitter)
{
    if (time_jitter == "full") {
        return RtmpJitterAlgorithmFULL;
    } else if (time_jitter == "zero") {
        return RtmpJitterAlgorithmZERO;
    } else {
        return RtmpJitterAlgorithmOFF;
    }
}

RtmpJitter::RtmpJitter()
{
    m_lastPktCorrectTime = -1;
    m_lastPktTime = 0;
}

RtmpJitter::~RtmpJitter()
{

}

error RtmpJitter::Correct(SharedPtrMessage *msg, RtmpJitterAlgorithm ag)
{
    error err = SUCCESS;

    // for performance issue
    if (ag != RtmpJitterAlgorithmFULL) {
        // all jitter correct features is disabled, ignore.
        if (ag == RtmpJitterAlgorithmOFF) {
            return err;
        }

        // start at zero, but donot ensure monotonically increasing.
        if (ag == RtmpJitterAlgorithmZERO) {
            // for the first time, last_pkt_correct_time is -1.
            if (m_lastPktCorrectTime == -1) {
                m_lastPktCorrectTime = msg->m_timestamp;
            }
            msg->m_timestamp -= m_lastPktCorrectTime;
            return err;
        }

        // other algorithm, ignore.
        return err;
    }

    // full jitter algorithm, do jitter correct.
    // set to 0 for metadata.
    if (!msg->IsAv()) {
        msg->m_timestamp = 0;
        return err;
    }

    /**
     * we use a very simple time jitter detect/correct algorithm:
     * 1. delta: ensure the delta is positive and valid,
     *     we set the delta to DEFAULT_FRAME_TIME_MS,
     *     if the delta of time is nagative or greater than CONST_MAX_JITTER_MS.
     * 2. last_pkt_time: specifies the original packet time,
     *     is used to detect next jitter.
     * 3. last_pkt_correct_time: simply add the positive delta,
     *     and enforce the time monotonically.
     */
    int64_t time = msg->m_timestamp;
    int64_t delta = time - m_lastPktTime;

    // if jitter detected, reset the delta.
    if (delta < CONST_MAX_JITTER_MS_NEG || delta > CONST_MAX_JITTER_MS) {
        // use default 10ms to notice the problem of stream.
        // @see https://github.com/ossrs/srs/issues/425
        delta = DEFAULT_FRAME_TIME_MS;
    }

    m_lastPktCorrectTime = MAX(0, m_lastPktCorrectTime + delta);

    msg->m_timestamp = m_lastPktCorrectTime;
    m_lastPktTime = time;

    return err;
}

int64_t RtmpJitter::GetTime()
{
    return m_lastPktCorrectTime;
}

#ifdef PERF_QUEUE_FAST_VECTOR
FastVector::FastVector()
{
    m_count = 0;
    m_nbMsgs = 8;
    m_msgs = new SharedPtrMessage*[m_nbMsgs];
}

FastVector::~FastVector()
{
    Free();
    Freepa(m_msgs);
}

int FastVector::Size()
{
    return m_count;
}

int FastVector::Begin()
{
    return 0;
}

int FastVector::End()
{
    return m_count;
}

SharedPtrMessage **FastVector::Data()
{
    return m_msgs;
}

SharedPtrMessage *FastVector::At(int index)
{
    Assert(index < m_count);
    return m_msgs[index];
}

void FastVector::Clear()
{
    m_count = 0;
}

void FastVector::Erase(int _begin, int _end)
{
    Assert(_begin < _end);

    // move all erased to previous.
    for (int i = 0; i < m_count - _end; i++) {
        m_msgs[_begin + i] = m_msgs[_end + i];
    }

    // update the count.
    m_count -= _end - _begin;
}

void FastVector::PushBack(SharedPtrMessage *msg)
{
    // increase vector.
    if (m_count >= m_nbMsgs) {
        int size = MAX(PERF_MW_MSGS * 8, m_nbMsgs * 2);
        SharedPtrMessage** buf = new SharedPtrMessage*[size];
        for (int i = 0; i < m_nbMsgs; i++) {
            buf[i] = m_msgs[i];
        }
        info("fast vector incrase %d=>%d", m_nbMsgs, size);

        // use new array.
        Freepa(m_msgs);
        m_msgs = buf;
        m_nbMsgs = size;
    }

    m_msgs[m_count++] = msg;
}

void FastVector::Free()
{
    for (int i = 0; i < m_count; i++) {
        SharedPtrMessage* m_msg = m_msgs[i];
        Freep(m_msg);
    }
    m_count = 0;
}
#endif


MessageQueue::MessageQueue(bool ignore_shrink)
{
    m_ignoreShrink = ignore_shrink;
    m_maxQueueSize = 0;
    m_avStartTime = m_avEndTime = -1;
}

MessageQueue::~MessageQueue()
{
    Clear();
}

int MessageQueue::Size()
{
    return (int)m_msgs.Size();
}

utime_t MessageQueue::Duration()
{
    return (m_avEndTime - m_avStartTime);
}

void MessageQueue::SetQueueSize(utime_t queue_size)
{
    m_maxQueueSize = queue_size;
}

error MessageQueue::Enqueue(SharedPtrMessage *msg, bool *is_overflow)
{
    error err = SUCCESS;

    m_msgs.PushBack(msg);

    // If jitter is off, the timestamp of first sequence header is zero, which wll cause SRS to shrink and drop the
    // keyframes even if there is not overflow packets in queue, so we must ignore the zero timestamps, please
    // @see https://github.com/ossrs/srs/pull/2186#issuecomment-953383063
    if (msg->IsAv() && msg->m_timestamp != 0) {
        if (m_avStartTime == -1) {
            m_avStartTime = utime_t(msg->m_timestamp * UTIME_MILLISECONDS);
        }

        m_avEndTime = utime_t(msg->m_timestamp * UTIME_MILLISECONDS);
    }

    if (m_maxQueueSize <= 0) {
        return err;
    }

    while (m_avEndTime - m_avStartTime > m_maxQueueSize) {
        // notice the caller queue already overflow and shrinked.
        if (is_overflow) {
            *is_overflow = true;
        }

        Shrink();
    }

    return err;
}

error MessageQueue::DumpPackets(int max_count, SharedPtrMessage **pmsgs, int &count)
{
    error err = SUCCESS;

    int nb_msgs = (int)m_msgs.Size();
    if (nb_msgs <= 0) {
        return err;
    }

    Assert(max_count > 0);
    count = MIN(max_count, nb_msgs);

    SharedPtrMessage** omsgs = m_msgs.Data();
    memcpy(pmsgs, omsgs, count * sizeof(SharedPtrMessage*));

    SharedPtrMessage* last = omsgs[count - 1];
    m_avStartTime = utime_t(last->m_timestamp * UTIME_MILLISECONDS);

    if (count >= nb_msgs) {
        // the pmsgs is big enough and clear msgs at most time.
        m_msgs.Clear();
    } else {
        // erase some vector elements may cause memory copy,
        // maybe can use more efficient vector.swap to avoid copy.
        // @remark for the pmsgs is big enough, for instance, SRS_PERF_MW_MSGS 128,
        //      the rtmp play client will get 128msgs once, so this branch rarely execute.
        m_msgs.Erase(m_msgs.Begin(), m_msgs.Begin() + count);
    }

    return err;
}

error MessageQueue::DumpPackets(LiveConsumer *consumer, bool atc, RtmpJitterAlgorithm ag)
{
    error err = SUCCESS;

    int nb_msgs = (int)m_msgs.Size();
    if (nb_msgs <= 0) {
        return err;
    }

    SharedPtrMessage** omsgs = m_msgs.Data();
    for (int i = 0; i < nb_msgs; i++) {
        SharedPtrMessage* msg = omsgs[i];
        if ((err = consumer->Enqueue(msg, atc, ag)) != SUCCESS) {
            return ERRORWRAP(err, "consume message");
        }
    }

    return err;
}

void MessageQueue::Shrink()
{
    SharedPtrMessage* video_sh = NULL;
    SharedPtrMessage* audio_sh = NULL;
    int msgs_size = (int)m_msgs.Size();

    // Remove all msgs, mark the sequence headers.
    for (int i = 0; i < (int)m_msgs.Size(); i++) {
        SharedPtrMessage* msg = m_msgs.At(i);

        if (msg->IsVideo() && FlvVideo::Sh(msg->m_payload, msg->m_size)) {
            Freep(video_sh);
            video_sh = msg;
            continue;
        }
        else if (msg->IsAudio() && FlvAudio::Sh(msg->m_payload, msg->m_size)) {
            Freep(audio_sh);
            audio_sh = msg;
            continue;
        }

        Freep(msg);
    }
    m_msgs.Clear();

    // Update av_start_time, the start time of queue.
    m_avStartTime = m_avEndTime;

    // Push back sequence headers and update their timestamps.
    if (video_sh) {
        video_sh->m_timestamp = u2ms(m_avEndTime);
        m_msgs.PushBack(video_sh);
    }
    if (audio_sh) {
        audio_sh->m_timestamp = u2ms(m_avEndTime);
        m_msgs.PushBack(audio_sh);
    }

    if (!m_ignoreShrink) {
        trace("shrinking, size=%d, removed=%d, max=%dms", (int)m_msgs.Size(), msgs_size - (int)m_msgs.Size(), u2msi(m_maxQueueSize));
    }
}

void MessageQueue::Clear()
{
#ifndef PERF_QUEUE_FAST_VECTOR
    std::vector<SharedPtrMessage*>::iterator it;

    for (it = m_msgs.begin(); it != m_msgs.end(); ++it) {
        SharedPtrMessage* msg = *it;
        Freep(msg);
    }
#else
    m_msgs.Free();
#endif

    m_msgs.Clear();

    m_avStartTime = m_avEndTime = -1;
}


IWakable::IWakable()
{

}

IWakable::~IWakable()
{

}



LiveConsumer::LiveConsumer(LiveSource *s)
{
    m_source = s;
    m_paused = false;
    m_jitter = new RtmpJitter();
    m_queue = new MessageQueue();
    m_shouldUpdateSourceId = false;

#ifdef PERF_QUEUE_COND_WAIT
    m_mwWait = st_cond_new();
    m_mwMinMsgs = 0;
    m_mwDuration = 0;
    m_mwWaiting = false;
#endif
}

LiveConsumer::~LiveConsumer()
{
    m_source->OnConsumerDestroy(this);
    Freep(m_jitter);
    Freep(m_queue);

#ifdef PERF_QUEUE_COND_WAIT
    st_cond_destroy(m_mwWait);
#endif
}

void LiveConsumer::SetQueueSize(utime_t queue_size)
{
    m_queue->SetQueueSize(queue_size);
}

void LiveConsumer::UpdateSourceId()
{
    m_shouldUpdateSourceId = true;
}

int64_t LiveConsumer::GetTime()
{
    return m_jitter->GetTime();
}

error LiveConsumer::Enqueue(SharedPtrMessage *shared_msg, bool atc, RtmpJitterAlgorithm ag)
{
    error err = SUCCESS;

    SharedPtrMessage* msg = shared_msg->Copy();

    if (!atc) {
        if ((err = m_jitter->Correct(msg, ag)) != SUCCESS) {
            return ERRORWRAP(err, "consume message");
        }
    }

    if ((err = m_queue->Enqueue(msg, NULL)) != SUCCESS) {
        return ERRORWRAP(err, "enqueue message");
    }

#ifdef PERF_QUEUE_COND_WAIT
    // fire the mw when msgs is enough.
    if (m_mwWaiting) {
        // For RTMP, we wait for messages and duration.
        utime_t duration = m_queue->Duration();
        bool match_min_msgs = m_queue->Size() > m_mwMinMsgs;

        // For ATC, maybe the SH timestamp bigger than A/V packet,
        // when encoder republish or overflow.
        // @see https://github.com/ossrs/srs/pull/749
        if (atc && duration < 0) {
            st_cond_signal(m_mwWait);
            m_mwWaiting = false;
            return err;
        }

        // when duration ok, signal to flush.
        if (match_min_msgs && duration > m_mwDuration) {
            st_cond_signal(m_mwWait);
            m_mwWaiting = false;
            return err;
        }
    }
#endif

    return err;
}

error LiveConsumer::DumpPackets(MessageArray *msgs, int &count)
{
    error err = SUCCESS;

    Assert(count >= 0);
    Assert(msgs->m_max > 0);

    // the count used as input to reset the max if positive.
    int max = count? MIN(count, msgs->m_max) : msgs->m_max;

    // the count specifies the max acceptable count,
    // here maybe 1+, and we must set to 0 when got nothing.
    count = 0;

    if (m_shouldUpdateSourceId) {
        trace("update source_id=%s/%s", m_source->SourceId().Cstr(), m_source->PreSourceId().Cstr());
        m_shouldUpdateSourceId = false;
    }

    // paused, return nothing.
    if (m_paused) {
        return err;
    }

    // pump msgs from queue.
    if ((err = m_queue->DumpPackets(max, msgs->m_msgs, count)) != SUCCESS) {
        return ERRORWRAP(err, "dump packets");
    }

    return err;
}

#ifdef PERF_QUEUE_COND_WAIT
void LiveConsumer::Wait(int nb_msgs, utime_t msgs_duration)
{
    if (m_paused) {
        st_usleep(CONSTS_RTMP_PULSE);
        return;
    }

    m_mwMinMsgs = nb_msgs;
    m_mwDuration = msgs_duration;

    utime_t duration = m_queue->Duration();
    bool match_min_msgs = m_queue->Size() > m_mwMinMsgs;

    // when duration ok, signal to flush.
    if (match_min_msgs && duration > m_mwDuration) {
        return;
    }

    // the enqueue will notify this cond.
    m_mwWaiting = true;

    // use cond block wait for high performance mode.
    st_cond_wait(m_mwWait);
}
#endif

error LiveConsumer::OnPlayClientPause(bool is_pause)
{
    error err = SUCCESS;

    trace("stream consumer change pause state %d=>%d", m_paused, is_pause);
    m_paused = is_pause;

    return err;
}

void LiveConsumer::Wakeup()
{
#ifdef PERF_QUEUE_COND_WAIT
    if (m_mwWaiting) {
        st_cond_signal(m_mwWait);
        m_mwWaiting = false;
    }
#endif
}


GopCache::GopCache()
{
    m_cachedVideoCount = 0;
    m_enableGopCache = true;
    m_audioAfterLastVideoCount = 0;
}

GopCache::~GopCache()
{
    Clear();
}

void GopCache::Dispose()
{
    Clear();
}

void GopCache::Set(bool v)
{
    m_enableGopCache = v;

    if (!v) {
        Clear();
        return;
    }
}

bool GopCache::Enabled()
{
    return m_enableGopCache;
}

error GopCache::Cache(SharedPtrMessage *shared_msg)
{
    error err = SUCCESS;

    if (!m_enableGopCache) {
        return err;
    }

    // the gop cache know when to gop it.
    SharedPtrMessage* msg = shared_msg;

    // got video, update the video count if acceptable
    if (msg->IsVideo()) {
        // drop video when not h.264
        if (!FlvVideo::H264(msg->m_payload, msg->m_size)) {
            return err;
        }

        m_cachedVideoCount++;
        m_audioAfterLastVideoCount = 0;
    }

    // no acceptable video or pure audio, disable the cache.
    if (PureAudio()) {
        return err;
    }

    // ok, gop cache enabled, and got an audio.
    if (msg->IsAudio()) {
        m_audioAfterLastVideoCount++;
    }

    // clear gop cache when pure audio count overflow
    if (m_audioAfterLastVideoCount > PURE_AUDIO_GUESS_COUNT) {
        warn("clear gop cache for guess pure audio overflow");
        Clear();
        return err;
    }

    // clear gop cache when got key frame
    if (msg->IsVideo() && FlvVideo::Keyframe(msg->m_payload, msg->m_size)) {
        Clear();

        // curent msg is video frame, so we set to 1.
        m_cachedVideoCount = 1;
    }

    // cache the frame.
    m_gopCache.push_back(msg->Copy());

    return err;
}

void GopCache::Clear()
{
    std::vector<SharedPtrMessage*>::iterator it;
    for (it = m_gopCache.begin(); it != m_gopCache.end(); ++it) {
        SharedPtrMessage* msg = *it;
        Freep(msg);
    }
    m_gopCache.clear();

    m_cachedVideoCount = 0;
    m_audioAfterLastVideoCount = 0;
}

error GopCache::Dump(LiveConsumer *consumer, bool atc, RtmpJitterAlgorithm jitter_algorithm)
{
    error err = SUCCESS;

    std::vector<SharedPtrMessage*>::iterator it;
    for (it = m_gopCache.begin(); it != m_gopCache.end(); ++it) {
        SharedPtrMessage* msg = *it;
        if ((err = consumer->Enqueue(msg, atc, jitter_algorithm)) != SUCCESS) {
            return ERRORWRAP(err, "enqueue message");
        }
    }
    trace("dispatch cached gop success. count=%d, duration=%d", (int)m_gopCache.size(), consumer->GetTime());

    return err;
}

bool GopCache::Empty()
{
    return m_gopCache.empty();
}

utime_t GopCache::StartTime()
{
    if (Empty()) {
        return 0;
    }

    SharedPtrMessage* msg = m_gopCache[0];
    Assert(msg);

    return utime_t(msg->m_timestamp * UTIME_MILLISECONDS);
}

bool GopCache::PureAudio()
{
    return m_cachedVideoCount == 0;
}

ILiveSourceHandler::ILiveSourceHandler()
{

}

ILiveSourceHandler::~ILiveSourceHandler()
{

}

// TODO: FIXME: Remove it?
bool HlsCanContinue(int ret, SharedPtrMessage* sh, SharedPtrMessage* msg)
{
    // only continue for decode error.
    if (ret != ERROR_HLS_DECODE_ERROR) {
        return false;
    }

    // when video size equals to sequence header,
    // the video actually maybe a sequence header,
    // continue to make ffmpeg happy.
    if (sh && sh->m_size == msg->m_size) {
        warn("the msg is actually a sequence header, ignore this packet.");
        return true;
    }

    return false;
}

MixQueue::MixQueue()
{
    m_nbVideos = 0;
    m_nbAudios = 0;
}

MixQueue::~MixQueue()
{
    Clear();
}

void MixQueue::Clear()
{
    std::multimap<int64_t, SharedPtrMessage*>::iterator it;
    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SharedPtrMessage* msg = it->second;
        Freep(msg);
    }
    msgs.clear();

    m_nbVideos = 0;
    m_nbAudios = 0;
}

void MixQueue::Push(SharedPtrMessage *msg)
{
    msgs.insert(std::make_pair(msg->m_timestamp, msg));

    if (msg->IsVideo()) {
        m_nbVideos++;
    } else {
        m_nbAudios++;
    }
}

SharedPtrMessage *MixQueue::Pop()
{
    bool mix_ok = false;

    // pure video
    if (m_nbVideos >= MIX_CORRECT_PURE_AV && m_nbAudios == 0) {
        mix_ok = true;
    }

    // pure audio
    if (m_nbAudios >= MIX_CORRECT_PURE_AV && m_nbVideos == 0) {
        mix_ok = true;
    }

    // got 1 video and 1 audio, mix ok.
    if (m_nbVideos >= 1 && m_nbAudios >= 1) {
        mix_ok = true;
    }

    if (!mix_ok) {
        return NULL;
    }

    // pop the first msg.
    std::multimap<int64_t, SharedPtrMessage*>::iterator it = msgs.begin();
    SharedPtrMessage* msg = it->second;
    msgs.erase(it);

    if (msg->IsVideo()) {
        m_nbVideos--;
    } else {
        m_nbAudios--;
    }

    return msg;
}

OriginHub::OriginHub()
{
    m_source = NULL;
    m_req = NULL;
    m_isActive = false;

//    m_hls = new Hls();
    m_dash = new Dash();
    m_dvr = new Dvr();
    m_encoder = new Encoder();
#ifdef SRS_HDS
    hds = new SrsHds();
#endif
//    m_ngExec = new NgExec();
    m_format = new RtmpFormat();

    config->Subscribe(this);
}

OriginHub::~OriginHub()
{
    config->Unsubscribe(this);

    if (true) {
        std::vector<Forwarder*>::iterator it;
        for (it = m_forwarders.begin(); it != m_forwarders.end(); ++it) {
            Forwarder* forwarder = *it;
            Freep(forwarder);
        }
        m_forwarders.clear();
    }
//    Freep(m_ngExec);

    Freep(m_format);
//    Freep(m_hls);
    Freep(m_dash);
    Freep(m_dvr);
    Freep(m_encoder);
#ifdef SRS_HDS
    Freep(hds);
#endif
}

error OriginHub::Initialize(LiveSource *s, Request *r)
{
    error err = SUCCESS;

    m_req = r;
    m_source = s;

    if ((err = m_format->Initialize()) != SUCCESS) {
        return ERRORWRAP(err, "format initialize");
    }

    // Setup the SPS/PPS parsing strategy.
    m_format->m_tryAnnexbFirst = config->TryAnnexbFirst(r->m_vhost);

//    if ((err = m_hls->initialize(this, m_req)) != SUCCESS) {
//        return ERRORWRAP(err, "hls initialize");
//    }

    if ((err = m_dash->Initialize(this, m_req)) != SUCCESS) {
        return ERRORWRAP(err, "dash initialize");
    }

    if ((err = m_dvr->Initialize(this, m_req)) != SUCCESS) {
        return ERRORWRAP(err, "dvr initialize");
    }

    return err;
}

void OriginHub::Dispose()
{
//    hls->dispose();
}

error OriginHub::Cycle()
{
    error err = SUCCESS;

//    if ((err = hls->cycle()) != SUCCESS) {
//        return ERRORWRAP(err, "hls cycle");
//    }

    // TODO: Support cycle DASH.

    return err;
}

bool OriginHub::Active()
{
    return m_isActive;
}

error OriginHub::OnMetaData(SharedPtrMessage *shared_metadata, OnMetaDataPacket *packet)
{
    error err = SUCCESS;

    if ((err = m_format->OnMetadata(packet)) != SUCCESS) {
        return ERRORWRAP(err, "Format parse metadata");
    }

    // copy to all forwarders
    if (true) {
        std::vector<Forwarder*>::iterator it;
        for (it = m_forwarders.begin(); it != m_forwarders.end(); ++it) {
            Forwarder* forwarder = *it;
            if ((err = forwarder->OnMetaData(shared_metadata)) != SUCCESS) {
                return ERRORWRAP(err, "Forwarder consume metadata");
            }
        }
    }

    if ((err = m_dvr->OnMetaData(shared_metadata)) != SUCCESS) {
        return ERRORWRAP(err, "DVR consume metadata");
    }

    return err;
}

error OriginHub::OnAudio(SharedPtrMessage *shared_audio)
{
    error err = SUCCESS;

    SharedPtrMessage* msg = shared_audio;

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = m_format->OnAudio(msg)) != SUCCESS) {
        return ERRORWRAP(err, "format consume audio");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as G.711 codec
    if (!m_format->m_acodec) {
        return err;
    }

    // cache the sequence header if aac
    // donot cache the sequence header to gop_cache, return here.
    if (m_format->IsAacSequenceHeader()) {
        Assert(m_format->m_acodec);
        AudioCodecConfig* c = m_format->m_acodec;

        static int flv_sample_sizes[] = {8, 16, 0};
        static int flv_sound_types[] = {1, 2, 0};

        // when got audio stream info.
        Statistic* stat = Statistic::Instance();
        if ((err = stat->OnAudioInfo(m_req, AudioCodecIdAAC, c->m_soundRate, c->m_soundType, c->m_aacObject)) != SUCCESS) {
            return ERRORWRAP(err, "stat audio");
        }

        trace("%dB audio sh, codec(%d, profile=%s, %dchannels, %dkbps, %dHZ), flv(%dbits, %dchannels, %dHZ)",
                  msg->m_size, c->m_id, AacObject2str(c->m_aacObject).c_str(), c->m_aacChannels,
                  c->m_audioDataRate / 1000, aac_srates[c->m_aacSampleRate],
                  flv_sample_sizes[c->m_soundSize], flv_sound_types[c->m_soundType],
                  flv_srates[c->m_soundRate]);
    }

//    if ((err = m_hls->on_audio(msg, format)) != SUCCESS) {
//        // apply the error strategy for hls.
//        // @see https://github.com/ossrs/srs/issues/264
//        std::string hls_error_strategy = config->get_hls_on_error(req_->vhost);
//        if (srs_config_hls_is_on_error_ignore(hls_error_strategy)) {
//            warn("hls: ignore audio error %s", ERRORDESC(err).c_str());
//            hls->on_unpublish();
//            ERRORRESET(err);
//        } else if (srs_config_hls_is_on_error_continue(hls_error_strategy)) {
//            if (srs_hls_can_continue(ERRORCODE(err), source->meta->ash(), msg)) {
//                ERRORRESET(err);
//            } else {
//                return ERRORWRAP(err, "hls: audio");
//            }
//        } else {
//            return ERRORWRAP(err, "hls: audio");
//        }
//    }

    if ((err = m_dash->OnAudio(msg, m_format)) != SUCCESS) {
        warn("dash: ignore audio error %s", ERRORDESC(err).c_str());
        ERRORRESET(err);
        m_dash->OnUnpublish();
    }

    if ((err = m_dvr->OnAudio(msg, m_format)) != SUCCESS) {
        warn("dvr: ignore audio error %s", ERRORDESC(err).c_str());
        ERRORRESET(err);
        m_dvr->OnUnpublish();
    }

#ifdef SRS_HDS
    if ((err = hds->on_audio(msg)) != SUCCESS) {
        warn("hds: ignore audio error %s", ERRORDESC(err).c_str());
        ERRORRESET(err);
        hds->on_unpublish();
    }
#endif

    // copy to all forwarders.
    if (true) {
        std::vector<Forwarder*>::iterator it;
        for (it = m_forwarders.begin(); it != m_forwarders.end(); ++it) {
            Forwarder* forwarder = *it;
            if ((err = forwarder->OnAudio(msg)) != SUCCESS) {
                return ERRORWRAP(err, "forward: audio");
            }
        }
    }

    return err;
}

error OriginHub::OnVideo(SharedPtrMessage *shared_video, bool is_sequence_header)
{
    error err = SUCCESS;

    SharedPtrMessage* msg = shared_video;

    // user can disable the sps parse to workaround when parse sps failed.
    // @see https://github.com/ossrs/srs/issues/474
    if (is_sequence_header) {
        m_format->m_avcParseSps = config->GetParseSps(m_req->m_vhost);
    }

    if ((err = m_format->OnVideo(msg)) != SUCCESS) {
        return ERRORWRAP(err, "format consume video");
    }

    // Ignore if no format->vcodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as H.263 codec
    if (!m_format->m_vcodec) {
        return err;
    }

    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (m_format->IsAvcSequenceHeader()) {
        VideoCodecConfig* c = m_format->m_vcodec;
        Assert(c);

        // when got video stream info.
        Statistic* stat = Statistic::Instance();
        if ((err = stat->OnVideoInfo(m_req, VideoCodecIdAVC, c->m_avcProfile, c->m_avcLevel, c->m_width, c->m_height)) != SUCCESS) {
            return ERRORWRAP(err, "stat video");
        }

        trace("%dB video sh,  codec(%d, profile=%s, level=%s, %dx%d, %dkbps, %.1ffps, %.1fs)",
                  msg->m_size, c->m_id, AvcProfile2str(c->m_avcProfile).c_str(),
                  AvcLevel2str(c->m_avcLevel).c_str(), c->m_width, c->m_height,
                  c->m_videoDataRate / 1000, c->m_frameRate, c->m_duration);
    }

    // Ignore video data when no sps/pps
    // @bug https://github.com/ossrs/srs/issues/703#issuecomment-578393155
    if (m_format->m_vcodec && !m_format->m_vcodec->IsAvcCodecOk()) {
        return err;
    }

//    if ((err = m_hls->on_video(msg, m_format)) != SUCCESS) {
//        // TODO: We should support more strategies.
//        // apply the error strategy for hls.
//        // @see https://github.com/ossrs/srs/issues/264
//        std::string hls_error_strategy = config->GetHlsOnError(m_req->m_vhost);
//        if (ConfigHlsIsOnErrorIgnore(hls_error_strategy)) {
//            warn("hls: ignore video error %s", ERRORDESC(err).c_str());
//            m_hls->on_unpublish();
//            ERRORRESET(err);
//        } else if (ConfigHlsIsOnErrorContinue(hls_error_strategy)) {
//            if (HlsCanContinue(ERRORCODE(err), m_source->m_meta->Vsh(), msg)) {
//                ERRORRESET(err);
//            } else {
//                return ERRORWRAP(err, "hls: video");
//            }
//        } else {
//            return ERRORWRAP(err, "hls: video");
//        }
//    }

    if ((err = m_dash->OnVideo(msg, m_format)) != SUCCESS) {
        warn("dash: ignore video error %s", ERRORDESC(err).c_str());
        ERRORRESET(err);
        m_dash->OnUnpublish();
    }

    if ((err = m_dvr->OnVideo(msg, m_format)) != SUCCESS) {
        warn("dvr: ignore video error %s", ERRORDESC(err).c_str());
        ERRORRESET(err);
        m_dvr->OnUnpublish();
    }

#ifdef SRS_HDS
    if ((err = hds->on_video(msg)) != SUCCESS) {
        warn("hds: ignore video error %s", ERRORDESC(err).c_str());
        ERRORRESET(err);
        hds->on_unpublish();
    }
#endif

    // copy to all forwarders.
    if (!m_forwarders.empty()) {
        std::vector<Forwarder*>::iterator it;
        for (it = m_forwarders.begin(); it != m_forwarders.end(); ++it) {
            Forwarder* forwarder = *it;
            if ((err = forwarder->OnVideo(msg)) != SUCCESS) {
                return ERRORWRAP(err, "forward video");
            }
        }
    }

    return err;
}

error OriginHub::OnPublish()
{
    error err = SUCCESS;

    // create forwarders
    if ((err = CreateForwarders()) != SUCCESS) {
        return ERRORWRAP(err, "create forwarders");
    }

    // TODO: FIXME: use initialize to set req.
    if ((err = m_encoder->OnPublish(m_req)) != SUCCESS) {
        return ERRORWRAP(err, "encoder publish");
    }

//    if ((err = hls->on_publish()) != SUCCESS) {
//        return ERRORWRAP(err, "hls publish");
//    }

    if ((err = m_dash->OnPublish()) != SUCCESS) {
        return ERRORWRAP(err, "dash publish");
    }

    // @see https://github.com/ossrs/srs/issues/1613#issuecomment-961657927
    if ((err = m_dvr->OnPublish(m_req)) != SUCCESS) {
        return ERRORWRAP(err, "dvr publish");
    }

    // TODO: FIXME: use initialize to set req.
#ifdef SRS_HDS
    if ((err = hds->on_publish(req_)) != SUCCESS) {
        return ERRORWRAP(err, "hds publish");
    }
#endif

//    // TODO: FIXME: use initialize to set req.
//    if ((err = m_ngExec->on_publish(m_req)) != SUCCESS) {
//        return ERRORWRAP(err, "exec publish");
//    }

    m_isActive = true;

    return err;
}

void OriginHub::OnUnpublish()
{
    m_isActive = false;

    // destroy all forwarders
    DestroyForwarders();

    m_encoder->OnUnpublish();
//    m_hls->OnUnpublish();
    m_dash->OnUnpublish();
    m_dvr->OnUnpublish();

#ifdef SRS_HDS
    hds->on_unpublish();
#endif

//    m_ngExec->on_unpublish();
}

error OriginHub::OnForwarderStart(Forwarder *forwarder)
{
    error err = SUCCESS;

    SharedPtrMessage* cache_metadata = m_source->m_meta->Data();
    SharedPtrMessage* cache_sh_video = m_source->m_meta->Vsh();
    SharedPtrMessage* cache_sh_audio = m_source->m_meta->Ash();

    // feed the forwarder the metadata/sequence header,
    // when reload to enable the forwarder.
    if (cache_metadata && (err = forwarder->OnMetaData(cache_metadata)) != SUCCESS) {
        return ERRORWRAP(err, "forward metadata");
    }
    if (cache_sh_video && (err = forwarder->OnVideo(cache_sh_video)) != SUCCESS) {
        return ERRORWRAP(err, "forward video sh");
    }
    if (cache_sh_audio && (err = forwarder->OnAudio(cache_sh_audio)) != SUCCESS) {
        return ERRORWRAP(err, "forward audio sh");
    }

    return err;
}

error OriginHub::OnDvrRequestSh()
{
    error err = SUCCESS;

    SharedPtrMessage* cache_metadata = m_source->m_meta->Data();
    SharedPtrMessage* cache_sh_video = m_source->m_meta->Vsh();
    SharedPtrMessage* cache_sh_audio = m_source->m_meta->Ash();

    // feed the dvr the metadata/sequence header,
    // when reload to start dvr, dvr will never get the sequence header in stream,
    // use the SrsLiveSource.on_dvr_request_sh to push the sequence header to DVR.
    if (cache_metadata && (err = m_dvr->OnMetaData(cache_metadata)) != SUCCESS) {
        return ERRORWRAP(err, "dvr metadata");
    }

    if (cache_sh_video) {
        if ((err = m_dvr->OnVideo(cache_sh_video, m_source->m_meta->VshFormat())) != SUCCESS) {
            return ERRORWRAP(err, "dvr video");
        }
    }

    if (cache_sh_audio) {
        if ((err = m_dvr->OnAudio(cache_sh_audio, m_source->m_meta->AshFormat())) != SUCCESS) {
            return ERRORWRAP(err, "dvr audio");
        }
    }

    return err;
}

error OriginHub::OnReloadVhostForward(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // TODO: FIXME: maybe should ignore when publish already stopped?

    // forwarders
    DestroyForwarders();

    // Don't start forwarders when source is not active.
    if (!m_isActive) {
        return err;
    }

    if ((err = CreateForwarders()) != SUCCESS) {
        return ERRORWRAP(err, "create forwarders");
    }

    trace("vhost %s forwarders reload success", vhost.c_str());

    return err;
}

error OriginHub::OnReloadVhostDash(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    m_dash->OnUnpublish();

    // Don't start DASH when source is not active.
    if (!m_isActive) {
        return err;
    }

    if ((err = m_dash->OnPublish()) != SUCCESS) {
        return ERRORWRAP(err, "dash start publish");
    }

    SharedPtrMessage* cache_sh_video = m_source->m_meta->Vsh();
    if (cache_sh_video) {
        if ((err = m_format->OnVideo(cache_sh_video)) != SUCCESS) {
            return ERRORWRAP(err, "format on_video");
        }
        if ((err = m_dash->OnVideo(cache_sh_video, m_format)) != SUCCESS) {
            return ERRORWRAP(err, "dash on_video");
        }
    }

    SharedPtrMessage* cache_sh_audio = m_source->m_meta->Ash();
    if (cache_sh_audio) {
        if ((err = m_format->OnAudio(cache_sh_audio)) != SUCCESS) {
            return ERRORWRAP(err, "format on_audio");
        }
        if ((err = m_dash->OnAudio(cache_sh_audio, m_format)) != SUCCESS) {
            return ERRORWRAP(err, "dash on_audio");
        }
    }

    return err;
}

error OriginHub::OnReloadVhostHls(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // TODO: FIXME: maybe should ignore when publish already stopped?

//    m_hls->OnUnpublish();

    // Don't start HLS when source is not active.
    if (!m_isActive) {
        return err;
    }

//    if ((err = m_hls->OnPublish()) != SUCCESS) {
//        return ERRORWRAP(err, "hls publish failed");
//    }
    trace("vhost %s hls reload success", vhost.c_str());

    // when publish, don't need to fetch sequence header, which is old and maybe corrupt.
    // when reload, we must fetch the sequence header from source cache.
    // notice the source to get the cached sequence header.
    // when reload to start hls, hls will never get the sequence header in stream,
    // use the SrsLiveSource.on_hls_start to push the sequence header to HLS.
    SharedPtrMessage* cache_sh_video = m_source->m_meta->Vsh();
    if (cache_sh_video) {
        if ((err = m_format->OnVideo(cache_sh_video)) != SUCCESS) {
            return ERRORWRAP(err, "format on_video");
        }
//        if ((err = m_hls->OnVideo(cache_sh_video, m_format)) != SUCCESS) {
//            return ERRORWRAP(err, "hls on_video");
//        }
    }

    SharedPtrMessage* cache_sh_audio = m_source->m_meta->Ash();
    if (cache_sh_audio) {
        if ((err = m_format->OnAudio(cache_sh_audio)) != SUCCESS) {
            return ERRORWRAP(err, "format on_audio");
        }
//        if ((err = m_hls->OnAudio(cache_sh_audio, m_format)) != SUCCESS) {
//            return ERRORWRAP(err, "hls on_audio");
//        }
    }

    return err;
}

error OriginHub::OnReloadVhostHds(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // TODO: FIXME: maybe should ignore when publish already stopped?

#ifdef SRS_HDS
    hds->on_unpublish();

    // Don't start HDS when source is not active.
    if (!is_active) {
        return err;
    }

    if ((err = hds->on_publish(req_)) != SUCCESS) {
        return ERRORWRAP(err, "hds publish failed");
    }
    srs_trace("vhost %s hds reload success", vhost.c_str());
#endif

    return err;
}

error OriginHub::OnReloadVhostDvr(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // TODO: FIXME: maybe should ignore when publish already stopped?

    // cleanup dvr
    m_dvr->OnUnpublish();

    // Don't start DVR when source is not active.
    if (!m_isActive) {
        return err;
    }

    // reinitialize the dvr, update plan.
    if ((err = m_dvr->Initialize(this, m_req)) != SUCCESS) {
        return ERRORWRAP(err, "reload dvr");
    }

    // start to publish by new plan.
    if ((err = m_dvr->OnPublish(m_req)) != SUCCESS) {
        return ERRORWRAP(err, "dvr publish failed");
    }

    if ((err = OnDvrRequestSh()) != SUCCESS) {
        return ERRORWRAP(err, "request sh");
    }

    trace("vhost %s dvr reload success", vhost.c_str());

    return err;
}

error OriginHub::OnReloadVhostTranscode(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // TODO: FIXME: maybe should ignore when publish already stopped?

    m_encoder->OnUnpublish();

    // Don't start transcode when source is not active.
    if (!m_isActive) {
        return err;
    }

    if ((err = m_encoder->OnPublish(m_req)) != SUCCESS) {
        return ERRORWRAP(err, "start encoder failed");
    }
    trace("vhost %s transcode reload success", vhost.c_str());

    return err;
}

error OriginHub::OnReloadVhostExec(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // TODO: FIXME: maybe should ignore when publish already stopped?

//    m_ngExec->on_unpublish();

    // Don't start exec when source is not active.
    if (!m_isActive) {
        return err;
    }

//    if ((err = m_ngExec->on_publish(m_req)) != SUCCESS) {
//        return ERRORWRAP(err, "start exec failed");
//    }
    trace("vhost %s exec reload success", vhost.c_str());

    return err;
}

error OriginHub::CreateForwarders()
{
    error err = SUCCESS;

    if (!config->GetForwardEnabled(m_req->m_vhost)) {
        return err;
    }

    // For backend config
    // If backend is enabled and applied, ignore destination.
    bool applied_backend_server = false;
    if ((err = CreateBackendForwarders(applied_backend_server)) != SUCCESS) {
        return ERRORWRAP(err, "create backend applied=%d", applied_backend_server);
    }

    // Already applied backend server, ignore destination.
    if (applied_backend_server) {
        return err;
    }

    // For destanition config
    ConfDirective* conf = config->GetForwards(m_req->m_vhost);
    for (int i = 0; conf && i < (int)conf->m_args.size(); i++) {
        std::string forward_server = conf->m_args.at(i);

        Forwarder* forwarder = new Forwarder(this);
        m_forwarders.push_back(forwarder);

        // initialize the forwarder with request.
        if ((err = forwarder->Initialize(m_req, forward_server)) != SUCCESS) {
            return ERRORWRAP(err, "init forwarder");
        }

        utime_t queue_size = config->GetQueueLength(m_req->m_vhost);
        forwarder->SetQueueSize(queue_size);

        if ((err = forwarder->OnPublish()) != SUCCESS) {
            return ERRORWRAP(err, "start forwarder failed, vhost=%s, app=%s, stream=%s, forward-to=%s",
                m_req->m_vhost.c_str(), m_req->m_app.c_str(), m_req->m_stream.c_str(), forward_server.c_str());
        }
    }

    return err;
}

error OriginHub::CreateBackendForwarders(bool &applied)
{
    error err = SUCCESS;

    // default not configure backend service
    applied = false;

    ConfDirective* conf = config->GetForwardBackend(m_req->m_vhost);
    if (!conf || conf->Arg0().empty()) {
        return err;
    }

    // configure backend service
    applied = true;

    // only get first backend url
    std::string backend_url = conf->Arg0();

    // get urls on forward backend
    std::vector<std::string> urls;
    if ((err = HttpHooks::OnForwardBackend(backend_url, m_req, urls)) != SUCCESS) {
        return ERRORWRAP(err, "get forward backend failed, backend=%s", backend_url.c_str());
    }

    // create forwarders by urls
    std::vector<std::string>::iterator it;
    for (it = urls.begin(); it != urls.end(); ++it) {
        std::string url = *it;

        // create temp Request by url
        Request* req = new Request();
        AutoFree(Request, req);
        ParseRtmpUrl(url, req->m_tcUrl, req->m_stream);
        DiscoveryTcUrl(req->m_tcUrl, req->m_schema, req->m_host, req->m_vhost, req->m_app, req->m_stream, req->m_port, req->m_param);

        // create forwarder
        Forwarder* forwarder = new Forwarder(this);
        m_forwarders.push_back(forwarder);

        std::stringstream forward_server;
        forward_server << req->m_host << ":" << req->m_port;

        // initialize the forwarder with request.
        if ((err = forwarder->Initialize(req, forward_server.str())) != SUCCESS) {
            return ERRORWRAP(err, "init backend forwarder failed, forward-to=%s", forward_server.str().c_str());
        }

        utime_t queue_size = config->GetQueueLength(m_req->m_vhost);
        forwarder->SetQueueSize(queue_size);

        if ((err = forwarder->OnPublish()) != SUCCESS) {
            return ERRORWRAP(err, "start backend forwarder failed, vhost=%s, app=%s, stream=%s, forward-to=%s",
                m_req->m_vhost.c_str(), m_req->m_app.c_str(), m_req->m_stream.c_str(), forward_server.str().c_str());
        }
    }

    return err;
}

void OriginHub::DestroyForwarders()
{
    std::vector<Forwarder*>::iterator it;
    for (it = m_forwarders.begin(); it != m_forwarders.end(); ++it) {
        Forwarder* forwarder = *it;
        forwarder->OnUnpublish();
        Freep(forwarder);
    }
    m_forwarders.clear();
}

MetaCache::MetaCache()
{
    m_meta = m_video = m_audio = NULL;
    m_previousVideo = m_previousAudio = NULL;
    m_vformat = new RtmpFormat();
    m_aformat = new RtmpFormat();
}

MetaCache::~MetaCache()
{
    Dispose();
    Freep(m_vformat);
    Freep(m_aformat);
}

void MetaCache::Dispose()
{
    Clear();
    Freep(m_previousVideo);
    Freep(m_previousAudio);
}

void MetaCache::Clear()
{
    Freep(m_meta);
    Freep(m_video);
    Freep(m_audio);
}

SharedPtrMessage *MetaCache::Data()
{
    return m_meta;
}

SharedPtrMessage *MetaCache::Vsh()
{
    return m_video;
}

Format *MetaCache::VshFormat()
{
    return m_vformat;
}

SharedPtrMessage *MetaCache::Ash()
{
    return m_audio;
}

Format *MetaCache::AshFormat()
{
    return m_aformat;
}

error MetaCache::Dumps(LiveConsumer *consumer, bool atc, RtmpJitterAlgorithm ag, bool dm, bool ds)
{
    error err = SUCCESS;

    // copy metadata.
    if (dm && m_meta && (err = consumer->Enqueue(m_meta, atc, ag)) != SUCCESS) {
        return ERRORWRAP(err, "enqueue metadata");
    }

    // copy sequence header
    // copy audio sequence first, for hls to fast parse the "right" audio codec.
    // @see https://github.com/ossrs/srs/issues/301
    if (ds && m_audio && (err = consumer->Enqueue(m_audio, atc, ag)) != SUCCESS) {
        return ERRORWRAP(err, "enqueue audio sh");
    }

    if (ds && m_video && (err = consumer->Enqueue(m_video, atc, ag)) != SUCCESS) {
        return ERRORWRAP(err, "enqueue video sh");
    }

    return err;
}

SharedPtrMessage *MetaCache::PreviousVsh()
{
    return m_previousVideo;
}

SharedPtrMessage *MetaCache::PreviousAsh()
{
    return m_previousAudio;
}

void MetaCache::UpdatePreviousVsh()
{
    Freep(m_previousVideo);
    m_previousVideo = m_video? m_video->Copy() : nullptr;
}

void MetaCache::UpdatePreviousAsh()
{
    Freep(m_previousAudio);
    m_previousAudio = m_audio? m_audio->Copy() : nullptr;
}

error MetaCache::UpdateData(MessageHeader *header, OnMetaDataPacket *metadata, bool &updated)
{
    updated = false;

    error err = SUCCESS;

    Amf0Any* prop = NULL;

    // when exists the duration, remove it to make ExoPlayer happy.
    if (metadata->m_metadata->GetProperty("duration") != NULL) {
        metadata->m_metadata->Remove("duration");
    }

    // generate metadata info to print
    std::stringstream ss;
    if ((prop = metadata->m_metadata->EnsurePropertyNumber("width")) != NULL) {
        ss << ", width=" << (int)prop->ToNumber();
    }
    if ((prop = metadata->m_metadata->EnsurePropertyNumber("height")) != NULL) {
        ss << ", height=" << (int)prop->ToNumber();
    }
    if ((prop = metadata->m_metadata->EnsurePropertyNumber("videocodecid")) != NULL) {
        ss << ", vcodec=" << (int)prop->ToNumber();
    }
    if ((prop = metadata->m_metadata->EnsurePropertyNumber("audiocodecid")) != NULL) {
        ss << ", acodec=" << (int)prop->ToNumber();
    }
    trace("got metadata%s", ss.str().c_str());

    // add server info to metadata
    metadata->m_metadata->Set("server", Amf0Any::Str(RTMP_SIG_SERVER));

    // version, for example, 1.0.0
    // add version to metadata, please donot remove it, for debug.
    metadata->m_metadata->Set("server_version", Amf0Any::Str(RTMP_SIG_VERSION));

    // encode the metadata to payload
    int size = 0;
    char* payload = NULL;
    if ((err = metadata->Encode(size, payload)) != SUCCESS) {
        return ERRORWRAP(err, "encode metadata");
    }

    if (size <= 0) {
        warn("ignore the invalid metadata. size=%d", size);
        return err;
    }

    // create a shared ptr message.
    Freep(m_meta);
    m_meta = new SharedPtrMessage();
    updated = true;

    // dump message to shared ptr message.
    // the payload/size managed by cache_metadata, user should not free it.
    if ((err = m_meta->Create(header, payload, size)) != SUCCESS) {
        return ERRORWRAP(err, "create metadata");
    }

    return err;
}

error MetaCache::UpdateAsh(SharedPtrMessage *msg)
{
    Freep(m_audio);
    m_audio = msg->Copy();
    UpdatePreviousAsh();
    return m_aformat->OnAudio(msg);
}

error MetaCache::UpdateVsh(SharedPtrMessage *msg)
{
    Freep(m_video);
    m_video = msg->Copy();
    UpdatePreviousVsh();
    return m_vformat->OnVideo(msg);
}

LiveSourceManager* sources = NULL;

LiveSourceManager::LiveSourceManager()
{
    m_lock = st_mutex_new();
    m_timer = new HourGlass("sources", this, 1 * UTIME_SECONDS);
}

LiveSourceManager::~LiveSourceManager()
{
    MutexDestroy(m_lock);
    Freep(m_timer);
}

error LiveSourceManager::Initialize()
{
    return SetupTicks();
}

error LiveSourceManager::FetchOrCreate(Request *r, ILiveSourceHandler *h, LiveSource **pps)
{
    error err = SUCCESS;

    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    // TODO: FIXME: Use smaller lock.
    Locker(m_lock);

    LiveSource* source = NULL;
    if ((source = Fetch(r)) != NULL) {
        // we always update the request of resource,
        // for origin auth is on, the token in request maybe invalid,
        // and we only need to update the token of request, it's simple.
        source->UpdateAuth(r);
        *pps = source;
        return err;
    }

    std::string stream_url = r->GetStreamUrl();
    std::string vhost = r->m_vhost;

    // should always not exists for create a source.
    Assert (pool.find(stream_url) == pool.end());

    trace("new live source, stream_url=%s", stream_url.c_str());

    source = new LiveSource();
    if ((err = source->Initialize(r, h)) != SUCCESS) {
        err = ERRORWRAP(err, "init source %s", r->GetStreamUrl().c_str());
        goto failed;
    }

    pool[stream_url] = source;
    *pps = source;
    return err;

failed:
    Freep(source);
    return err;
}

LiveSource *LiveSourceManager::Fetch(Request *r)
{
    LiveSource* source = NULL;

    std::string stream_url = r->GetStreamUrl();
    if (pool.find(stream_url) == pool.end()) {
        return NULL;
    }

    source = pool[stream_url];

    return source;
}

void LiveSourceManager::Dispose()
{
    std::map<std::string, LiveSource*>::iterator it;
    for (it = pool.begin(); it != pool.end(); ++it) {
        LiveSource* source = it->second;
        source->Dispose();
    }
    return;
}

error LiveSourceManager::SetupTicks()
{
    error err = SUCCESS;

    if ((err = m_timer->Tick(1, 1 * UTIME_SECONDS)) != SUCCESS) {
        return ERRORWRAP(err, "tick");
    }

    if ((err = m_timer->Start()) != SUCCESS) {
        return ERRORWRAP(err, "timer");
    }

    return err;
}

error LiveSourceManager::Notify(int event, utime_t interval, utime_t tick)
{
    error err = SUCCESS;

    std::map<std::string, LiveSource*>::iterator it;
    for (it = pool.begin(); it != pool.end();) {
        LiveSource* source = it->second;

        // Do cycle source to cleanup components, such as hls dispose.
        if ((err = source->Cycle()) != SUCCESS) {
            return ERRORWRAP(err, "source=%s/%s cycle", source->SourceId().Cstr(), source->PreSourceId().Cstr());
        }

        // TODO: FIXME: support source cleanup.
        // @see https://github.com/ossrs/srs/issues/713
        // @see https://github.com/ossrs/srs/issues/714
#if 0
        // When source expired, remove it.
        if (source->Expired()) {
            int cid = source->source_id();
            if (cid == -1 && source->pre_source_id() > 0) {
                cid = source->pre_source_id();
            }
            if (cid > 0) {
                _srs_context->set_id(cid);
            }
            srs_trace("cleanup die source, total=%d", (int)pool.size());

            Freep(source);
            pool.erase(it++);
        } else {
            ++it;
        }
#else
        ++it;
#endif
    }

    return err;
}

void LiveSourceManager::Destroy()
{
    std::map<std::string, LiveSource*>::iterator it;
    for (it = pool.begin(); it != pool.end(); ++it) {
        LiveSource* source = it->second;
        Freep(source);
    }
    pool.clear();
}

ILiveSourceBridge::ILiveSourceBridge()
{

}

ILiveSourceBridge::~ILiveSourceBridge()
{

}

LiveSource::LiveSource()
{
    m_req = NULL;
    m_jitterAlgorithm = RtmpJitterAlgorithmOFF;
    m_mixCorrect = false;
    m_mixQueue = new MixQueue();

    m_canPublish = true;
    m_dieAt = 0;

    m_handler = NULL;
    m_bridge = NULL;

//    m_playEdge = new PlayEdge();
//    m_publishEdge = new PublishEdge();
    m_gopCache = new GopCache();
    m_hub = new OriginHub();
    m_meta = new MetaCache();

    m_isMonotonicallyIncrease = false;
    m_lastPacketTime = 0;

    config->Subscribe(this);
    m_atc = false;
}

LiveSource::~LiveSource()
{
    config->Unsubscribe(this);

    // never free the consumers,
    // for all consumers are auto free.
    m_consumers.clear();

    Freep(m_hub);
    Freep(m_meta);
    Freep(m_mixQueue);

//    Freep(m_playEdge);
//    Freep(m_publishEdge);
    Freep(m_gopCache);

    Freep(m_req);
    Freep(m_bridge);
}

void LiveSource::Dispose()
{
    m_hub->Dispose();
    m_meta->Dispose();
    m_gopCache->Dispose();
}

error LiveSource::Cycle()
{
    error err = m_hub->Cycle();
    if (err != SUCCESS) {
        return ERRORWRAP(err, "hub cycle");
    }

    return SUCCESS;
}

bool LiveSource::Expired()
{
    // unknown state?
    if (m_dieAt == 0) {
        return false;
    }

    // still publishing?
    if (!m_canPublish ) { //|| !m_publishEdge->CanPublish()
        return false;
    }

    // has any consumers?
    if (!m_consumers.empty()) {
        return false;
    }

    utime_t now = GetSystemTime();
    if (now > m_dieAt + SOURCE_CLEANUP) {
        return true;
    }

    return false;
}

error LiveSource::Initialize(Request *r, ILiveSourceHandler *h)
{
    error err = SUCCESS;

    Assert(h);
    Assert(!m_req);

    m_handler = h;
    m_req = r->Copy();
    m_atc = config->GetAtc(m_req->m_vhost);

    if ((err = m_hub->Initialize(this, m_req)) != SUCCESS) {
        return ERRORWRAP(err, "hub");
    }

//    if ((err = m_playEdge->Initialize(this, m_req)) != SUCCESS) {
//        return ERRORWRAP(err, "edge(play)");
//    }
//    if ((err = m_publishEdge->Initialize(this, m_req)) != SUCCESS) {
//        return ERRORWRAP(err, "edge(publish)");
//    }

    utime_t queue_size = config->GetQueueLength(m_req->m_vhost);
//    m_publishEdge->set_queue_size(queue_size);

    m_jitterAlgorithm = (RtmpJitterAlgorithm)config->GetTimeJitter(m_req->m_vhost);
    m_mixCorrect = config->GetMixCorrect(m_req->m_vhost);

    return err;
}

void LiveSource::SetBridge(ILiveSourceBridge *v)
{
    Freep(m_bridge);
    m_bridge = v;
}

error LiveSource::OnReloadVhostPlay(std::string vhost)
{
    error err = SUCCESS;

    if (m_req->m_vhost != vhost) {
        return err;
    }

    // time_jitter
    m_jitterAlgorithm = (RtmpJitterAlgorithm)config->GetTimeJitter(m_req->m_vhost);

    // mix_correct
    if (true) {
        bool v = config->GetMixCorrect(m_req->m_vhost);

        // when changed, clear the mix queue.
        if (v != m_mixCorrect) {
            m_mixQueue->Clear();
        }
        m_mixCorrect = v;
    }

    // atc changed.
    if (true) {
        bool v = config->GetAtc(vhost);

        if (v != m_atc) {
            warn("vhost %s atc changed to %d, connected client may corrupt.", vhost.c_str(), v);
            m_gopCache->Clear();
        }
        m_atc = v;
    }

    // gop cache changed.
    if (true) {
        bool v = config->GetGopCache(vhost);

        if (v != m_gopCache->Enabled()) {
            std::string url = m_req->GetStreamUrl();
            trace("vhost %s gop_cache changed to %d, source url=%s", vhost.c_str(), v, url.c_str());
            m_gopCache->Set(v);
        }
    }

    // queue length
    if (true) {
        utime_t v = config->GetQueueLength(m_req->m_vhost);

        if (true) {
            std::vector<LiveConsumer*>::iterator it;

            for (it = m_consumers.begin(); it != m_consumers.end(); ++it) {
                LiveConsumer* consumer = *it;
                consumer->SetQueueSize(v);
            }

            trace("consumers reload queue size success.");
        }

        // TODO: FIXME: https://github.com/ossrs/srs/issues/742#issuecomment-273656897
        // TODO: FIXME: support queue size.
#if 0
        if (true) {
            std::vector<Forwarder*>::iterator it;

            for (it = forwarders.begin(); it != forwarders.end(); ++it) {
                Forwarder* forwarder = *it;
                forwarder->set_queue_size(v);
            }

            srs_trace("forwarders reload queue size success.");
        }

        if (true) {
            publish_edge->set_queue_size(v);
            srs_trace("publish_edge reload queue size success.");
        }
#endif
    }

    return err;
}

error LiveSource::OnSourceIdChanged(ContextId id)
{
    error err = SUCCESS;

    if (!m_sourceId.Compare(id)) {
        return err;
    }

    if (m_preSourceId.Empty()) {
        m_preSourceId = id;
    }
    m_sourceId = id;

    // notice all consumer
    std::vector<LiveConsumer*>::iterator it;
    for (it = m_consumers.begin(); it != m_consumers.end(); ++it) {
        LiveConsumer* consumer = *it;
        consumer->UpdateSourceId();
    }

    return err;
}

ContextId LiveSource::SourceId()
{
    return m_sourceId;
}

ContextId LiveSource::PreSourceId()
{
    return m_preSourceId;
}

bool LiveSource::Inactive()
{
    return m_canPublish;
}

void LiveSource::UpdateAuth(Request *r)
{
    m_req->UpdateAuth(r);
}

bool LiveSource::CanPublish(bool is_edge)
{
    // TODO: FIXME: Should check the status of bridge.

//    if (is_edge) {
//        return m_publishEdge->CanPublish();
//    }

    return m_canPublish;
}

error LiveSource::OnMetaData(CommonMessage *msg, OnMetaDataPacket *metadata)
{
    error err = SUCCESS;

    // if allow atc_auto and bravo-atc detected, open atc for vhost.
    Amf0Any* prop = NULL;
    m_atc = config->GetAtc(m_req->m_vhost);
    if (config->GetAtcAuto(m_req->m_vhost)) {
        if ((prop = metadata->m_metadata->GetProperty("bravo_atc")) != NULL) {
            if (prop->IsString() && prop->ToStr() == "true") {
                m_atc = true;
            }
        }
    }

    // Update the meta cache.
    bool updated = false;
    if ((err = m_meta->UpdateData(&msg->m_header, metadata, updated)) != SUCCESS) {
        return ERRORWRAP(err, "update metadata");
    }
    if (!updated) {
        return err;
    }

    // when already got metadata, drop when reduce sequence header.
    bool drop_for_reduce = false;
    if (m_meta->Data() && config->GetReduceSequenceHeader(m_req->m_vhost)) {
        drop_for_reduce = true;
        warn("drop for reduce sh metadata, size=%d", msg->m_size);
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        std::vector<LiveConsumer*>::iterator it;
        for (it = m_consumers.begin(); it != m_consumers.end(); ++it) {
            LiveConsumer* consumer = *it;
            if ((err = consumer->Enqueue(m_meta->Data(), m_atc, m_jitterAlgorithm)) != SUCCESS) {
                return ERRORWRAP(err, "consume metadata");
            }
        }
    }

    // Copy to hub to all utilities.
    return m_hub->OnMetaData(m_meta->Data(), metadata);
}

error LiveSource::OnAudio(CommonMessage *audio)
{
    error err = SUCCESS;

    // monotically increase detect.
    if (!m_mixCorrect && m_isMonotonicallyIncrease) {
        if (m_lastPacketTime > 0 && audio->m_header.m_timestamp < m_lastPacketTime) {
            m_isMonotonicallyIncrease = false;
            warn("AUDIO: stream not monotonically increase, please open mix_correct.");
        }
    }
    m_lastPacketTime = audio->m_header.m_timestamp;

    // convert shared_audio to msg, user should not use shared_audio again.
    // the payload is transfer to msg, and set to NULL in shared_audio.
    SharedPtrMessage msg;
    if ((err = msg.Create(audio)) != SUCCESS) {
        return ERRORWRAP(err, "create message");
    }

    // directly process the audio message.
    if (!m_mixCorrect) {
        return OnAudioImp(&msg);
    }

    // insert msg to the queue.
    m_mixQueue->Push(msg.Copy());

    // fetch someone from mix queue.
    SharedPtrMessage* m = m_mixQueue->Pop();
    if (!m) {
        return err;
    }

    // consume the monotonically increase message.
    if (m->IsAudio()) {
        err = OnAudioImp(m);
    } else {
        err = OnVideoImp(m);
    }
    Freep(m);

    return err;
}

error LiveSource::OnAudioImp(SharedPtrMessage *msg)
{
    error err = SUCCESS;

    bool is_aac_sequence_header = FlvAudio::Sh(msg->m_payload, msg->m_size);
    bool is_sequence_header = is_aac_sequence_header;

    // whether consumer should drop for the duplicated sequence header.
    bool drop_for_reduce = false;
    if (is_sequence_header && m_meta->PreviousAsh() && config->GetReduceSequenceHeader(m_req->m_vhost)) {
        if (m_meta->PreviousAsh()->m_size == msg->m_size) {
            drop_for_reduce = BytesEquals(m_meta->PreviousAsh()->m_payload, msg->m_payload, msg->m_size);
            warn("drop for reduce sh audio, size=%d", msg->m_size);
        }
    }

    // Copy to hub to all utilities.
    if ((err = m_hub->OnAudio(msg)) != SUCCESS) {
        return ERRORWRAP(err, "consume audio");
    }

    // For bridge to consume the message.
    if (m_bridge && (err = m_bridge->OnAudio(msg)) != SUCCESS) {
        return ERRORWRAP(err, "bridge consume audio");
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)m_consumers.size(); i++) {
            LiveConsumer* consumer = m_consumers.at(i);
            if ((err = consumer->Enqueue(msg, m_atc, m_jitterAlgorithm)) != SUCCESS) {
                return ERRORWRAP(err, "consume message");
            }
        }
    }

    // cache the sequence header of aac, or first packet of mp3.
    // for example, the mp3 is used for hls to write the "right" audio codec.
    // TODO: FIXME: to refine the stream info system.
    if (is_aac_sequence_header || !m_meta->Ash()) {
        if ((err = m_meta->UpdateAsh(msg)) != SUCCESS) {
            return ERRORWRAP(err, "meta consume audio");
        }
    }

    // when sequence header, donot push to gop cache and adjust the timestamp.
    if (is_sequence_header) {
        return err;
    }

    // cache the last gop packets
    if ((err = m_gopCache->Cache(msg)) != SUCCESS) {
        return ERRORWRAP(err, "gop cache consume audio");
    }

    // if atc, update the sequence header to abs time.
    if (m_atc) {
        if (m_meta->Ash()) {
            m_meta->Ash()->m_timestamp = msg->m_timestamp;
        }
        if (m_meta->Data()) {
            m_meta->Data()->m_timestamp = msg->m_timestamp;
        }
    }

    return err;
}

error LiveSource::OnVideo(CommonMessage *shared_video)
{
    error err = SUCCESS;

    // monotically increase detect.
    if (!m_mixCorrect && m_isMonotonicallyIncrease) {
        if (m_lastPacketTime > 0 && shared_video->m_header.m_timestamp < m_lastPacketTime) {
            m_isMonotonicallyIncrease = false;
            warn("VIDEO: stream not monotonically increase, please open mix_correct.");
        }
    }
    m_lastPacketTime = shared_video->m_header.m_timestamp;

    // drop any unknown header video.
    // @see https://github.com/ossrs/srs/issues/421
    if (!FlvVideo::Acceptable(shared_video->m_payload, shared_video->m_size)) {
        char b0 = 0x00;
        if (shared_video->m_size > 0) {
            b0 = shared_video->m_payload[0];
        }

        warn("drop unknown header video, size=%d, bytes[0]=%#x", shared_video->m_size, b0);
        return err;
    }

    // convert shared_video to msg, user should not use shared_video again.
    // the payload is transfer to msg, and set to NULL in shared_video.
    SharedPtrMessage msg;
    if ((err = msg.Create(shared_video)) != SUCCESS) {
        return ERRORWRAP(err, "create message");
    }

    // directly process the video message.
    if (!m_mixCorrect) {
        return OnVideoImp(&msg);
    }

    // insert msg to the queue.
    m_mixQueue->Push(msg.Copy());

    // fetch someone from mix queue.
    SharedPtrMessage* m = m_mixQueue->Pop();
    if (!m) {
        return err;
    }

    // consume the monotonically increase message.
    if (m->IsAudio()) {
        err = OnAudioImp(m);
    } else {
        err = OnVideoImp(m);
    }
    Freep(m);

    return err;
}

error LiveSource::OnVideoImp(SharedPtrMessage *msg)
{
    error err = SUCCESS;

    bool is_sequence_header = FlvVideo::Sh(msg->m_payload, msg->m_size);

    // whether consumer should drop for the duplicated sequence header.
    bool drop_for_reduce = false;
    if (is_sequence_header && m_meta->PreviousVsh() && config->GetReduceSequenceHeader(m_req->m_vhost)) {
        if (m_meta->PreviousVsh()->m_size == msg->m_size) {
            drop_for_reduce = BytesEquals(m_meta->PreviousVsh()->m_payload, msg->m_payload, msg->m_size);
            warn("drop for reduce sh video, size=%d", msg->m_size);
        }
    }

    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (is_sequence_header && (err = m_meta->UpdateVsh(msg)) != SUCCESS) {
        return ERRORWRAP(err, "meta update video");
    }

    // Copy to hub to all utilities.
    if ((err = m_hub->OnVideo(msg, is_sequence_header)) != SUCCESS) {
        return ERRORWRAP(err, "hub consume video");
    }

    // For bridge to consume the message.
    if (m_bridge && (err = m_bridge->OnVideo(msg)) != SUCCESS) {
        return ERRORWRAP(err, "bridge consume video");
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)m_consumers.size(); i++) {
            LiveConsumer* consumer = m_consumers.at(i);
            if ((err = consumer->Enqueue(msg, m_atc, m_jitterAlgorithm)) != SUCCESS) {
                return ERRORWRAP(err, "consume video");
            }
        }
    }

    // when sequence header, donot push to gop cache and adjust the timestamp.
    if (is_sequence_header) {
        return err;
    }

    // cache the last gop packets
    if ((err = m_gopCache->Cache(msg)) != SUCCESS) {
        return ERRORWRAP(err, "gop cache consume vdieo");
    }

    // if atc, update the sequence header to abs time.
    if (m_atc) {
        if (m_meta->Vsh()) {
            m_meta->Vsh()->m_timestamp = msg->m_timestamp;
        }
        if (m_meta->Data()) {
            m_meta->Data()->m_timestamp = msg->m_timestamp;
        }
    }

    return err;
}

error LiveSource::OnAggregate(CommonMessage *msg)
{
    error err = SUCCESS;

    Buffer* stream = new Buffer(msg->m_payload, msg->m_size);
    AutoFree(Buffer, stream);

    // the aggregate message always use abs time.
    int delta = -1;

    while (!stream->Empty()) {
        if (!stream->Require(1)) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate");
        }
        int8_t type = stream->Read1Bytes();

        if (!stream->Require(3)) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate");
        }
        int32_t data_size = stream->Read3Bytes();

        if (data_size < 0) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate size");
        }

        if (!stream->Require(3)) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate time");
        }
        int32_t timestamp = stream->Read3Bytes();

        if (!stream->Require(1)) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate time(high bits)");
        }
        int32_t time_h = stream->Read1Bytes();

        timestamp |= time_h<<24;
        timestamp &= 0x7FFFFFFF;

        // adjust abs timestamp in aggregate msg.
        // only -1 means uninitialized delta.
        if (delta == -1) {
            delta = (int)msg->m_header.m_timestamp - (int)timestamp;
        }
        timestamp += delta;

        if (!stream->Require(3)) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate stream id");
        }
        int32_t stream_id = stream->Read3Bytes();

        if (data_size > 0 && !stream->Require(data_size)) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate data");
        }

        // to common message.
        CommonMessage o;

        o.m_header.m_messageType = type;
        o.m_header.m_payloadLength = data_size;
        o.m_header.m_timestampDelta = timestamp;
        o.m_header.m_timestamp = timestamp;
        o.m_header.m_streamId = stream_id;
        o.m_header.m_perferCid = msg->m_header.m_perferCid;

        if (data_size > 0) {
            o.m_size = data_size;
            o.m_payload = new char[o.m_size];
            stream->ReadBytes(o.m_payload, o.m_size);
        }

        if (!stream->Require(4)) {
            return ERRORNEW(ERROR_RTMP_AGGREGATE, "aggregate previous tag size");
        }
        stream->Read4Bytes();

        // process parsed message
        if (o.m_header.IsAudio()) {
            if ((err = OnAudio(&o)) != SUCCESS) {
                return ERRORWRAP(err, "consume audio");
            }
        } else if (o.m_header.IsVideo()) {
            if ((err = OnVideo(&o)) != SUCCESS) {
                return ERRORWRAP(err, "consume video");
            }
        }
    }

    return err;
}

error LiveSource::OnPublish()
{
    error err = SUCCESS;

    // update the request object.
    Assert(m_req);

    m_canPublish = false;

    // whatever, the publish thread is the source or edge source,
    // save its id to srouce id.
    if ((err = OnSourceIdChanged(Context->GetId())) != SUCCESS) {
        return ERRORWRAP(err, "source id change");
    }

    // reset the mix queue.
    m_mixQueue->Clear();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    m_meta->Clear();

    // detect the monotonically again.
    m_isMonotonicallyIncrease = true;
    m_lastPacketTime = 0;

    // Notify the hub about the publish event.
    if ((err = m_hub->OnPublish()) != SUCCESS) {
        return ERRORWRAP(err, "hub publish");
    }

    // notify the handler.
    Assert(m_handler);
    if ((err = m_handler->OnPublish(this, m_req)) != SUCCESS) {
        return ERRORWRAP(err, "handle publish");
    }

    if (m_bridge && (err = m_bridge->OnPublish()) != SUCCESS) {
        return ERRORWRAP(err, "bridge publish");
    }

    Statistic* stat = Statistic::Instance();
    stat->OnStreamPublish(m_req, m_sourceId.Cstr());

    return err;
}

void LiveSource::OnUnpublish()
{
    // ignore when already unpublished.
    if (m_canPublish) {
        return;
    }

    // Notify the hub about the unpublish event.
    m_hub->OnUnpublish();

    // only clear the gop cache,
    // donot clear the sequence header, for it maybe not changed,
    // when drop dup sequence header, drop the metadata also.
    m_gopCache->Clear();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    m_meta->UpdatePreviousVsh();
    m_meta->UpdatePreviousAsh();

    trace("cleanup when unpublish");

    m_canPublish = true;
    if (!m_sourceId.Empty()) {
        m_preSourceId = m_sourceId;
    }
    m_sourceId = ContextId();

    // notify the handler.
    Assert(m_handler);
    Statistic* stat = Statistic::Instance();
    stat->OnStreamClose(m_req);

    m_handler->OnUnpublish(this, m_req);

    if (m_bridge) {
        m_bridge->OnUnpublish();
        Freep(m_bridge);
    }

    // no consumer, stream is die.
    if (m_consumers.empty()) {
        m_dieAt = GetSystemTime();
    }
}

error LiveSource::CreateConsumer(LiveConsumer *&consumer)
{
    error err = SUCCESS;

    consumer = new LiveConsumer(this);
    m_consumers.push_back(consumer);

//    // for edge, when play edge stream, check the state
//    if (config->GetVhostIsEdge(m_req->m_vhost)) {
//        // notice edge to start for the first client.
//        if ((err = m_playEdge->OnClientPlay()) != SUCCESS) {
//            return ERRORWRAP(err, "play edge");
//        }
//    }

    return err;
}

error LiveSource::ConsumerDumps(LiveConsumer *consumer, bool ds, bool dm, bool dg)
{
    error err = SUCCESS;

    utime_t queue_size = config->GetQueueLength(m_req->m_vhost);
    consumer->SetQueueSize(queue_size);

    // if atc, update the sequence header to gop cache time.
    if (m_atc && !m_gopCache->Empty()) {
        if (m_meta->Data()) {
            m_meta->Data()->m_timestamp = u2ms(m_gopCache->StartTime());
        }
        if (m_meta->Vsh()) {
            m_meta->Vsh()->m_timestamp = u2ms(m_gopCache->StartTime());
        }
        if (m_meta->Ash()) {
            m_meta->Ash()->m_timestamp = u2ms(m_gopCache->StartTime());
        }
    }

    // If stream is publishing, dumps the sequence header and gop cache.
    if (m_hub->Active()) {
        // Copy metadata and sequence header to consumer.
        if ((err = m_meta->Dumps(consumer, m_atc, m_jitterAlgorithm, dm, ds)) != SUCCESS) {
            return ERRORWRAP(err, "meta dumps");
        }

        // copy gop cache to client.
        if (dg && (err = m_gopCache->Dump(consumer, m_atc, m_jitterAlgorithm)) != SUCCESS) {
            return ERRORWRAP(err, "gop cache dumps");
        }
    }

    // print status.
    if (dg) {
        trace("create consumer, active=%d, queue_size=%.2f, jitter=%d", m_hub->Active(), queue_size, m_jitterAlgorithm);
    } else {
        trace("create consumer, active=%d, ignore gop cache, jitter=%d", m_hub->Active(), m_jitterAlgorithm);
    }

    return err;
}

void LiveSource::OnConsumerDestroy(LiveConsumer *consumer)
{
    std::vector<LiveConsumer*>::iterator it;
    it = std::find(m_consumers.begin(), m_consumers.end(), consumer);
    if (it != m_consumers.end()) {
        it = m_consumers.erase(it);
    }

    if (m_consumers.empty()) {
//        m_playEdge->on_all_client_stop();
        m_dieAt = GetSystemTime();
    }
}

void LiveSource::SetCache(bool enabled)
{
    m_gopCache->Set(enabled);
}

RtmpJitterAlgorithm LiveSource::Jitter()
{
    return m_jitterAlgorithm;
}

error LiveSource::OnEdgeStartPublish()
{
//    return m_publishEdge->on_client_publish();
}

error LiveSource::OnEdgeProxyPublish(CommonMessage *msg)
{
//    return m_publishEdge->on_proxy_publish(msg);
}

void LiveSource::OnEdgeProxyUnpublish()
{
//    m_publishEdge->on_proxy_unpublish();
}

std::string LiveSource::GetCurrOrigin()
{
//    return m_playEdge->get_curr_origin();
}

#include "protocol_kbps.h"

KbpsSlice::KbpsSlice(WallClock *c)
{
    m_clk = c;
    m_starttime = 0;
    m_bytes = 0;
}

KbpsSlice::~KbpsSlice()
{

}

void KbpsSlice::Sample()
{
    utime_t now = m_clk->Now();

    if (m_sample_30s.m_time < 0) {
        m_sample_30s.Update(m_bytes, now, 0);
    }
    if (m_sample_1m.m_time < 0) {
        m_sample_1m.Update(m_bytes, now, 0);
    }
    if (m_sample_5m.m_time < 0) {
        m_sample_5m.Update(m_bytes, now, 0);
    }
    if (m_sample_60m.m_time < 0) {
        m_sample_60m.Update(m_bytes, now, 0);
    }

    if (now - m_sample_30s.m_time >= 30 * UTIME_SECONDS) {
        int kbps = (int)((m_bytes - m_sample_30s.m_total) * 8 / u2ms(now - m_sample_30s.m_time));
        m_sample_30s.Update(m_bytes, now, kbps);
    }
    if (now - m_sample_1m.m_time >= 60 * UTIME_SECONDS) {
        int kbps = (int)((m_bytes - m_sample_1m.m_total) * 8 / u2ms(now - m_sample_1m.m_time));
        m_sample_1m.Update(m_bytes, now, kbps);
    }
    if (now - m_sample_5m.m_time >= 300 * UTIME_SECONDS) {
        int kbps = (int)((m_bytes - m_sample_5m.m_total) * 8 / u2ms(now - m_sample_5m.m_time));
        m_sample_5m.Update(m_bytes, now, kbps);
    }
    if (now - m_sample_60m.m_time >= 3600 * UTIME_SECONDS) {
        int kbps = (int)((m_bytes - m_sample_60m.m_total) * 8 / u2ms(now - m_sample_60m.m_time));
        m_sample_60m.Update(m_bytes, now, kbps);
    }
}

IKbpsDelta::IKbpsDelta()
{

}

IKbpsDelta::~IKbpsDelta()
{

}

EphemeralDelta::EphemeralDelta()
{
    m_in = m_out = 0;
}

EphemeralDelta::~EphemeralDelta()
{

}

void EphemeralDelta::AddDelta(int64_t in, int64_t out)
{
    m_in += in;
    m_out += out;
}

void EphemeralDelta::Remark(int64_t *in, int64_t *out)
{
    if (in) *in = m_in;
    if (out) *out = m_out;
    m_in = m_out = 0;
}

NetworkDelta::NetworkDelta()
{
    m_in = m_out = NULL;
    m_inBase = m_inDelta = 0;
    m_outBase = m_outDelta = 0;
}

NetworkDelta::~NetworkDelta()
{

}

void NetworkDelta::SetIo(IProtocolStatistic *in, IProtocolStatistic *out)
{
    if (m_in) {
        m_inDelta += m_in->GetRecvBytes() - m_inBase;
    }
    if (in) {
        m_inBase = in->GetRecvBytes();
        m_inDelta += m_inBase;
    }
    m_in = in;

    if (m_out) {
        m_outDelta += m_out->GetSendBytes() - m_outBase;
    }
    if (out) {
        m_outBase = out->GetSendBytes();
        m_outDelta += m_outBase;
    }
    m_out = out;
}

void NetworkDelta::Remark(int64_t *in, int64_t *out)
{
    if (m_in) {
        m_inDelta += m_in->GetRecvBytes() - m_inBase;
        m_inBase = m_in->GetRecvBytes();
    }
    if (m_out) {
        m_outDelta += m_out->GetSendBytes() - m_outBase;
        m_outBase = m_out->GetSendBytes();
    }

    *in = m_inDelta;
    *out = m_outDelta;
    m_inDelta = m_outDelta = 0;
}

Kbps::Kbps(WallClock *c)
{
    m_clk = c ? c : _clock;
    m_is = new KbpsSlice(m_clk);
    m_os = new KbpsSlice(m_clk);
}

Kbps::~Kbps()
{
    Freep(m_is);
    Freep(m_os);
}

int Kbps::GetSendKbps()
{
    int duration = u2ms(m_clk->Now() - m_is->m_starttime);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = GetSendBytes();
    return (int)(bytes * 8 / duration);
}

int Kbps::GetRecvKbps()
{
    int duration = u2ms(m_clk->Now() - m_os->m_starttime);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = GetRecvBytes();
    return (int)(bytes * 8 / duration);
}

int Kbps::GetSendKbps30s()
{
    return m_os->m_sample_30s.m_rate;
}

int Kbps::GetRecvKbps30s()
{
    return m_is->m_sample_30s.m_rate;
}

int Kbps::GetSendKbps5m()
{
    return m_os->m_sample_5m.m_rate;
}

int Kbps::GetRecvKbps5m()
{
    return m_is->m_sample_5m.m_rate;
}

void Kbps::AddDelta(IKbpsDelta* delta)
{
    if (!delta) return;

    int64_t in, out;
    delta->Remark(&in, &out);
    AddDelta(in, out);
}

void Kbps::AddDelta(int64_t in, int64_t out)
{
    // update the total bytes
    m_is->m_bytes += in;
    m_os->m_bytes += out;

    // we donot sample, please use sample() to do resample.
}

void Kbps::Sample()
{
    m_is->Sample();
    m_os->Sample();
}

int64_t Kbps::GetSendBytes()
{
    return m_os->m_bytes;
}

int64_t Kbps::GetRecvBytes()
{
    return m_is->m_bytes;
}

NetworkKbps::NetworkKbps(WallClock *clock)
{
    m_delta = new NetworkDelta();
    m_kbps = new Kbps(clock);
}

NetworkKbps::~NetworkKbps()
{
    Freep(m_kbps);
    Freep(m_delta);
}

void NetworkKbps::SetIo(IProtocolStatistic *in, IProtocolStatistic *out)
{
    m_delta->SetIo(in, out);
}

void NetworkKbps::Sample()
{
    m_kbps->AddDelta(m_delta);
    m_kbps->Sample();
}

int NetworkKbps::GetSendKbps()
{
    return m_kbps->GetSendKbps();
}

int NetworkKbps::GetRecvKbps()
{
    return m_kbps->GetRecvKbps();
}

int NetworkKbps::GetSendKbps30s()
{
    return m_kbps->GetSendKbps30s();
}

int NetworkKbps::GetRecvKbps30s()
{
    return m_kbps->GetRecvKbps30s();
}

int NetworkKbps::GetSendKbps5m()
{
    return m_kbps->GetSendKbps5m();
}

int NetworkKbps::GetRecvKbps5m()
{
    return m_kbps->GetRecvKbps5m();
}

int64_t NetworkKbps::GetSendBytes()
{
    return m_kbps->GetSendBytes();
}

int64_t NetworkKbps::GetRecvBytes()
{
    return m_kbps->GetRecvBytes();
}

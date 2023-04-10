#ifndef PROTOCOL_KBPS_H
#define PROTOCOL_KBPS_H


#include "kbps.h"
#include "protocol_io.h"
#include <cstdint>

/**
 * The slice of kbps statistic, for input or output.
 */

class KbpsSlice
{
private:
    WallClock* m_clk;
public:
    // session startup bytes
    // @remark, use total_bytes() to get the total bytes of slice.
    int64_t m_bytes;
    // slice starttime, the first time to record bytes.
    utime_t m_starttime;
    // samples
    RateSample m_sample_30s;
    RateSample m_sample_1m;
    RateSample m_sample_5m;
    RateSample m_sample_60m;
public:
    KbpsSlice(WallClock* c);
    virtual ~KbpsSlice();
public:
    // Resample the slice to calculate the kbps.
    virtual void Sample();
};

/**
 * The interface which provices delta of bytes. For example, we got a delta from a TCP client:
 *       ISrsKbpsDelta* delta = ...;
 * Now, we can add delta simple to a kbps:
 *      kbps->add_delta(delta);
 * Or by multiple kbps:
 *       int64_t in, out;
 *       delta->remark(&in, &out);
 *       kbps1->add_delta(in, out);
 *       kbpsN->add_delta(in, out);
 * Then you're able to use the kbps object.
 */
class IKbpsDelta
{
public:
    IKbpsDelta();
    virtual ~IKbpsDelta();
public:
    // Resample to get the value of delta bytes.
    // @remark If no delta bytes, both in and out will be set to 0.
    virtual void Remark(int64_t* in, int64_t* out) = 0;
};

// A delta data source for SrsKbps, used in ephemeral case, for example, UDP server to increase stat when received or
// sent out each UDP packet.
class EphemeralDelta : public IKbpsDelta
{
private:
    uint64_t m_in;
    uint64_t m_out;
public:
    EphemeralDelta();
    virtual ~EphemeralDelta();
public:
    virtual void AddDelta(int64_t in, int64_t out);
// Interface ISrsKbpsDelta.
public:
    virtual void Remark(int64_t* in, int64_t* out);
};

// A network delta data source for SrsKbps.
class NetworkDelta : public IKbpsDelta
{
private:
    IProtocolStatistic* m_in;
    IProtocolStatistic* m_out;
    uint64_t m_inBase;
    uint64_t m_inDelta;
    uint64_t m_outBase;
    uint64_t m_outDelta;
public:
    NetworkDelta();
    virtual ~NetworkDelta();
public:
    // Switch the under-layer network io, we use the bytes as a fresh delta.
    virtual void SetIo(IProtocolStatistic* in, IProtocolStatistic* out);
// Interface ISrsKbpsDelta.
public:
    virtual void Remark(int64_t* in, int64_t* out);
};

/**
 * To statistic the kbps. For example, we got a set of connections and add the total delta:
 *       SrsKbps* kbps = ...;
 *       for conn in connections:
 *           kbps->add_delta(conn->delta()) // Which return an ISrsKbpsDelta object.
 * Then we sample and got the total kbps:
 *       kbps->sample()
 *       kbps->get_xxx_kbps().
 */
class Kbps
{
private:
    KbpsSlice* m_is;
    KbpsSlice* m_os;
    WallClock* m_clk;
public:
    // Note that we won't free the clock c.
    Kbps(WallClock* c = NULL);
    virtual ~Kbps();
public:
    // Get total average kbps.
    virtual int GetSendKbps();
    virtual int GetRecvKbps();
    // Get the average kbps in 30s.
    virtual int GetSendKbps30s();
    virtual int GetRecvKbps30s();
    // Get the average kbps in 5m or 300s.
    virtual int GetSendKbps5m();
    virtual int GetRecvKbps5m();
public:
    // Add delta to kbps. Please call sample() after all deltas are added to kbps.
    virtual void AddDelta(IKbpsDelta *delta);
    virtual void AddDelta(int64_t in, int64_t out);
    // Sample the kbps to get the kbps in N seconds.
    virtual void Sample();
public:
    virtual int64_t GetSendBytes();
    virtual int64_t GetRecvBytes();
};

// A sugar to use SrsNetworkDelta and SrsKbps.
class NetworkKbps
{
private:
    NetworkDelta* m_delta;
    Kbps* m_kbps;
public:
    NetworkKbps(WallClock* clock = NULL);
    virtual ~NetworkKbps();
public:
    virtual void SetIo(IProtocolStatistic* in, IProtocolStatistic* out);
    virtual void Sample();
public:
    virtual int GetSendKbps();
    virtual int GetRecvKbps();
    virtual int GetSendKbps30s();
    virtual int GetRecvKbps30s();
    virtual int GetSendKbps5m();
    virtual int GetRecvKbps5m();
public:
    virtual int64_t GetSendBytes();
    virtual int64_t GetRecvBytes();
};

#endif // PROTOCOL_KBPS_H

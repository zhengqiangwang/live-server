#ifndef PROTOCOL_IO_H
#define PROTOCOL_IO_H

#include "core_time.h"
#include "log.h"
#include "io.h"
#include <sys/types.h>

/**
 * The system io reader/writer architecture:
 *                                         +---------------+  +---------------+
 *                                         | IStreamWriter |  | IVectorWriter |
 *                                         +---------------+  +---------------+
 *                                         | + write()     |  | + writev()    |
 *                                         +-------------+-+  ++--------------+
 * +----------+     +--------------------+               /\   /\
 * | IReader  |     |    IStatistic      |                 \ /
 * +----------+     +--------------------+                  V
 * | + read() |     | + get_recv_bytes() |           +------+----+
 * +------+---+     | + get_send_bytes() |           |  IWriter  |
 *       / \        +---+--------------+-+           +-------+---+
 *        |            / \            / \                   / \
 *        |             |              |                     |
 * +------+-------------+------+      ++---------------------+--+
 * | IProtocolReader           |      | IProtocolWriter         |
 * +---------------------------+      +-------------------------+
 * | + readfully()             |      | + set_send_timeout()    |
 * | + set_recv_timeout()      |      +-------+-----------------+
 * +------------+--------------+             / \
 *             / \                            |
 *              |                             |
 *           +--+-----------------------------+-+
 *           |       IProtocolReadWriter        |
 *           +----------------------------------+
 */

/**
 * Get the statistic of channel.
 */

class IProtocolStatistic
{
public:
    IProtocolStatistic();
    virtual ~IProtocolStatistic();
// For protocol
public:
    // Get the total recv bytes over underlay fd.
    virtual int64_t GetRecvBytes() = 0;
    // Get the total send bytes over underlay fd.
    virtual int64_t GetSendBytes() = 0;
};

/**
 * the reader for the protocol to read from whatever channel.
 */
class IProtocolReader : public IReader, virtual public IProtocolStatistic
{
public:
    IProtocolReader();
    virtual ~IProtocolReader();
// for protocol
public:
    // Set the timeout tm in srs_utime_t for recv bytes from peer.
    // @remark Use SRS_UTIME_NO_TIMEOUT to never timeout.
    virtual void SetRecvTimeout(utime_t tm) = 0;
    // Get the timeout in srs_utime_t for recv bytes from peer.
    virtual utime_t GetRecvTimeout() = 0;
// For handshake.
public:
    // Read specified size bytes of data
    // @param nread, the actually read size, NULL to ignore.
    virtual error ReadFully(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
 * the writer for the protocol to write to whatever channel.
 */
class IProtocolWriter : public IWriter, virtual public IProtocolStatistic
{
public:
    IProtocolWriter();
    virtual ~IProtocolWriter();
// For protocol
public:
    // Set the timeout tm in srs_utime_t for send bytes to peer.
    // @remark Use SRS_UTIME_NO_TIMEOUT to never timeout.
    virtual void SetSendTimeout(utime_t tm) = 0;
    // Get the timeout in srs_utime_t for send bytes to peer.
    virtual utime_t GetSendTimeout() = 0;
};

/**
 * The reader and writer.
 */
class IProtocolReadWriter : public IProtocolReader, public IProtocolWriter
{
public:
    IProtocolReadWriter();
    virtual ~IProtocolReadWriter();
};

#endif // PROTOCOL_IO_H

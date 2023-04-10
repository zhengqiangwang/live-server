#include "protocol_stream.h"
#include "error.h"
#include "utility.h"
#include <cstring>


// the default recv buffer size, 128KB.
#define DEFAULT_RECV_BUFFER_SIZE 131072

// limit user-space buffer to 256KB, for 3Mbps stream delivery.
//      800*2000/8=200000B(about 195KB).
// @remark it's ok for higher stream, the buffer is ok for one chunk is 256KB.
#define MAX_SOCKET_BUFFER 262144

// the max header size,
// @see SrsProtocol::read_message_header().
#define RTMP_MAX_MESSAGE_HEADER 11

#ifdef PERF_MERGED_READ
IMergeReadHandler::IMergeReadHandler()
{
}

IMergeReadHandler::~IMergeReadHandler()
{
}
#endif

FastStream::FastStream(int size)
{
#ifdef PERF_MERGED_READ
    m_mergedRead = false;
    m_handler = NULL;
#endif

    m_nbBuffer = size? size:DEFAULT_RECV_BUFFER_SIZE;
    m_buffer = (char*)malloc(m_nbBuffer);
    m_p = m_end = m_buffer;
}

FastStream::~FastStream()
{
    free(m_buffer);
    m_buffer = NULL;
}

int FastStream::Size()
{
    return (int)(m_end - m_p);
}

char *FastStream::Bytes()
{
    return m_p;
}

void FastStream::SetBuffer(int buffer_size)
{
    // never exceed the max size.
    if (buffer_size > MAX_SOCKET_BUFFER) {
        warn("limit buffer size %d to %d", buffer_size, MAX_SOCKET_BUFFER);
    }

    // the user-space buffer size limit to a max value.
    int nb_resize_buf = MIN(buffer_size, MAX_SOCKET_BUFFER);

    // only realloc when buffer changed bigger
    if (nb_resize_buf <= m_nbBuffer) {
        return;
    }

    // realloc for buffer change bigger.
    int start = (int)(m_p - m_buffer);
    int nb_bytes = (int)(m_end - m_p);

    m_buffer = (char*)realloc(m_buffer, nb_resize_buf);
    m_nbBuffer = nb_resize_buf;
    m_p = m_buffer + start;
    m_end = m_p + nb_bytes;
}

char FastStream::Read1Byte()
{
    Assert(m_end - m_p >= 1);
    return *m_p++;
}

char *FastStream::ReadSlice(int size)
{
    Assert(size >= 0);
    Assert(m_end - m_p >= size);
    Assert(m_p + size >= m_buffer);

    char* ptr = m_p;
    m_p += size;

    return ptr;
}

void FastStream::Skip(int size)
{
    Assert(m_end - m_p >= size);
    Assert(m_p + size >= m_buffer);
    m_p += size;
}

error FastStream::Grow(IReader *reader, int required_size)
{
    error err = SUCCESS;

    // already got required size of bytes.
    if (m_end - m_p >= required_size) {
        return err;
    }

    // must be positive.
    Assert(required_size > 0);

    // the free space of buffer,
    //      buffer = consumed_bytes + exists_bytes + free_space.
    int nb_free_space = (int)(m_buffer + m_nbBuffer - m_end);

    // the bytes already in buffer
    int nb_exists_bytes = (int)(m_end - m_p);
    Assert(nb_exists_bytes >= 0);

    // resize the space when no left space.
    if (nb_exists_bytes + nb_free_space < required_size) {
        // reset or move to get more space.
        if (!nb_exists_bytes) {
            // reset when buffer is empty.
            m_p = m_end = m_buffer;
        } else if (nb_exists_bytes < m_nbBuffer && m_p > m_buffer) {
            // move the left bytes to start of buffer.
            // @remark Only move memory when space is enough, or failed at next check.
            // @see https://github.com/ossrs/srs/issues/848
            m_buffer = (char*)memmove(m_buffer, m_p, nb_exists_bytes);
            m_p = m_buffer;
            m_end = m_p + nb_exists_bytes;
        }

        // check whether enough free space in buffer.
        nb_free_space = (int)(m_buffer + m_nbBuffer - m_end);
        if (nb_exists_bytes + nb_free_space < required_size) {
            return ERRORNEW(ERROR_READER_BUFFER_OVERFLOW, "overflow, required=%d, max=%d, left=%d", required_size, m_nbBuffer, nb_free_space);
        }
    }

    // buffer is ok, read required size of bytes.
    while (m_end - m_p < required_size) {
        ssize_t nread;
        if ((err = reader->Read(m_end, nb_free_space, &nread)) != SUCCESS) {
            return ERRORWRAP(err, "read bytes");
        }

#ifdef PERF_MERGED_READ
        /**
         * to improve read performance, merge some packets then read,
         * when it on and read small bytes, we sleep to wait more data.,
         * that is, we merge some data to read together.
         */
        if (m_mergedRead && m_handler) {
            m_handler->OnRead(nread);
        }
#endif

        // we just move the ptr to next.
        Assert((int)nread > 0);
        m_end += nread;
        nb_free_space -= (int)nread;
    }

    return err;
}

#ifdef PERF_MERGED_READ
void FastStream::SetMergeRead(bool v, IMergeReadHandler *handler)
{
    m_mergedRead = v;
    m_handler = handler;
}
#endif

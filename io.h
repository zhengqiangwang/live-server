#ifndef IO_H
#define IO_H

#include "log.h"

#ifndef _WIN32
#include <sys/uio.h>
#endif

/**
 * The reader to read data from channel.
 */
class IReader
{
public:
    IReader();
    virtual ~IReader();
public:
    /**
     * Read bytes from reader.
     * @param nread How many bytes read from channel. NULL to ignore.
     */
    virtual error Read(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
 * The seeker to seek with a device.
 */
class ISeeker
{
public:
    ISeeker();
    virtual ~ISeeker();
public:
    /**
     * The Lseek() function repositions the offset of the file descriptor fildes to the argument offset, according to the
     * directive whence. Lseek() repositions the file pointer fildes as follows:
     *      If whence is SEEK_SET, the offset is set to offset bytes.
     *      If whence is SEEK_CUR, the offset is set to its current location plus offset bytes.
     *      If whence is SEEK_END, the offset is set to the size of the file plus offset bytes.
     * @param seeked Upon successful completion, lseek() returns the resulting offset location as measured in bytes from
     *      the beginning of the file. NULL to ignore.
     */
    virtual error Lseek(off_t offset, int whence, off_t* seeked) = 0;
};

/**
 * The reader and seeker.
 */
class IReadSeeker : public IReader, public ISeeker
{
public:
    IReadSeeker();
    virtual ~IReadSeeker();
};

/**
 * The writer to write stream data to channel.
 */
class IStreamWriter
{
public:
    IStreamWriter();
    virtual ~IStreamWriter();
public:
    /**
     * write bytes over writer.
     * @nwrite the actual written bytes. NULL to ignore.
     */
    virtual error Write(void* buf, size_t size, ssize_t* nwrite) = 0;
};

/**
 * The vector writer to write vector(iovc) to channel.
 */
class IVectorWriter
{
public:
    IVectorWriter();
    virtual ~IVectorWriter();
public:
    /**
     * write iov over writer.
     * @nwrite the actual written bytes. NULL to ignore.
     * @remark for the HTTP FLV, to writev to improve performance.
     *      @see https://github.com/ossrs/srs/issues/405
     */
    virtual error Writev(const iovec *iov, int iov_size, ssize_t* nwrite) = 0;
};

/**
 * The generally writer, stream and vector writer.
 */
class IWriter : public IStreamWriter, public IVectorWriter
{
public:
    IWriter();
    virtual ~IWriter();
};

/**
 * The writer and seeker.
 */
class IWriteSeeker : public IWriter, public ISeeker
{
public:
    IWriteSeeker();
    virtual ~IWriteSeeker();
};

#endif // IO_H

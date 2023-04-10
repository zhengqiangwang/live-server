#ifndef FILE_H
#define FILE_H


#include "io.h"
#include "log.h"
#include <string>

#ifndef _WIN32
#include <sys/uio.h>
#endif

class FileReader;

/**
 * file writer, to write to file.
 */
class FileWriter : public IWriteSeeker
{
private:
    std::string m_path;
    int m_fd;
public:
    FileWriter();
    virtual ~FileWriter();
public:
    /**
     * open file writer, in truncate mode.
     * @param p a string indicates the path of file to open.
     */
    virtual error Open(std::string p);
    /**
     * open file writer, in append mode.
     * @param p a string indicates the path of file to open.
     */
    virtual error OpenAppend(std::string p);
    /**
     * close current writer.
     * @remark user can reopen again.
     */
    virtual void Close();
public:
    virtual bool IsOpen();
    virtual void Seek2(int64_t offset);
    virtual int64_t Tellg();
// Interface ISrsWriteSeeker
public:
    virtual error Write(void* buf, size_t count, ssize_t* pnwrite);
    virtual error Writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual error Lseek(off_t offset, int whence, off_t* seeked);
};

// The file reader factory.
class IFileReaderFactory
{
public:
    IFileReaderFactory();
    virtual ~IFileReaderFactory();
public:
    virtual FileReader* CreateFileReader();
};

/**
 * file reader, to read from file.
 */
class FileReader : public IReadSeeker
{
private:
    std::string m_path;
    int m_fd;
public:
    FileReader();
    virtual ~FileReader();
public:
    /**
     * open file reader.
     * @param p a string indicates the path of file to open.
     */
    virtual error Open(std::string p);
    /**
     * close current reader.
     * @remark user can reopen again.
     */
    virtual void Close();
public:
    // TODO: FIXME: extract interface.
    virtual bool IsOpen();
    virtual int64_t Tellg();
    virtual void Skip(int64_t size);
    virtual int64_t Seek2(int64_t offset);
    virtual int64_t Filesize();
// Interface ISrsReadSeeker
public:
    virtual error Read(void* buf, size_t count, ssize_t* pnread);
    virtual error Lseek(off_t offset, int whence, off_t* seeked);
};

// For utest to mock it.
typedef int (*open_t)(const char* path, int oflag, ...);
typedef ssize_t (*write_t)(int fildes, const void* buf, size_t nbyte);
typedef ssize_t (*read_t)(int fildes, void* buf, size_t nbyte);
typedef off_t (*lseek_t)(int fildes, off_t offset, int whence);
typedef int (*close_t)(int fildes);

#endif // FILE_H

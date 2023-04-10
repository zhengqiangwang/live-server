#include "file.h"
#include "error.h"
#include <fcntl.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/uio.h>
#endif

// For utest to mock it.
open_t open_fn = ::open;
write_t write_fn = ::write;
read_t read_fn = ::read;
lseek_t lseek_fn = ::lseek;
close_t close_fn = ::close;

FileWriter::FileWriter()
{
    m_fd = -1;
}

FileWriter::~FileWriter()
{
    Close();
}

error FileWriter::Open(std::string p)
{
    error err = SUCCESS;

    if (m_fd > 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", p.c_str());
    }

    int flags = O_CREAT|O_WRONLY|O_TRUNC;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;

    if ((m_fd = open_fn(p.c_str(), flags, mode)) < 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
    }

    m_path = p;

    return err;
}

error FileWriter::OpenAppend(std::string p)
{
    error err = SUCCESS;

    if (m_fd > 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", m_path.c_str());
    }

    int flags = O_CREAT|O_APPEND|O_WRONLY;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;

    if ((m_fd = open_fn(p.c_str(), flags, mode)) < 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
    }

    m_path = p;

    return err;
}

void FileWriter::Close()
{
    if (m_fd < 0) {
        return;
    }

    if (close_fn(m_fd) < 0) {
        warn("close file %s failed", m_path.c_str());
    }
    m_fd = -1;

    return;
}

bool FileWriter::IsOpen()
{
    return m_fd > 0;
}

void FileWriter::Seek2(int64_t offset)
{
    off_t r0 = lseek_fn(m_fd, (off_t)offset, SEEK_SET);
    Assert(r0 != -1);
}

int64_t FileWriter::Tellg()
{
    return (int64_t)lseek_fn(m_fd, 0, SEEK_CUR);
}

error FileWriter::Write(void *buf, size_t count, ssize_t *pnwrite)
{
    error err = SUCCESS;

    ssize_t nwrite;
    // TODO: FIXME: use st_write.
#ifdef _WIN32
    if ((nwrite = ::_write(fd, buf, (unsigned int)count)) < 0) {
#else
    if ((nwrite = write_fn(m_fd, buf, count)) < 0) {
#endif
        return ERRORNEW(ERROR_SYSTEM_FILE_WRITE, "write to file %s failed", m_path.c_str());
    }

    if (pnwrite != NULL) {
        *pnwrite = nwrite;
    }

    return err;
}

error FileWriter::Writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    error err = SUCCESS;

    ssize_t nwrite = 0;
    for (int i = 0; i < iovcnt; i++) {
        const iovec* piov = iov + i;
        ssize_t this_nwrite = 0;
        if ((err = Write(piov->iov_base, piov->iov_len, &this_nwrite)) != SUCCESS) {
            return ERRORWRAP(err, "write file");
        }
        nwrite += this_nwrite;
    }

    if (pnwrite) {
        *pnwrite = nwrite;
    }

    return err;
}

error FileWriter::Lseek(off_t offset, int whence, off_t *seeked)
{
    off_t sk = lseek_fn(m_fd, offset, whence);
    if (sk < 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_SEEK, "seek file");
    }

    if (seeked) {
        *seeked = sk;
    }

    return SUCCESS;
}

IFileReaderFactory::IFileReaderFactory()
{

}

IFileReaderFactory::~IFileReaderFactory()
{

}

FileReader *IFileReaderFactory::CreateFileReader()
{
    return new FileReader();
}

FileReader::FileReader()
{
    m_fd = -1;
}

FileReader::~FileReader()
{
    Close();
}

error FileReader::Open(std::string p)
{
    error err = SUCCESS;

    if (m_fd > 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", m_path.c_str());
    }

    if ((m_fd = open_fn(p.c_str(), O_RDONLY)) < 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
    }

    m_path = p;

    return err;
}

void FileReader::Close()
{
    int ret = ERROR_SUCCESS;

    if (m_fd < 0) {
        return;
    }

    if (close_fn(m_fd) < 0) {
        warn("close file %s failed. ret=%d", m_path.c_str(), ret);
    }
    m_fd = -1;

    return;
}

bool FileReader::IsOpen()
{
    return m_fd > 0;
}

int64_t FileReader::Tellg()
{
    return (int64_t)lseek_fn(m_fd, 0, SEEK_CUR);
}

void FileReader::Skip(int64_t size)
{
    off_t r0 = lseek_fn(m_fd, (off_t)size, SEEK_CUR);
    Assert(r0 != -1);
}

int64_t FileReader::Seek2(int64_t offset)
{
    return (int64_t)lseek_fn(m_fd, (off_t)offset, SEEK_SET);
}

int64_t FileReader::Filesize()
{
    int64_t cur = Tellg();
    int64_t size = (int64_t)lseek_fn(m_fd, 0, SEEK_END);

    off_t r0 = lseek_fn(m_fd, (off_t)cur, SEEK_SET);
    Assert(r0 != -1);

    return size;
}

error FileReader::Read(void *buf, size_t count, ssize_t *pnread)
{
    error err = SUCCESS;

    ssize_t nread;
    // TODO: FIXME: use st_read.
#ifdef _WIN32
    if ((nread = _read(fd, buf, (unsigned int)count)) < 0) {
#else
    if ((nread = read_fn(m_fd, buf, count)) < 0) {
#endif
        return ERRORNEW(ERROR_SYSTEM_FILE_READ, "read from file %s failed", m_path.c_str());
    }

    if (nread == 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_EOF, "file EOF");
    }

    if (pnread != NULL) {
        *pnread = nread;
    }

    return err;
}

error FileReader::Lseek(off_t offset, int whence, off_t *seeked)
{
    off_t sk = lseek_fn(m_fd, offset, whence);
    if (sk < 0) {
        return ERRORNEW(ERROR_SYSTEM_FILE_SEEK, "seek %d failed", (int)sk);
    }

    if (seeked) {
        *seeked = sk;
    }

    return SUCCESS;
}

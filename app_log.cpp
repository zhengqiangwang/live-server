#include "app_log.h"
#include "app_config.h"
#include "app_thread.h"
#include "app_utility.h"
#include "protocol_log.h"
#include "utility.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

// the max size of a line of log.
#define LOG_MAX_SIZE 8192

// the tail append to each log.
#define LOG_TAIL '\n'
// reserved for the end of log data, it must be strlen(LOG_TAIL)
#define LOG_TAIL_SIZE 1

FileLog::FileLog()
{
    m_level = LogLevelTrace;
    m_logData = new char[LOG_MAX_SIZE];

    m_fd = -1;
    m_logToFileTank = false;
    m_utc = false;
    m_mutex = new ThreadMutex();
}

FileLog::~FileLog()
{
    Freepa(m_logData);

    if(m_fd > 0){
        ::close(m_fd);
        m_fd = -1;
    }

    if(config){
        config->Unsubscribe(this);
    }

    Freep(m_mutex);
}

error FileLog::Initialize()
{
    if (config) {
        config->Subscribe(this);

        m_logToFileTank = config->GetLogTankFile();
        m_utc = config->GetUtcTime();

        std::string level = config->GetLogLevel();
        std::string level_v2 = config->GetLogLevelV2();
        m_level = level_v2.empty() ? GetLogLevel(level) : GetLogLevelV2(level_v2);
    }

    return SUCCESS;
}

void FileLog::Reopen()
{
    if (m_fd > 0) {
        ::close(m_fd);
    }

    if (!m_logToFileTank) {
        return;
    }

    OpenLogFile();
}

void FileLog::log(LogLevel level, const char *tag, const ContextId &context_id, const char *fmt, va_list args)
{
    if (level < m_level || level >= LogLevelDisabled) {
        return;
    }

    ThreadLocker(m_mutex);

    int size = 0;
    bool header_ok = LogHeader(
        m_logData, LOG_MAX_SIZE, m_utc, level >= LogLevelWarn, tag, context_id, LogLevelStrings[level], &size
    );
    if (!header_ok) {
        return;
    }

    // Something not expected, drop the log.
    int r0 = vsnprintf(m_logData + size, LOG_MAX_SIZE - size, fmt, args);
    if (r0 <= 0 || r0 >= LOG_MAX_SIZE - size) {
        return;
    }
    size += r0;

    // Add errno and strerror() if error. Check size to avoid security issue https://github.com/ossrs/srs/issues/1229
    if (level == LogLevelError && errno != 0 && size < LOG_MAX_SIZE) {
        r0 = snprintf(m_logData + size, LOG_MAX_SIZE - size, "(%s)", strerror(errno));

        // Something not expected, drop the log.
        if (r0 <= 0 || r0 >= LOG_MAX_SIZE - size) {
            return;
        }
        size += r0;
    }

    WriteLog(m_fd, m_logData, size, level);
}

void FileLog::WriteLog(int &fd, char *str_log, int size, int level)
{
    // ensure the tail and EOF of string
    //      LOG_TAIL_SIZE for the TAIL char.
    //      1 for the last char(0).
    size = MIN(LOG_MAX_SIZE - 1 - LOG_TAIL_SIZE, size);

    // add some to the end of char.
    str_log[size++] = LOG_TAIL;

    // if not to file, to console and return.
    if (!m_logToFileTank) {
        // if is error msg, then print color msg.
        // \033[31m : red text code in shell
        // \033[32m : green text code in shell
        // \033[33m : yellow text code in shell
        // \033[0m : normal text code
        if (level <= LogLevelTrace) {
            printf("%.*s", size, str_log);
        } else if (level == LogLevelWarn) {
            printf("\033[33m%.*s\033[0m", size, str_log);
        } else{
            printf("\033[31m%.*s\033[0m", size, str_log);
        }
        fflush(stdout);

        return;
    }

    // open log file. if specified
    if (fd < 0) {
        OpenLogFile();
    }

    // write log to file.
    if (fd > 0) {
        ::write(fd, str_log, size);
    }
}

void FileLog::OpenLogFile()
{
    if (!config) {
        return;
    }

    std::string filename = config->GetLogFile();

    if (filename.empty()) {
        return;
    }

    m_fd = ::open(filename.c_str(),
        O_RDWR | O_CREAT | O_APPEND,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH
    );
}

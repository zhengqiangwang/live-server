#ifndef APP_LOG_H
#define APP_LOG_H

#include "protocol_log.h"
#include "app_reload.h"

class ThreadMutex;

// For log TAGs.
#define TAG_MAIN "MAIN"
#define TAG_MAYBE "MAYBE"
#define TAG_DTLS_ALERT "DTLS_ALERT"
#define TAG_DTLS_HANG "DTLS_HANG"
#define TAG_RESOURCE_UNSUB "RESOURCE_UNSUB"
#define TAG_LARGE_TIMER "LARGE_TIMER"

// Use memory/disk cache and donot flush when write log.
// it's ok to use it without config, which will log to console, and default trace level.
// when you want to use different level, override this classs, set the protected _level.
class FileLog : public ILog, public IReloadHandler
{
private:
    LogLevel m_level;
private:
    char* m_logData;
    //log to file if specified log_file
    int m_fd;
    //whether log to file tank
    bool m_logToFileTank;
    //whether use utc time
    bool m_utc;
    ThreadMutex* m_mutex;
public:
    FileLog();
    virtual ~FileLog();
//ILog interface
public:
    virtual error Initialize();
    virtual void Reopen();
    virtual void log(LogLevel level, const char* tag, const ContextId& context_id, const char* fmt, va_list args);
private:
    virtual void WriteLog(int& fd, char* str_log, int size, int level);
    virtual void OpenLogFile();
};

#endif // APP_LOG_H

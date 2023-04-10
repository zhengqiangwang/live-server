#include "protocol_log.h"
#include "protocol_utility.h"
#include "kbps.h"
#include "error.h"
#include <cstring>
#include <sys/time.h>

Pps* pps_cids_get = nullptr;
Pps* pps_cids_set = nullptr;

#define BASIC_LOG_SIZE 8192

ThreadContext::ThreadContext()
{

}

ThreadContext::~ThreadContext()
{

}

ContextId ThreadContext::GenerateId()
{
    ContextId cid = ContextId();
    return cid.SetValue(RandomStr(8));
}

static ContextId context_default;
static int context_key = -1;
void ContextDestructor(void* arg)
{
    ContextId* cid = (ContextId*)arg;
    Freep(cid);
}
const ContextId &ThreadContext::GetId()
{
    ++pps_cids_get->m_sugar;

    if(!st_thread_self()){
        return context_default;
    }

    void* cid = st_thread_getspecific(context_key);
    if(!cid){
        return context_default;
    }

    return *(ContextId*)cid;
}

const ContextId &ThreadContext::SetId(const ContextId &v)
{
    return ContextSetCidOf(st_thread_self(), v);
}

void ThreadContext::ClearCid()
{

}

const ContextId &ContextSetCidOf(st_thread_t trd, const ContextId &v)
{
    ++pps_cids_set->m_sugar;

    if(!trd){
        context_default = v;
        return v;
    }

    ContextId* cid = new ContextId();
    *cid = v;

    if(context_key < 0){
        int r0 = st_key_create(&context_key, ContextDestructor);
        Assert(r0 == 0);
    }

    int r0 = st_thread_setspecific2(trd, context_key, cid);
    Assert(r0 == 0);

    return v;
}

ImplContextRestore::ImplContextRestore(ContextId cid)
{
    m_cid = cid;
}

ImplContextRestore::~ImplContextRestore()
{
    Context->SetId(m_cid);
}

ConsoleLog::ConsoleLog(LogLevel l, bool u)
{
    m_level = l;
    m_utc = u;

    m_buffer = new char[BASIC_LOG_SIZE];
}

ConsoleLog::~ConsoleLog()
{
    Freepa(m_buffer);
}

error ConsoleLog::Initialize()
{
    return SUCCESS;
}

void ConsoleLog::Reopen()
{

}

void ConsoleLog::log(LogLevel level, const char *tag, const ContextId &context_id, const char *fmt, va_list args)
{
    if(level < m_level || level >= LogLevelDisabled){
        return;
    }

    int size = 0;
    if(!LogHeader(m_buffer, BASIC_LOG_SIZE, m_utc, level >= LogLevelWarn, tag, context_id, LogLevelStrings[level], &size)){
        return;
    }

    //something not expected drop the log.
    int r0 = vsnprintf(m_buffer + size, BASIC_LOG_SIZE - size, fmt, args);
    if(r0 <= 0 || r0 >= BASIC_LOG_SIZE - size){
        return;
    }
    size += r0;

    //add errno and strerror() if error
    if(level == LogLevelError && errno != 0){
        r0 = snprintf(m_buffer + size, BASIC_LOG_SIZE - size, "(%s)", strerror(errno));

        //something not expected, drop the log
        if(r0 <= 0 || r0 >= BASIC_LOG_SIZE - size){
            return;
        }
        size += r0;
    }

    if(level >= LogLevelWarn){
        fprintf(stderr, "%s\n", m_buffer);
    }else{
        fprintf(stdout, "%s\n", m_buffer);
    }
}

bool LogHeader(char *buffer, int size, bool utc, bool dangerous, const char *tag, ContextId cid, const char *level, int *psize)
{
    //clock time
    timeval tv;
    if(gettimeofday(&tv, nullptr) == -1){
        return false;
    }

    //to calendar time
    struct tm now;
    //each of these functions returns nullptr in case an error was detected. @see https://linux.die.net/man/3/localtime_r
    if(utc){
       if(gmtime_r(&tv.tv_sec, &now) == nullptr){
           return false;
       }
    }else{
        if(localtime_r(&tv.tv_sec, &now) == nullptr){
            return false;
        }
    }

    int written = -1;
    if(dangerous){
        if(tag){
            written = snprintf(buffer, size,                 "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d][%s] ",
                               1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                               level, getpid(), cid.Cstr(), errno, tag);
        } else {
                   written = snprintf(buffer, size,
                       "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%d] ",
                       1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                       level, getpid(), cid.Cstr(), errno);
               }
    }else{
        if(tag){
            written = snprintf(buffer, size,                 "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s][%s] ",
                               1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                               level, getpid(), cid.Cstr(), tag);
        }else{
            written = snprintf(buffer, size,
                "[%d-%02d-%02d %02d:%02d:%02d.%03d][%s][%d][%s] ",
                1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, (int)(tv.tv_usec / 1000),
                level, getpid(), cid.Cstr());
        }
    }

    //exceed the size, ignore this log.
    //check size to avoid security issue
    if(written <= 0 || written >= size){
        return false;
    }

    //write the header size
    *psize = written;

    return true;
}

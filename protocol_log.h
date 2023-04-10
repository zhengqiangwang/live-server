#ifndef PROTOCOLLOG_H
#define PROTOCOLLOG_H

#include "log.h"
#include "core.h"
#include "st/st.h"

#include <map>

//the state thread context, GetId will get the st-thread id,
//which identify the client
class ThreadContext : public IContext
{
private:
    std::map<st_thread_t, ContextId> cache;
public:
    ThreadContext();
    virtual ~ThreadContext();
public:
    virtual ContextId GenerateId();
    virtual const ContextId& GetId();
    virtual const ContextId& SetId(const ContextId& v);
private:
    virtual void ClearCid();
};

//set the context id of specified thread
extern const ContextId& ContextSetCidOf(st_thread_t trd, const ContextId& v);

//The context restore stores the context and restore it when done
#define ContextRestore(cid) ImplContextRestore _context_restore_instance(cid);
class ImplContextRestore
{
private:
    ContextId m_cid;
public:
    ImplContextRestore(ContextId cid);
    virtual ~ImplContextRestore();
};

//the basic console log, which write log to console
class ConsoleLog : public ILog
{
private:
    LogLevel m_level;
    bool m_utc;
private:
    char* m_buffer;
public:
    ConsoleLog(LogLevel l, bool u);
    virtual ~ConsoleLog();
//interface ILog
public:
    virtual error Initialize();
    virtual void Reopen();
    virtual void log(LogLevel level, const char* tag, const ContextId& context_id, const char* fmt, va_list args);
};

//generate the log header
bool LogHeader(char* buffer, int size, bool utc, bool dangerous, const char* tag, ContextId cid, const char* level, int* psize);

#endif // PROTOCOLLOG_H

#ifndef LOG_H
#define LOG_H

#include "core.h"


// Please note that the enum name might not be the string, to keep compatible with previous definition.
enum LogLevel
{
    LogLevelForbidden = 0x00,
    LogLevelVerbose = 0x01,
    LogLevelInfo = 0x02,
    LogLevelTrace = 0x04,
    LogLevelWarn = 0x08,
    LogLevelError = 0x10,

    LogLevelDisabled = 0x20,
};

//Get the level in string
extern const char* LogLevelStrings[];

//the log interface provides method to write log.
class ILog
{
public:
    ILog();
    virtual ~ILog();
public:
    //initialize log
    virtual error Initialize() = 0;
    //reopen the log file for log rotate
    virtual void Reopen() = 0;
public:
    //write a application level log
    virtual void log(LogLevel level, const char* tag, const ContextId& contextId, const char* fmt, va_list args) = 0;
};

class IContext
{
public:
    IContext();
    virtual ~IContext();
public:
    //generate a new context id
    virtual ContextId GenerateId() = 0;
    //get the context id of current thread
    virtual const ContextId& GetId() = 0;
    //set the context id of current thread
    //@return the current context id
    virtual const ContextId& SetId(const ContextId& v) = 0;
};

extern IContext* Context;

extern ILog* Log;

extern void LoggerImpl(LogLevel level, const char* tag, const ContextId& context_id, const char* fmt, ...);

// Log style.
// Use __FUNCTION__ to print c method
// Use __PRETTY_FUNCTION__ to print c++ class:method
#define verbose(msg, ...) LoggerImpl(LogLevelVerbose, NULL, Context->GetId(), msg, ##__VA_ARGS__)
#define info(msg, ...) LoggerImpl(LogLevelInfo, NULL, Context->GetId(), msg, ##__VA_ARGS__)
#define trace(msg, ...) LoggerImpl(LogLevelTrace, NULL, Context->GetId(), msg, ##__VA_ARGS__)
#define warn(msg, ...) LoggerImpl(LogLevelWarn, NULL, Context->GetId(), msg, ##__VA_ARGS__)
#define ERROR(msg, ...) LoggerImpl(LogLevelError, NULL, Context->GetId(), msg, ##__VA_ARGS__)
// With tag.
#define verbose2(tag, msg, ...) LoggerImpl(LogLevelVerbose, tag, Context->GetId(), msg, ##__VA_ARGS__)
#define info2(tag, msg, ...) LoggerImpl(LogLevelInfo, tag, Context->GetId(), msg, ##__VA_ARGS__)
#define trace2(tag, msg, ...) LoggerImpl(LogLevelTrace, tag, Context->GetId(), msg, ##__VA_ARGS__)
#define warn2(tag, msg, ...) LoggerImpl(LogLevelWarn, tag, Context->GetId(), msg, ##__VA_ARGS__)
#define error2(tag, msg, ...) LoggerImpl(LogLevelError, tag, Context->GetId(), msg, ##__VA_ARGS__)

// TODO: FIXME: Add more verbose and info logs.
//#ifndef VERBOSE
//    #undef verbose
//    #define verbose(msg, ...) (void)0
//#endif
//#ifndef INFO
//    #undef info
//    #define info(msg, ...) (void)0
//#endif
//#ifndef TRACE
//    #undef trace
//    #define trace(msg, ...) (void)0
//#endif

#endif // LOG_H

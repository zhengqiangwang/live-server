#include "log.h"
#include "stdarg.h"

const char* LogLevelStrings[] = {
#ifdef SRS_LOG_LEVEL_V2
        // The v2 log level specs by log4j.
        "FORB",     "TRACE",     "DEBUG",    NULL,   "INFO",    NULL, NULL, NULL,
        "WARN",     NULL,       NULL,       NULL,   NULL,       NULL, NULL, NULL,
        "ERROR",    NULL,       NULL,       NULL,   NULL,       NULL, NULL, NULL,
        NULL,       NULL,       NULL,       NULL,   NULL,       NULL, NULL, NULL,
        "OFF",
#else
        // SRS 4.0 level definition, to keep compatible.
        "Forb",     "Verb",     "Debug",    NULL,   "Trace",    NULL, NULL, NULL,
        "Warn",     NULL,       NULL,       NULL,   NULL,       NULL, NULL, NULL,
        "Error",    NULL,       NULL,       NULL,   NULL,       NULL, NULL, NULL,
        NULL,       NULL,       NULL,       NULL,   NULL,       NULL, NULL, NULL,
        "Off",
#endif
};

ILog::ILog()
{

}

ILog::~ILog()
{

}

IContext::IContext()
{

}

IContext::~IContext()
{

}

void LoggerImpl(LogLevel level, const char *tag, const ContextId &context_id, const char *fmt, ...)
{
    if(!Log)return;

    va_list args;
    va_start(args, fmt);
    Log->log(level, tag, context_id, fmt, args);
    va_end(args);
}

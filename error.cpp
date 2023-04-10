#include "error.h"
#include "log.h"
#include <map>
#include <sstream>
#include <cerrno>
#include <cassert>
#include <cstdarg>
#include <unistd.h>

extern bool IsSystemControlError(error err)
{
    int errorcode = ERRORCODE(err);
    return errorcode == ERROR_CONTROL_RTMP_CLOSE
        || errorcode == ERROR_CONTROL_REPUBLISH
        || errorcode == ERROR_CONTROL_REDIRECT;
}

extern bool IsClientGracefullyClose(error err)
{
    int errorcode = ERRORCODE(err);
    return errorcode == ERROR_SOCKET_READ
        || errorcode == ERROR_SOCKET_READ_FULLY
        || errorcode == ERROR_SOCKET_WRITE;
}

extern bool IsServerGracefullyClose(error err)
{
    int code = ERRORCODE(err);
    return code == ERROR_HTTP_STREAM_EOF;
}

ComplexError::ComplexError()
{
    m_code = ERROR_SUCCESS;
    m_wrapped = nullptr;
    m_rerrno = m_line = 0;
}

ComplexError::~ComplexError()
{
    Freep(m_wrapped);
}

std::string ComplexError::Description()
{
    if(m_desc.empty())
    {
        std::stringstream ss;
        ss << "code=" << m_code;

        std::string codestr = ERRORCODESTR(this);
        if (!codestr.empty())
        {
            ss << "(" << codestr <<")";
        }

        std::string codeLongstr = ERRORCODELONGSTR(this);
        if(!codeLongstr.empty())
        {
            ss << "(" << codeLongstr << ")";
        }

        ComplexError* next = this;
        while(next)
        {
            ss << " : " << next->m_msg;
            next = next->m_wrapped;
        }
        ss << std::endl;

        next = this;
        while(next) {
            ss << "thread [" << getpid() << "][" << next->m_cid.Cstr() << "]: "
            << next->m_func << "() [" << next->m_file << ":" << next->m_line << "]"
            << "[errno=" << next->m_rerrno << "]";

            next = next->m_wrapped;

            if(next)
            {
                ss << std::endl;
            }
        }

        m_desc = ss.str();
    }

    return m_desc;
}

std::string ComplexError::Summary()
{
    if(m_summary.empty())
    {
        std::stringstream ss;

        ss << "code=" << m_code;

        std::string codeStr = ERRORCODESTR(this);
        if(!codeStr.empty())
        {
            ss << "(" << codeStr << ")";
        }

        ComplexError* next = this;
        while(next){
            ss << " : " << next->m_msg;
            next = next->m_wrapped;
        }

        m_summary = ss.str();
    }

    return m_summary;
}

ComplexError* ComplexError::Create(const char* func, const char* file, int line, int code, const char* fmt, ...)
{
    int rerrno = (int)errno;

    va_list ap;
    va_start(ap, fmt);
    static char buffer[4096];
    int r0 = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    ComplexError* err = new ComplexError();

    err->m_func = func;
    err->m_file = file;
    err->m_line = line;
    err->m_code = code;
    err->m_rerrno = rerrno;
    if(r0 > 0 && r0 < (int)sizeof (buffer)){
        err->m_msg = std::string(buffer, r0);
    }

    err->m_wrapped = nullptr;
    if(Context){
        err->m_cid = Context->GetId();
    }

    return err;
}

ComplexError* ComplexError::Wrap(const char* func, const char* file, int line, ComplexError* v, const char* fmt, ...)
{
    int rerrno = (int)errno;

    va_list ap;
    va_start(ap, fmt);
    static char buffer[4096];
    int r0 = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    ComplexError* err = new ComplexError();

    err->m_func = func;
    err->m_file = file;
    err->m_line = line;
    if(v)
    {
        err->m_code = v->m_code;
    }
    err->m_rerrno = rerrno;
    if(r0 > 0 && r0 < (int)sizeof (buffer)){
        err->m_msg = std::string(buffer, r0);
    }
    err->m_wrapped = v;
    if(Context){
        err->m_cid = Context->GetId();
    }

    return err;
}

ComplexError* ComplexError::Success()
{
    return nullptr;
}

ComplexError* ComplexError::Copy(ComplexError* from)
{
    if(from == SUCCESS){
        return SUCCESS;
    }

    ComplexError* err = new ComplexError();

    err->m_code = from->m_code;
    err->m_wrapped = ERRORCOPY(from->m_wrapped);
    err->m_msg = from->m_msg;
    err->m_func = from->m_func;
    err->m_file = from->m_file;
    err->m_line = from->m_line;
    err->m_cid = from->m_cid;
    err->m_rerrno = from->m_rerrno;
    err->m_desc = from->m_desc;

    return err;
}

std::string ComplexError::Description(ComplexError* err)
{
    return err ? err->Description() : "Success";
}

std::string ComplexError::Summary(ComplexError* err)
{
    return err ? err->Summary() : "Success";
}

int ComplexError::ErrorCode(ComplexError* err)
{
    return err ? err->m_code : ERROR_SUCCESS;
}

#define STRERRNO_GEN(n, v, m, s) {(ErrorCode)v, m, s},
static struct
{
    ErrorCode code;
    const char* name;
    const char* description;
} StrerrorTab[] = {
#ifndef _WIN32
    {ERROR_SUCCESS, "Success", "SUccess"},
#endif
    ERRNO_MAP_SYSTEM(STRERRNO_GEN)
    ERRNO_MAP_RTMP(STRERRNO_GEN)
    ERRNO_MAP_APP(STRERRNO_GEN)
    ERRNO_MAP_HTTP(STRERRNO_GEN)
    ERRNO_MAP_USER(STRERRNO_GEN)
};
#undef STRERRNO_GEN

std::string ComplexError::ErrorCodeStr(ComplexError* err)
{
    static std::string not_found = "";
    static std::map<enum ErrorCode, std::string> error_map;

    if(error_map.empty())
    {
        for(int i = 0; i < (int)(sizeof(StrerrorTab) / sizeof(StrerrorTab[0])); i++)
        {
            enum ErrorCode code = StrerrorTab[i].code;
            error_map[code] = StrerrorTab[i].name;
        }
    }

    std::map<enum ErrorCode, std::string>::iterator it = error_map.find((enum ErrorCode)ERRORCODE(err));
    if(it == error_map.end())
    {
        return not_found;
    }

    return it->second;
}

std::string ComplexError::ErrorCodeLongstr(ComplexError* err)
{
    static std::string not_found = "";
    static std::map<enum ErrorCode, std::string> error_map;

    if(error_map.empty())
    {
        for(int i = 0; i < (int)(sizeof(StrerrorTab) / sizeof (StrerrorTab[0])); i++)
        {
            enum ErrorCode code = StrerrorTab[i].code;
            error_map[code] = StrerrorTab[i].description;
        }
    }

    std::map<enum ErrorCode, std::string>::iterator it = error_map.find((enum ErrorCode)ERRORCODE(err));
    if(it == error_map.end())
    {
        return not_found;
    }

    return it->second;
}

void ComplexError::Assert(bool expression)
{
   assert(expression);
}

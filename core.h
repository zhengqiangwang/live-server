#ifndef CORE_H
#define CORE_H

#define INTERNAL_STR(v) #v
#define XSTR(v) INTERNAL_STR(v)

//the project informations, may sent to client in HTTP header of RTMP metadata.
#define VERSION_MAJOR "1"
#define VERSION_MINOR "0"
#define VERSION_REVISION "0"
#define RTMP_SIG_KEY "SERVER"
#define RTMP_SIG_CODE "Bee"
#define RTMP_SIG_URL "127.0.0.1"
#define RTMP_SIG_LICENSE "MIT"
#define CONSTRIBUTORS "WANG"
#define RTMP_SIG_VERSION XSTR(VERSION_MAJOR) "." XSTR(VERSION_MINOR) "." XSTR(VERSION_REVISION)
#define RTMP_SIG_SERVER RTMP_SIG_KEY "/" RTMP_SIG_VERSION "(" RTMP_SIG_CODE ")"
#define RTMP_SIG_DOMAIN "127.0.0.1"
#define RTMP_SIG_AUTHORS "wang"

//the current edition
#define VERSION_STABLE 1
#define VERSION_STABLE_BRANCH XSTR(VERSION_STABLE) ".0release"

//use Freep(p) free p and set to nullptr
#define Freep(p) \
    if(p) { \
        delete p; \
        p = nullptr; \
    } \
    (void)0


//use Freepa(T[]) to free an array
#define Freepa(pa) \
    if(pa) { \
        delete[] pa; \
        pa = nullptr; \
    } \
    (void) 0

class ComplexError;
typedef ComplexError *error;

#include <string>


//the context id, it default to a string object, we can use other objects
class ContextId
{
private:
    std::string m_id = "";

public:
    ContextId();
    ContextId(const ContextId &cp);
    ContextId& operator=(const ContextId &cp);
    virtual ~ContextId();
public:
    const char *Cstr() const;
    bool Empty() const;
    //this compare is string compare
    int Compare(const ContextId &to) const;
    //set the context id value
    ContextId &SetValue(const std::string &id);
};

#endif // CORE_H

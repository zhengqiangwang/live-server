#ifndef APP_SECURITY_H
#define APP_SECURITY_H


#include "protocol_rtmp_stack.h"
#include <string>

class ConfDirective;

// The security apply on vhost.
class Security
{
public:
    Security();
    virtual ~Security();
public:
    // Security check the client apply by vhost security strategy
    // @param type the client type, publish or play.
    // @param ip the ip address of client.
    // @param req the request object of client.
    virtual error Check(RtmpConnType type, std::string ip, Request* req);
private:
    virtual error DoCheck(ConfDirective* rules, RtmpConnType type, std::string ip, Request* req);
    virtual error AllowCheck(ConfDirective* rules, RtmpConnType type, std::string ip);
    virtual error DenyCheck(ConfDirective* rules, RtmpConnType type, std::string ip);
};

#endif // APP_SECURITY_H

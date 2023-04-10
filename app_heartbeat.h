#ifndef APP_HEARTBEAT_H
#define APP_HEARTBEAT_H

#include "log.h"

// The http heartbeat to api-server to notice api that the information of .
class HttpHeartbeat
{
public:
    HttpHeartbeat();
    virtual ~HttpHeartbeat();
public:
    virtual void Heartbeat();
private:
    virtual error DoHeartbeat();
};
#endif // APP_HEARTBEAT_H

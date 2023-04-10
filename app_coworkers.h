#ifndef APP_COWORKERS_H
#define APP_COWORKERS_H


#include "log.h"
#include <map>
class JsonAny;
class Request;
class LiveSource;

// For origin cluster.
class CoWorkers
{
private:
    static CoWorkers* _instance;
private:
    std::map<std::string, Request*> m_streams;
private:
    CoWorkers();
    virtual ~CoWorkers();
public:
    static CoWorkers* Instance();
public:
    virtual JsonAny* Dumps(std::string vhost, std::string coworker, std::string app, std::string stream);
private:
    virtual Request* FindStreamInfo(std::string vhost, std::string app, std::string stream);
public:
    virtual error OnPublish(LiveSource* s, Request* r);
    virtual void OnUnpublish(LiveSource* s, Request* r);
};

#endif // APP_COWORKERS_H

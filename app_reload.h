#ifndef APP_RELOAD_H
#define APP_RELOAD_H

#include "core.h"

// The handler for config reload.
// When reload callback, the config is updated yet.
//
// Features not support reload,
class IReloadHandler
{
public:
    IReloadHandler();
    virtual ~IReloadHandler();
public:
    virtual error OnReloadMaxConns();
    virtual error OnReloadListen();
    virtual error OnReloadPithyPrint();
    virtual error OnReloadHttpApiRawApi();
    virtual error OnReloadRtcServer();
public:
    virtual error OnReloadVhostAdded(std::string vhost);
    virtual error OnReloadVhostRemoved(std::string vhost);
    virtual error OnReloadVhostPlay(std::string vhost);
    virtual error OnReloadVhostForward(std::string vhost);
    virtual error OnReloadVhostDash(std::string vhost);
    virtual error OnReloadVhostHls(std::string vhost);
    virtual error OnReloadVhostHds(std::string vhost);
    virtual error OnReloadVhostDvr(std::string vhost);
    virtual error OnReloadVhostPublish(std::string vhost);
    virtual error OnReloadVhostTcpNodelay(std::string vhost);
    virtual error OnReloadVhostRealtime(std::string vhost);
    virtual error OnReloadVhostChunkSize(std::string vhost);
    virtual error OnReloadVhostTranscode(std::string vhost);
    virtual error OnReloadVhostExec(std::string vhost);
    virtual error OnReloadIngestRemoved(std::string vhost, std::string ingest_id);
    virtual error OnReloadIngestAdded(std::string vhost, std::string ingest_id);
    virtual error OnReloadIngestUpdated(std::string vhost, std::string ingest_id);
    virtual error OnReloadUserInfo();
};

#endif // APP_RELOAD_H

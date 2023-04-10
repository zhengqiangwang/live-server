#include "app_reload.h"
#include "error.h"

IReloadHandler::IReloadHandler()
{

}

IReloadHandler::~IReloadHandler()
{

}

error IReloadHandler::OnReloadMaxConns()
{
    return SUCCESS;
}

error IReloadHandler::OnReloadListen()
{
    return SUCCESS;
}

error IReloadHandler::OnReloadPithyPrint()
{
    return SUCCESS;
}

error IReloadHandler::OnReloadHttpApiRawApi()
{
    return SUCCESS;
}

error IReloadHandler::OnReloadRtcServer()
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostAdded(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostRemoved(std::string vhost)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostPlay(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostForward(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostDash(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostHls(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostHds(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostDvr(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostPublish(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostTcpNodelay(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostRealtime(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostChunkSize(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostTranscode(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadVhostExec(std::string /*vhost*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadIngestRemoved(std::string /*vhost*/, std::string /*ingest_id*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadIngestAdded(std::string /*vhost*/, std::string /*ingest_id*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadIngestUpdated(std::string /*vhost*/, std::string /*ingest_id*/)
{
    return SUCCESS;
}

error IReloadHandler::OnReloadUserInfo()
{
    return SUCCESS;
}

#include "app_heartbeat.h"
#include "app_config.h"
#include "app_utility.h"
#include "error.h"
#include "protocol_http_stack.h"
#include "core_autofree.h"
#include "protocol_json.h"
#include "protocol_utility.h"
#include "protocol_http_client.h"

HttpHeartbeat::HttpHeartbeat()
{

}

HttpHeartbeat::~HttpHeartbeat()
{

}

void HttpHeartbeat::Heartbeat()
{
    error err = DoHeartbeat();
    if (err != SUCCESS) {
        warn("heartbeat err=%s", ERRORDESC(err).c_str());
    }
    Freep(err);
    return;
}

error HttpHeartbeat::DoHeartbeat()
{
    error err = SUCCESS;

    std::string url = config->GetHeartbeatUrl();

    HttpUri uri;
    if ((err = uri.Initialize(url)) != SUCCESS) {
        return ERRORWRAP(err, "http uri parse hartbeart url failed. url=%s", url.c_str());
    }

    IPAddress* ip = NULL;
    std::string device_id = config->GetHeartbeatDeviceId();

    std::vector<IPAddress*>& ips = GetLocalIps();
    if (!ips.empty()) {
        ip = ips[config->GetStatsNetwork() % (int)ips.size()];
    }

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("device_id", JsonAny::Str(device_id.c_str()));
    obj->Set("ip", JsonAny::Str(ip->m_ip.c_str()));

    if (config->GetHeartbeatSummaries()) {
        JsonObject* summaries = JsonAny::Object();
        obj->Set("summaries", summaries);

        ApiDumpSummaries(summaries);
    }

    HttpClient http;
    if ((err = http.Initialize(uri.GetSchema(), uri.GetHost(), uri.GetPort())) != SUCCESS) {
        return ERRORWRAP(err, "init uri=%s", uri.GetUrl().c_str());
    }

    std::string req = obj->Dumps();
    IHttpMessage* msg = NULL;
    if ((err = http.Post(uri.GetPath(), req, &msg)) != SUCCESS) {
        return ERRORWRAP(err, "http post hartbeart uri failed. url=%s, request=%s", url.c_str(), req.c_str());
    }
    AutoFree(IHttpMessage, msg);

    std::string res;
    if ((err = msg->BodyReadAll(res)) != SUCCESS) {
        return ERRORWRAP(err, "read body");
    }

    return err;
}

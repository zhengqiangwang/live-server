#include "app_coworkers.h"
#include "app_config.h"
#include "consts.h"
#include "protocol_json.h"
#include "protocol_rtmp_stack.h"
#include "protocol_utility.h"
#include "utility.h"

CoWorkers* CoWorkers::_instance = NULL;

CoWorkers::CoWorkers()
{

}

CoWorkers::~CoWorkers()
{
    std::map<std::string, Request*>::iterator it;
    for (it = m_streams.begin(); it != m_streams.end(); ++it) {
        Request* r = it->second;
        Freep(r);
    }
    m_streams.clear();
}

CoWorkers *CoWorkers::Instance()
{
    if (!_instance) {
        _instance = new CoWorkers();
    }
    return _instance;
}

JsonAny *CoWorkers::Dumps(std::string vhost, std::string coworker, std::string app, std::string stream)
{
    Request* r = FindStreamInfo(vhost, app, stream);
    if (!r) {
        // TODO: FIXME: Find stream from our origin util return to the start point.
        return JsonAny::Null();
    }

    // The service port parsing from listen port.
    std::string listen_host;
    int listen_port = CONSTS_RTMP_DEFAULT_PORT;
    std::vector<std::string> listen_hostports = config->GetListens();
    if (!listen_hostports.empty()) {
        std::string list_hostport = listen_hostports.at(0);

        if (list_hostport.find(":") != std::string::npos) {
            ParseHostport(list_hostport, listen_host, listen_port);
        } else {
            listen_port = ::atoi(list_hostport.c_str());
        }
    }

    // The ip of server, we use the request coworker-host as ip, if listen host is localhost or loopback.
    // For example, the server may behind a NAT(192.x.x.x), while its ip is a docker ip(172.x.x.x),
    // we should use the NAT(192.x.x.x) address as it's the exposed ip.
    // @see https://github.com/ossrs/srs/issues/1501
    std::string service_ip;
    if (listen_host != CONSTS_LOCALHOST && listen_host != CONSTS_LOOPBACK && listen_host != CONSTS_LOOPBACK6) {
        service_ip = listen_host;
    }
    if (service_ip.empty()) {
        int coworker_port;
        std::string coworker_host = coworker;
        if (coworker.find(":") != std::string::npos) {
            ParseHostport(coworker, coworker_host, coworker_port);
        }

        service_ip = coworker_host;
    }
    if (service_ip.empty()) {
        service_ip = GetPublicInternetAddress();
    }

    // The backend API endpoint.
    std::string backend = config->GetHttpApiListen();
    if (backend.find(":") == std::string::npos) {
        backend = service_ip + ":" + backend;
    }

    // The routers to detect loop and identify path.
    JsonArray* routers = JsonAny::Array()->Append(JsonAny::Str(backend.c_str()));

    trace("Redirect vhost=%s, path=%s/%s to ip=%s, port=%d, api=%s",
        vhost.c_str(), app.c_str(), stream.c_str(), service_ip.c_str(), listen_port, backend.c_str());

    return JsonAny::Object()
        ->Set("ip", JsonAny::Str(service_ip.c_str()))
        ->Set("port", JsonAny::Integer(listen_port))
        ->Set("vhost", JsonAny::Str(r->m_vhost.c_str()))
        ->Set("api", JsonAny::Str(backend.c_str()))
        ->Set("routers", routers);
}

Request *CoWorkers::FindStreamInfo(std::string vhost, std::string app, std::string stream)
{
    // First, we should parse the vhost, if not exists, try default vhost instead.
    ConfDirective* conf = config->GetVhost(vhost, true);
    if (!conf) {
        return NULL;
    }

    // Get stream information from local cache.
    std::string url = GenerateStreamUrl(conf->Arg0(), app, stream);
    std::map<std::string, Request*>::iterator it = m_streams.find(url);
    if (it == m_streams.end()) {
        return NULL;
    }

    return it->second;
}

error CoWorkers::OnPublish(LiveSource *s, Request *r)
{
    error err = SUCCESS;

    std::string url = r->GetStreamUrl();

    // Delete the previous stream informations.
    std::map<std::string, Request*>::iterator it = m_streams.find(url);
    if (it != m_streams.end()) {
        Freep(it->second);
    }

    // Always use the latest one.
    m_streams[url] = r->Copy();

    return err;
}

void CoWorkers::OnUnpublish(LiveSource *s, Request *r)
{
    std::string url = r->GetStreamUrl();

    std::map<std::string, Request*>::iterator it = m_streams.find(url);
    if (it != m_streams.end()) {
        Freep(it->second);
        m_streams.erase(it);
    }
}

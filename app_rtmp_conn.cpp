#include "app_rtmp_conn.h"
#include "app_config.h"
#include "app_http_hooks.h"
#include "app_pithy_print.h"
#include "app_recv_thread.h"
#include "app_source.h"
#include "app_security.h"
#include "app_refer.h"
#include "app_statistic.h"
#include "app_http_conn.h"
#include "consts.h"
#include "log.h"
#include "protocol_amf0.h"
#include "protocol_utility.h"
#include "protocol_rtmp_msg_array.h"
#include "utility.h"
#include "app_server.h"
#include "app_utility.h"
#include "error.h"
#include "core_autofree.h"

// the timeout in srs_utime_t to wait encoder to republish
// if timeout, close the connection.
#define REPUBLISH_SEND_TIMEOUT (3 * UTIME_MINUTES)
// if timeout, close the connection.
#define REPUBLISH_RECV_TIMEOUT (3 * UTIME_MINUTES)

// the timeout in srs_utime_t to wait client data, when client paused
// if timeout, close the connection.
#define PAUSED_SEND_TIMEOUT (3 * UTIME_MINUTES)
// if timeout, close the connection.
#define PAUSED_RECV_TIMEOUT (3 * UTIME_MINUTES)

// when edge timeout, retry next.
#define EDGE_TOKEN_TRAVERSE_TIMEOUT (3 * UTIME_SECONDS)

SimpleRtmpClient::SimpleRtmpClient(std::string u, utime_t ctm, utime_t stm) : BasicRtmpClient(u, ctm, stm)
{

}

SimpleRtmpClient::~SimpleRtmpClient()
{

}

error SimpleRtmpClient::ConnectApp()
{
    std::vector<IPAddress*>& ips = GetLocalIps();
    Assert(config->GetStatsNetwork() < (int)ips.size());
    IPAddress* local_ip = ips[config->GetStatsNetwork()];

    bool debug_srs_upnode = config->GetDebugSrsUpnode(m_req->m_vhost);

    return DoConnectApp(local_ip->m_ip, debug_srs_upnode);
}

ClientInfo::ClientInfo()
{
    m_edge = false;
    m_req = new Request();
    m_res = new Response();
    m_type = RtmpConnUnknown;
}

ClientInfo::~ClientInfo()
{
    Freep(m_req);
    Freep(m_res);
}

RtmpConn::RtmpConn(Server *svr, netfd_t c, std::string cip, int cport)
{
    // Create a identify for this client.
    Context->SetId(Context->GenerateId());

    m_server = svr;

    m_stfd = c;
    m_skt = new TcpConnection(c);
    m_manager = svr;
    m_ip = cip;
    m_port = cport;
    m_createTime = u2ms(GetSystemTime());
//    m_spanMain = _srs_apm->dummy();
//    m_spanConnect = _srs_apm->dummy();
//    m_spanClient = _srs_apm->dummy();
    m_trd = new STCoroutine("rtmp", this, Context->GetId());

    m_kbps = new NetworkKbps();
    m_kbps->SetIo(m_skt, m_skt);
    m_delta = new NetworkDelta();
    m_delta->SetIo(m_skt, m_skt);

    m_rtmp = new RtmpServer(m_skt);
    m_refer = new Refer();
    m_security = new Security();
    m_duration = 0;
    m_wakable = NULL;

    m_mwSleep = PERF_MW_SLEEP;
    m_mwMsgs = 0;
    m_realtime = PERF_MIN_LATENCY_ENABLED;
    m_sendMinInterval = 0;
    m_tcpNodelay = false;
    m_info = new ClientInfo();

    m_publish1stpktTimeout = 0;
    m_publishNormalTimeout = 0;

    config->Subscribe(this);
}

RtmpConn::~RtmpConn()
{
    config->Unsubscribe(this);

    m_trd->Interrupt();
    // wakeup the handler which need to notice.
    if (m_wakable) {
        m_wakable->Wakeup();
    }
    Freep(m_trd);

    Freep(m_kbps);
    Freep(m_delta);
    Freep(m_skt);

    Freep(m_info);
    Freep(m_rtmp);
    Freep(m_refer);
    Freep(m_security);
//    Freep(m_spanMain);
//    Freep(m_spanConnect);
//    Freep(m_spanClient);
}

std::string RtmpConn::Desc()
{
    return "RtmpConn";
}

std::string Ipv4String(uint32_t rip)
{
    return Fmt("%d.%d.%d.%d", uint8_t(rip>>24), uint8_t(rip>>16), uint8_t(rip>>8), uint8_t(rip));
}

error RtmpConn::DoCycle()
{
    error err = SUCCESS;

    // We should keep the root span to alive util connection closed.
    // Note that we use producer and consumer span because RTMP connection is long polling connection.
    // Note that we also store this span in coroutine context, so that edge could load it.
//    Freep(m_spanMain);
//    m_spanMain = _srs_apm->span("rtmp")->set_kind(ApmKindServer)->attr("cip", m_ip)
//        ->attr("cid", Context->GetId().Cstr());

//    trace("RTMP client ip=%s:%d, fd=%d, trace=%s, span=%s", ip.c_str(), port, srs_netfd_fileno(stfd),
//        m_spanMain->format_trace_id(), m_spanMain->format_span_id()
//    );

    m_rtmp->SetRecvTimeout(CONSTS_RTMP_TIMEOUT);
    m_rtmp->SetSendTimeout(CONSTS_RTMP_TIMEOUT);

    if ((err = m_rtmp->Handshake()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp handshake");
    }

    uint32_t rip = m_rtmp->ProxyRealIp();
    std::string rips = Ipv4String(rip);
    if (rip > 0) {
        trace("RTMP proxy real client ip=%s", rips.c_str());
    }

    // Update the real IP of client, also set the HTTP fields.
//    m_spanMain->attr("rip", rip ? rips : ip)->attr("http.client_ip", rip ? rips : ip);

//    // The span for RTMP connecting to application.
//    Freep(m_spanConnect);
//    m_spanConnect = _srs_apm->span("connect")->as_child(m_spanMain);

    Request* req = m_info->m_req;
    if ((err = m_rtmp->ConnectApp(req)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp connect tcUrl");
    }

    // set client ip to request.
    req->m_ip = m_ip;

    trace("connect app, tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%d, app=%s, args=%s",
        req->m_tcUrl.c_str(), req->m_pageUrl.c_str(), req->m_swfUrl.c_str(),
        req->m_schema.c_str(), req->m_vhost.c_str(), req->m_port,
        req->m_app.c_str(), (req->m_args? "(obj)":"null"));

    // show client identity
    if(req->m_args) {
        std::string srs_version;
        std::string srs_server_ip;
        int srs_pid = 0;
        int srs_id = 0;

        Amf0Any* prop = NULL;
        if ((prop = req->m_args->EnsurePropertyString("srs_version")) != NULL) {
            srs_version = prop->ToStr();
        }
        if ((prop = req->m_args->EnsurePropertyString("srs_server_ip")) != NULL) {
            srs_server_ip = prop->ToStr();
        }
        if ((prop = req->m_args->EnsurePropertyNumber("srs_pid")) != NULL) {
            srs_pid = (int)prop->ToNumber();
        }
        if ((prop = req->m_args->EnsurePropertyNumber("srs_id")) != NULL) {
            srs_id = (int)prop->ToNumber();
        }

        if (srs_pid > 0) {
            trace("edge-srs ip=%s, version=%s, pid=%d, id=%d",
                srs_server_ip.c_str(), srs_version.c_str(), srs_pid, srs_id);
        }

        // Load the span from the AMF0 object propagator.
        // Note that we will update the trace id, so please make sure no spans are ended before this.
//        _srs_apm->extract(m_spanMain, req->m_args);
    }

    if ((err = ServiceCycle()) != SUCCESS) {
        err = ERRORWRAP(err, "service cycle");
    }

    error r0 = SUCCESS;
    if ((r0 = OnDisconnect()) != SUCCESS) {
        err = ERRORWRAP(err, "on disconnect %s", ERRORDESC(r0).c_str());
        Freep(r0);
    }

    // If client is redirect to other servers, we already logged the event.
    if (ERRORCODE(err) == ERROR_CONTROL_REDIRECT) {
        ERRORRESET(err);
    }

    return err;
}

error RtmpConn::OnReloadVhostRemoved(std::string vhost)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (req->m_vhost != vhost) {
        return err;
    }

    // if the vhost connected is removed, disconnect the client.
    trace("vhost %s removed/disabled, close client url=%s",
              vhost.c_str(), req->GetStreamUrl().c_str());

    // should never close the fd in another thread,
    // one fd should managed by one thread, we should use interrupt instead.
    // so we just ignore the vhost enabled event.
    //srs_close_stfd(stfd);

    return err;
}

error RtmpConn::OnReloadVhostPlay(std::string vhost)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (req->m_vhost != vhost) {
        return err;
    }

    // send_min_interval
    if (true) {
        utime_t v = config->GetSendMinInterval(vhost);
        if (v != m_sendMinInterval) {
            trace("apply smi %d=>%d ms", u2msi(m_sendMinInterval), u2msi(v));
            m_sendMinInterval = v;
        }
    }

    m_mwMsgs = config->GetMwMsgs(req->m_vhost, m_realtime);
    m_mwSleep = config->GetMwSleep(req->m_vhost);
    m_skt->SetSocketBuffer(m_mwSleep);

    return err;
}

error RtmpConn::OnreloadVhostTcpNodelay(std::string vhost)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (req->m_vhost != vhost) {
        return err;
    }

    SetSockOptions();

    return err;
}

error RtmpConn::OnReloadVhostRealtime(std::string vhost)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (req->m_vhost != vhost) {
        return err;
    }

    bool realtime_enabled = config->GetRealtimeEnabled(req->m_vhost);
    if (realtime_enabled != m_realtime) {
        trace("realtime changed %d=>%d", m_realtime, realtime_enabled);
        m_realtime = realtime_enabled;
    }

    m_mwMsgs = config->GetMwMsgs(req->m_vhost, m_realtime);
    m_mwSleep = config->GetMwSleep(req->m_vhost);
    m_skt->SetSocketBuffer(m_mwSleep);

    return err;
}

error RtmpConn::OnReloadVhostPublish(std::string vhost)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (req->m_vhost != vhost) {
        return err;
    }

    st_utime_t p1stpt = config->GetPublish1StpktTimeout(req->m_vhost);
    if (p1stpt != m_publish1stpktTimeout) {
        trace("p1stpt changed %d=>%d", u2msi(m_publish1stpktTimeout), u2msi(p1stpt));
        m_publish1stpktTimeout = p1stpt;
    }

    st_utime_t pnt = config->GetPublishNormalTimeout(req->m_vhost);
    if (pnt != m_publishNormalTimeout) {
        trace("pnt changed %d=>%d", u2msi(m_publishNormalTimeout), u2msi(pnt));
        m_publishNormalTimeout = pnt;
    }

    return err;
}

IKbpsDelta *RtmpConn::Delta()
{
    return m_delta;
}

error RtmpConn::ServiceCycle()
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    int out_ack_size = config->GetOutAckSize(req->m_vhost);
    if (out_ack_size && (err = m_rtmp->SetWindowAckSize(out_ack_size)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: set out window ack size");
    }

    int in_ack_size = config->GetInAckSize(req->m_vhost);
    if (in_ack_size && (err = m_rtmp->SetInWindowAckSize(in_ack_size)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: set in window ack size");
    }

    if ((err = m_rtmp->SetPeerBandwidth((int)(2.5 * 1000 * 1000), 2)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: set peer bandwidth");
    }

    // get the ip which client connected.
    std::string local_ip = GetLocalIp(NetfdFileno(m_stfd));

    // set chunk size to larger.
    // set the chunk size before any larger response greater than 128,
    // to make OBS happy, @see https://github.com/ossrs/srs/issues/454
    int chunk_size = config->GetChunkSize(req->m_vhost);
    if ((err = m_rtmp->SetChunkSize(chunk_size)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: set chunk size %d", chunk_size);
    }

    // response the client connect ok.
    if ((err = m_rtmp->ResponseConnectApp(req, local_ip.c_str())) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: response connect app");
    }

    // Must be a connecting application span.
//    m_spanConnect->End();

    if ((err = m_rtmp->OnBwDone()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: on bw down");
    }

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: thread quit");
        }

        err = StreamServiceCycle();

        // stream service must terminated with error, never success.
        // when terminated with success, it's user required to stop.
        // TODO: FIXME: Support RTMP client timeout, https://github.com/ossrs/srs/issues/1134
        if (err == SUCCESS) {
            continue;
        }

        // when not system control error, fatal error, return.
        if (!IsSystemControlError(err)) {
            return ERRORWRAP(err, "rtmp: stream service");
        }

        // for republish, continue service
        if (ERRORCODE(err) == ERROR_CONTROL_REPUBLISH) {
            // set timeout to a larger value, wait for encoder to republish.
            m_rtmp->SetSendTimeout(REPUBLISH_RECV_TIMEOUT);
            m_rtmp->SetRecvTimeout(REPUBLISH_SEND_TIMEOUT);

            info("rtmp: retry for republish");
            Freep(err);
            continue;
        }

        // for "some" system control error,
        // logical accept and retry stream service.
        if (ERRORCODE(err) == ERROR_CONTROL_RTMP_CLOSE) {
            // TODO: FIXME: use ping message to anti-death of socket.
            // set timeout to a larger value, for user paused.
            m_rtmp->SetRecvTimeout(PAUSED_RECV_TIMEOUT);
            m_rtmp->SetSendTimeout(PAUSED_SEND_TIMEOUT);

            trace("rtmp: retry for close");
            Freep(err);
            continue;
        }

        // for other system control message, fatal error.
        return ERRORWRAP(err, "rtmp: reject");
    }

    return err;
}

error RtmpConn::StreamServiceCycle()
{
    error err = SUCCESS;

    Request* req = m_info->m_req;
    if ((err = m_rtmp->IdentifyClient(m_info->m_res->m_streamId, m_info->m_type, req->m_stream, req->m_duration)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: identify client");
    }

    DiscoveryTcUrl(req->m_tcUrl, req->m_schema, req->m_host, req->m_vhost, req->m_app, req->m_stream, req->m_port, req->m_param);

    // guess stream name
    if (req->m_stream.empty()) {
        std::string app = req->m_app, param = req->m_param;
        GuessStreamByApp(req->m_app, req->m_param, req->m_stream);
        trace("Guessing by app=%s, param=%s to app=%s, param=%s, stream=%s", app.c_str(), param.c_str(), req->m_app.c_str(), req->m_param.c_str(), req->m_stream.c_str());
    }

    req->Strip();
    trace("client identified, type=%s, vhost=%s, app=%s, stream=%s, param=%s, duration=%dms",
        ClientTypeString(m_info->m_type).c_str(), req->m_vhost.c_str(), req->m_app.c_str(), req->m_stream.c_str(), req->m_param.c_str(), u2msi(req->m_duration));

    // Start APM only when client is identified, because it might republish.
//    Freep(m_spanClient);
//    m_spanClient = _srs_apm->span("client")->as_child(m_spanConnect)->attr("type", ClientTypeString(m_info->m_type))
//        ->attr("url", req->GetStreamUrl())->attr("http.url", req->GetStreamUrl());
//    // We store the span to coroutine context, for edge to load it.
//    _srs_apm->store(m_spanClient);

    // discovery vhost, resolve the vhost from config
    ConfDirective* parsed_vhost = config->GetVhost(req->m_vhost);
    if (parsed_vhost) {
        req->m_vhost = parsed_vhost->Arg0();
    }
//    m_spanClient->attr("vhost", req->vhost)->attr("http.host", req->host)->attr("http.server_name", req->vhost)
//        ->attr("http.target", srs_fmt("/%s/%s", req->app.c_str(), req->stream.c_str()));

    if (req->m_schema.empty() || req->m_vhost.empty() || req->m_port == 0 || req->m_app.empty()) {
        return ERRORNEW(ERROR_RTMP_REQ_TCURL, "discovery tcUrl failed, tcUrl=%s, schema=%s, vhost=%s, port=%d, app=%s",
            req->m_tcUrl.c_str(), req->m_schema.c_str(), req->m_vhost.c_str(), req->m_port, req->m_app.c_str());
    }

    // check vhost, allow default vhost.
    if ((err = CheckVhost(true)) != SUCCESS) {
        return ERRORWRAP(err, "check vhost");
    }

    trace("connected stream, tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%d, app=%s, stream=%s, param=%s, args=%s",
        req->m_tcUrl.c_str(), req->m_pageUrl.c_str(), req->m_swfUrl.c_str(), req->m_schema.c_str(), req->m_vhost.c_str(), req->m_port,
        req->m_app.c_str(), req->m_stream.c_str(), req->m_param.c_str(), (req->m_args? "(obj)":"null"));

    // do token traverse before serve it.
    // @see https://github.com/ossrs/srs/pull/239
    if (true) {
        m_info->m_edge = config->GetVhostIsEdge(req->m_vhost);
        bool edge_traverse = config->GetVhostEdgeTokenTraverse(req->m_vhost);
        if (m_info->m_edge && edge_traverse) {
            if ((err = CheckEdgeTokenTraverseAuth()) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: check token traverse");
            }
        }
    }

    // security check
    if ((err = m_security->Check(m_info->m_type, m_ip, req)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: security check");
    }

    // Never allow the empty stream name, for HLS may write to a file with empty name.
    // @see https://github.com/ossrs/srs/issues/834
    if (req->m_stream.empty()) {
        return ERRORNEW(ERROR_RTMP_STREAM_NAME_EMPTY, "rtmp: empty stream");
    }

    // client is identified, set the timeout to service timeout.
    m_rtmp->SetRecvTimeout(CONSTS_RTMP_TIMEOUT);
    m_rtmp->SetSendTimeout(CONSTS_RTMP_TIMEOUT);

    // find a source to serve.
    LiveSource* source = NULL;
    if ((err = sources->FetchOrCreate(req, m_server, &source)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: fetch source");
    }
    Assert(source != NULL);

    bool enabled_cache = config->GetGopCache(req->m_vhost);
    trace("source url=%s, ip=%s, cache=%d, is_edge=%d, source_id=%s/%s",
        req->GetStreamUrl().c_str(), m_ip.c_str(), enabled_cache, m_info->m_edge, source->SourceId().Cstr(), source->PreSourceId().Cstr());
    source->SetCache(enabled_cache);

    switch (m_info->m_type) {
        case RtmpConnPlay: { //rtmp pull stream
            // response connection start play
            if ((err = m_rtmp->StartPlay(m_info->m_res->m_streamId)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: start play");
            }

            // We must do stat the client before hooks, because hooks depends on it.
            Statistic* stat = Statistic::Instance();
            if ((err = stat->OnClient(Context->GetId().Cstr(), req, this, m_info->m_type)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: stat client");
            }

            // We must do hook after stat, because depends on it.
            if ((err = HttpHooksOnPlay()) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: callback on play");
            }

//            // Must be a client span.
//            m_spanClient->set_name("play")->end();
//            // We end the connection span because it's a producer and only trace the established.
//            m_spanMain->end();

            err = Playing(source);
            HttpHooksOnStop();

            return err;
        }
        case RtmpConnFMLEPublish: {//rtmp push stream
            if ((err = m_rtmp->StartFmlePublish(m_info->m_res->m_streamId)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: start FMLE publish");
            }

//            // Must be a client span.
//            m_spanClient->set_name("publish")->end();
//            // We end the connection span because it's a producer and only trace the established.
//            m_spanMain->end();

            return Publishing(source);
        }
        case RtmpConnHaivisionPublish: {
            if ((err = m_rtmp->StartHaivisionPublish(m_info->m_res->m_streamId)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: start HAIVISION publish");
            }

//            // Must be a client span.
//            m_spanClient->set_name("publish")->end();
//            // We end the connection span because it's a producer and only trace the established.
//            m_spanMain->end();

            return Publishing(source);
        }
        case RtmpConnFlashPublish: {
            if ((err = m_rtmp->StartFlashPublish(m_info->m_res->m_streamId)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: start FLASH publish");
            }

//            // Must be a client span.
//            m_spanClient->set_name("publish")->end();
//            // We end the connection span because it's a producer and only trace the established.
//            m_spanMain->end();

            return Publishing(source);
        }
        default: {
            return ERRORNEW(ERROR_SYSTEM_CLIENT_INVALID, "rtmp: unknown client type=%d", m_info->m_type);
        }
    }

    return err;
}

error RtmpConn::CheckVhost(bool try_default_vhost)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;
    Assert(req != NULL);

    ConfDirective* vhost = config->GetVhost(req->m_vhost, try_default_vhost);
    if (vhost == NULL) {
        return ERRORNEW(ERROR_RTMP_VHOST_NOT_FOUND, "rtmp: no vhost %s", req->m_vhost.c_str());
    }

    if (!config->GetVhostEnabled(req->m_vhost)) {
        return ERRORNEW(ERROR_RTMP_VHOST_NOT_FOUND, "rtmp: vhost %s disabled", req->m_vhost.c_str());
    }

    if (req->m_vhost != vhost->Arg0()) {
        trace("vhost change from %s to %s", req->m_vhost.c_str(), vhost->Arg0().c_str());
        req->m_vhost = vhost->Arg0();
    }

    if (config->GetReferEnabled(req->m_vhost)) {
        if ((err = m_refer->Check(req->m_pageUrl, config->GetReferAll(req->m_vhost))) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: referer check");
        }
    }

    if ((err = HttpHooksOnConnect()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: callback on connect");
    }

    return err;
}

error RtmpConn::Playing(LiveSource *source)
{
    error err = SUCCESS;

    // Check page referer of player.
    Request* req = m_info->m_req;
    if (config->GetReferEnabled(req->m_vhost)) {
        if ((err = m_refer->Check(req->m_pageUrl, config->GetReferPlay(req->m_vhost))) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: referer check");
        }
    }

    // When origin cluster enabled, try to redirect to the origin which is active.
    // A active origin is a server which is delivering stream.
    if (!m_info->m_edge && config->GetVhostOriginCluster(req->m_vhost) && source->Inactive()) {
        std::vector<std::string> coworkers = config->GetVhostCoworkers(req->m_vhost);
        for (int i = 0; i < (int)coworkers.size(); i++) {
            // TODO: FIXME: User may config the server itself as coworker, we must identify and ignore it.
            std::string host; int port = 0; std::string coworker = coworkers.at(i);

            std::string url = "http://" + coworker + "/api/v1/clusters?"
                + "vhost=" + req->m_vhost + "&ip=" + req->m_host + "&app=" + req->m_app + "&stream=" + req->m_stream
                + "&coworker=" + coworker;
            if ((err = HttpHooks::DiscoverCoWorkers(url, host, port)) != SUCCESS) {
                // If failed to discovery stream in this coworker, we should request the next one util the last.
                // @see https://github.com/ossrs/srs/issues/1223
                if (i < (int)coworkers.size() - 1) {
                    continue;
                }
                return ERRORWRAP(err, "discover coworkers, url=%s", url.c_str());
            }

            std::string rurl = GenerateRtmpUrl(host, port, req->m_host, req->m_vhost, req->m_app, req->m_stream, req->m_param);
            trace("rtmp: redirect in cluster, from=%s:%d, target=%s:%d, url=%s, rurl=%s",
                req->m_host.c_str(), req->m_port, host.c_str(), port, url.c_str(), rurl.c_str());

            // Ignore if host or port is invalid.
            if (host.empty() || port == 0) {
                continue;
            }

            bool accepted = false;
            if ((err = m_rtmp->Redirect(req, rurl, accepted)) != SUCCESS) {
                ERRORRESET(err);
            } else {
                return ERRORNEW(ERROR_CONTROL_REDIRECT, "redirected");
            }
        }

        return ERRORNEW(ERROR_OCLUSTER_REDIRECT, "no origin");
    }

    // Set the socket options for transport.
    SetSockOptions();

    // Create a consumer of source.
    LiveConsumer* consumer = NULL;
    AutoFree(LiveConsumer, consumer);
    if ((err = source->CreateConsumer(consumer)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: create consumer");
    }
    if ((err = source->ConsumerDumps(consumer)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: dumps consumer");
    }

    // Use receiving thread to receive packets from peer.
    QueueRecvThread trd(consumer, m_rtmp, PERF_MW_SLEEP, Context->GetId());

    if ((err = trd.Start()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: start receive thread");
    }

    // Deliver packets to peer.
    m_wakable = consumer;
    err = DoPlaying(source, consumer, &trd);
    m_wakable = NULL;

    trd.Stop();

    // Drop all packets in receiving thread.
    if (!trd.Empty()) {
        warn("drop the received %d messages", trd.Size());
    }

    return err;
}

error RtmpConn::DoPlaying(LiveSource *source, LiveConsumer *consumer, QueueRecvThread *rtrd)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;
    Assert(req);
    Assert(consumer);

    // initialize other components
    PithyPrint* pprint = PithyPrint::CreateRtmpPlay();
    AutoFree(PithyPrint, pprint);

    MessageArray msgs(PERF_MW_MSGS);
    bool user_specified_duration_to_stop = (req->m_duration > 0);
    int64_t starttime = -1;

    // setup the realtime.
    m_realtime = config->GetRealtimeEnabled(req->m_vhost);
    // setup the mw config.
    // when mw_sleep changed, resize the socket send buffer.
    m_mwMsgs = config->GetMwMsgs(req->m_vhost, m_realtime);
    m_mwSleep = config->GetMwSleep(req->m_vhost);
    m_skt->SetSocketBuffer(m_mwSleep);
    // initialize the send_min_interval
    m_sendMinInterval = config->GetSendMinInterval(req->m_vhost);

    trace("start play smi=%dms, mw_sleep=%d, mw_msgs=%d, realtime=%d, tcp_nodelay=%d",
        u2msi(m_sendMinInterval), u2msi(m_mwSleep), m_mwMsgs, m_realtime, m_tcpNodelay);

//    IApmSpan* span = _srs_apm->span("play-cycle")->set_kind(ApmKindProducer)->as_child(m_spanClient)
//        ->attr("realtime", Fmt("%d", m_realtime))->end();
//    AutoFree(IApmSpan, span);

    while (true) {
        // when source is set to expired, disconnect it.
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: thread quit");
        }

        // collect elapse for pithy print.
        pprint->Elapse();

        // to use isolate thread to recv, can improve about 33% performance.
        while (!rtrd->Empty()) {
            CommonMessage* msg = rtrd->Pump();
            if ((err = ProcessPlayControlMsg(consumer, msg)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: play control message");
            }
        }

        // quit when recv thread error.
        if ((err = rtrd->ErrorCode()) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: recv thread");
        }

#ifdef SRS_PERF_QUEUE_COND_WAIT
        // wait for message to incoming.
        // @see https://github.com/ossrs/srs/issues/257
        consumer->wait(mw_msgs, mw_sleep);
#endif

        // get messages from consumer.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        // @remark when enable send_min_interval, only fetch one message a time.
        int count = (m_sendMinInterval > 0)? 1 : 0;
        if ((err = consumer->DumpPackets(&msgs, count)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: consumer dump packets");
        }

        // reportable
        if (pprint->CanPrint()) {
            m_kbps->Sample();
            trace("-> " CONSTS_LOG_PLAY " time=%d, msgs=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d, mw=%d/%d",
                (int)pprint->Age(), count, m_kbps->GetSendKbps(), m_kbps->GetSendKbps30s(), m_kbps->GetSendKbps5m(),
                m_kbps->GetRecvKbps(), m_kbps->GetRecvKbps30s(), m_kbps->GetRecvKbps5m(), u2msi(m_mwSleep), m_mwMsgs);

            // TODO: Do not use pithy print for frame span.
//            IApmSpan* sample = _srs_apm->span("play-frame")->set_kind(ApmKindConsumer)->as_child(span)
//                ->attr("msgs", Fmt("%d", count))->attr("kbps", Fmt("%d", m_kbps->GetSendKbps30s()));
//            Freep(sample);
        }

        if (count <= 0) {
#ifndef SRS_PERF_QUEUE_COND_WAIT
            Usleep(m_mwSleep);
#endif
            // ignore when nothing got.
            continue;
        }

        // only when user specifies the duration,
        // we start to collect the durations for each message.
        if (user_specified_duration_to_stop) {
            for (int i = 0; i < count; i++) {
                SharedPtrMessage* msg = msgs.m_msgs[i];

                // foreach msg, collect the duration.
                // @remark: never use msg when sent it, for the protocol sdk will free it.
                if (starttime < 0 || starttime > msg->m_timestamp) {
                    starttime = msg->m_timestamp;
                }
                m_duration += (msg->m_timestamp - starttime) * UTIME_MILLISECONDS;
                starttime = msg->m_timestamp;
            }
        }

        // sendout messages, all messages are freed by send_and_free_messages().
        // no need to assert msg, for the rtmp will assert it.
        if (count > 0 && (err = m_rtmp->SendAndFreeMessages(msgs.m_msgs, count, m_info->m_res->m_streamId)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: send %d messages", count);
        }

        // if duration specified, and exceed it, stop play live.
        // @see: https://github.com/ossrs/srs/issues/45
        if (user_specified_duration_to_stop) {
            if (m_duration >= req->m_duration) {
                return ERRORNEW(ERROR_RTMP_DURATION_EXCEED, "rtmp: time %d up %d", u2msi(m_duration), u2msi(req->m_duration));
            }
        }

        // apply the minimal interval for delivery stream in srs_utime_t.
        if (m_sendMinInterval > 0) {
            Usleep(m_sendMinInterval);
        }

        // Yield to another coroutines.
        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777437476
        ThreadYield();
    }

    return err;
}

error RtmpConn::Publishing(LiveSource *source)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (config->GetReferEnabled(req->m_vhost)) {
        if ((err = m_refer->Check(req->m_pageUrl, config->GetReferPublish(req->m_vhost))) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: referer check");
        }
    }

    // We must do stat the client before hooks, because hooks depends on it.
    Statistic* stat = Statistic::Instance();
    if ((err = stat->OnClient(Context->GetId().Cstr(), req, this, m_info->m_type)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: stat client");
    }

    // We must do hook after stat, because depends on it.
    if ((err = HttpHooksOnPublish()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: callback on publish");
    }

    // TODO: FIXME: Should refine the state of publishing.
    if ((err = AcquirePublish(source)) == SUCCESS) {
        // use isolate thread to recv,
        // @see: https://github.com/ossrs/srs/issues/237
        PublishRecvThread rtrd(m_rtmp, req, NetfdFileno(m_stfd), 0, this, source, Context->GetId());
        err = DoPublishing(source, &rtrd);
        rtrd.Stop();
    }

    // whatever the acquire publish, always release publish.
    // when the acquire error in the midlle-way, the publish state changed,
    // but failed, so we must cleanup it.
    // @see https://github.com/ossrs/srs/issues/474
    // @remark when stream is busy, should never release it.
    if (ERRORCODE(err) != ERROR_SYSTEM_STREAM_BUSY) {
        ReleasePublish(source);
    }

    HttpHooksOnUnpublish();

    return err;
}

error RtmpConn::DoPublishing(LiveSource *source, PublishRecvThread *rtrd)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;
    PithyPrint* pprint = PithyPrint::CreateRtmpPublish();
    AutoFree(PithyPrint, pprint);

    // start isolate recv thread.
    // TODO: FIXME: Pass the callback here.
    if ((err = rtrd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: receive thread");
    }

    // initialize the publish timeout.
    m_publish1stpktTimeout = config->GetPublish1StpktTimeout(req->m_vhost);
    m_publishNormalTimeout = config->GetPublishNormalTimeout(req->m_vhost);

    // set the sock options.
    SetSockOptions();

    if (true) {
        bool mr = config->GetMrEnabled(req->m_vhost);
        utime_t mr_sleep = config->GetMrSleep(req->m_vhost);
        trace("start publish mr=%d/%d, p1stpt=%d, pnt=%d, tcp_nodelay=%d", mr, u2msi(mr_sleep), u2msi(m_publish1stpktTimeout), u2msi(m_publishNormalTimeout), m_tcpNodelay);
    }

//    IApmSpan* span = _srs_apm->span("publish-cycle")->set_kind(ApmKindProducer)->as_child(m_spanClient)
//        ->attr("timeout", Fmt("%d", u2msi(m_publishNormalTimeout)))->end();
//    AutoFree(IApmSpan, span);

    int64_t nb_msgs = 0;
    uint64_t nb_frames = 0;
    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: thread quit");
        }

        pprint->Elapse();

        // cond wait for timeout.
        if (nb_msgs == 0) {
            // when not got msgs, wait for a larger timeout.
            // @see https://github.com/ossrs/srs/issues/441
            rtrd->Wait(m_publish1stpktTimeout);
        } else {
            rtrd->Wait(m_publishNormalTimeout);
        }

        // check the thread error code.
        if ((err = rtrd->ErrorCode()) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: receive thread");
        }

        // when not got any messages, timeout.
        if (rtrd->NbMsgs() <= nb_msgs) {
            return ERRORNEW(ERROR_SOCKET_TIMEOUT, "rtmp: publish timeout %dms, nb_msgs=%d",
                nb_msgs? u2msi(m_publishNormalTimeout) : u2msi(m_publish1stpktTimeout), (int)nb_msgs);
        }
        nb_msgs = rtrd->NbMsgs();

        // Update the stat for video fps.
        // @remark https://github.com/ossrs/srs/issues/851
        Statistic* stat = Statistic::Instance();
        if ((err = stat->OnVideoFrames(req, (int)(rtrd->NbVideoFrames() - nb_frames))) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: stat video frames");
        }
        nb_frames = rtrd->NbVideoFrames();

        // reportable
        if (pprint->CanPrint()) {
            m_kbps->Sample();
            bool mr = config->GetMrEnabled(req->m_vhost);
            utime_t mr_sleep = config->GetMrSleep(req->m_vhost);
            trace("<- " CONSTS_LOG_CLIENT_PUBLISH " time=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d, mr=%d/%d, p1stpt=%d, pnt=%d",
                (int)pprint->Age(), m_kbps->GetSendKbps(), m_kbps->GetSendKbps30s(), m_kbps->GetSendKbps5m(),
                m_kbps->GetRecvKbps(), m_kbps->GetRecvKbps30s(), m_kbps->GetRecvKbps5m(), mr, u2msi(mr_sleep),
                u2msi(m_publish1stpktTimeout), u2msi(m_publishNormalTimeout));

            // TODO: Do not use pithy print for frame span.
//            IApmSpan* sample = _srs_apm->span("publish-frame")->set_kind(ApmKindConsumer)->as_child(span)
//                ->attr("msgs", srs_fmt("%" PRId64, nb_frames))->attr("kbps", Fmt("%d", m_kbps->GetRecvKbps30s()));
//            Freep(sample);

        }
    }

    return err;
}

error RtmpConn::AcquirePublish(LiveSource *source)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    // Check whether RTMP stream is busy.
    if (!source->CanPublish(m_info->m_edge)) {
        return ERRORNEW(ERROR_SYSTEM_STREAM_BUSY, "rtmp: stream %s is busy", req->GetStreamUrl().c_str());
    }

    // Check whether RTC stream is busy.
#ifdef SRS_RTC
    SrsRtcSource *rtc = NULL;
    bool rtc_server_enabled = config->get_rtc_server_enabled();
    bool rtc_enabled = config->get_rtc_enabled(req->vhost);
    if (rtc_server_enabled && rtc_enabled && !info->edge) {
        if ((err = _srs_rtc_sources->fetch_or_create(req, &rtc)) != SUCCESS) {
            return ERRORWRAP(err, "create source");
        }

        if (!rtc->can_publish()) {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtc stream %s busy", req->get_stream_url().c_str());
        }
    }
#endif

    // Bridge to RTC streaming.
#if defined(SRS_RTC) && defined(SRS_FFMPEG_FIT)
    if (rtc) {
        SrsRtcFromRtmpBridge *bridge = new SrsRtcFromRtmpBridge(rtc);
        if ((err = bridge->initialize(req)) != SUCCESS) {
            Freep(bridge);
            return ERRORWRAP(err, "bridge init");
        }

        source->set_bridge(bridge);
    }
#endif

    // Start publisher now.
    if (m_info->m_edge) {
        err = source->OnEdgeStartPublish();
    } else {
        err = source->OnPublish();
    }

    return err;
}

void RtmpConn::ReleasePublish(LiveSource *source)
{
    // when edge, notice edge to change state.
    // when origin, notice all service to unpublish.
    if (m_info->m_edge) {
        source->OnEdgeProxyUnpublish();
    } else {
        source->OnUnpublish();
    }
}

error RtmpConn::HandlePublishMessage(LiveSource *source, CommonMessage *msg)
{
    error err = SUCCESS;

    // process publish event.
    if (msg->m_header.IsAmf0Command() || msg->m_header.IsAmf3Command()) {
        Packet* pkt = NULL;
        if ((err = m_rtmp->DecodeMessage(msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: decode message");
        }
        AutoFree(Packet, pkt);

        // for flash, any packet is republish.
        if (m_info->m_type == RtmpConnFlashPublish) {
            // flash unpublish.
            // TODO: maybe need to support republish.
            trace("flash flash publish finished.");
            return ERRORNEW(ERROR_CONTROL_REPUBLISH, "rtmp: republish");
        }

        // for fmle, drop others except the fmle start packet.
        if (dynamic_cast<FMLEStartPacket*>(pkt)) {
            FMLEStartPacket* unpublish = dynamic_cast<FMLEStartPacket*>(pkt);
            if ((err = m_rtmp->FmleUnpublish(m_info->m_res->m_streamId, unpublish->m_transactionId)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: republish");
            }
            return ERRORNEW(ERROR_CONTROL_REPUBLISH, "rtmp: republish");
        }

        trace("fmle ignore AMF0/AMF3 command message.");
        return err;
    }

    // video, audio, data message
    if ((err = ProcessPublishMessage(source, msg)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: consume message");
    }

    return err;
}

error RtmpConn::ProcessPublishMessage(LiveSource *source, CommonMessage *msg)
{
    error err = SUCCESS;

    // for edge, directly proxy message to origin.
    if (m_info->m_edge) {
        if ((err = source->OnEdgeProxyPublish(msg)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: proxy publish");
        }
        return err;
    }

    // process audio packet
    if (msg->m_header.IsAudio()) {
        if ((err = source->OnAudio(msg)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: consume audio");
        }
        return err;
    }
    // process video packet
    if (msg->m_header.IsVideo()) {
        if ((err = source->OnVideo(msg)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: consume video");
        }
        return err;
    }

    // process aggregate packet
    if (msg->m_header.IsAggregate()) {
        if ((err = source->OnAggregate(msg)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: consume aggregate");
        }
        return err;
    }

    // process onMetaData
    if (msg->m_header.IsAmf0Data() || msg->m_header.IsAmf3Data()) {
        Packet* pkt = NULL;
        if ((err = m_rtmp->DecodeMessage(msg, &pkt)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: decode message");
        }
        AutoFree(Packet, pkt);

        if (dynamic_cast<OnMetaDataPacket*>(pkt)) {
            OnMetaDataPacket* metadata = dynamic_cast<OnMetaDataPacket*>(pkt);
            if ((err = source->OnMetaData(msg, metadata)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: consume metadata");
            }
            return err;
        }
        return err;
    }

    return err;
}

error RtmpConn::ProcessPlayControlMsg(LiveConsumer *consumer, CommonMessage *msg)
{
    error err = SUCCESS;

    if (!msg) {
        return err;
    }
    AutoFree(CommonMessage, msg);

    if (!msg->m_header.IsAmf0Command() && !msg->m_header.IsAmf3Command()) {
        return err;
    }

    Packet* pkt = NULL;
    if ((err = m_rtmp->DecodeMessage(msg, &pkt)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: decode message");
    }
    AutoFree(Packet, pkt);

    // for jwplayer/flowplayer, which send close as pause message.
    CloseStreamPacket* close = dynamic_cast<CloseStreamPacket*>(pkt);
    if (close) {
        return ERRORNEW(ERROR_CONTROL_RTMP_CLOSE, "rtmp: close stream");
    }

    // call msg,
    // support response null first,
    // TODO: FIXME: response in right way, or forward in edge mode.
    CallPacket* call = dynamic_cast<CallPacket*>(pkt);
    if (call) {
        // only response it when transaction id not zero,
        // for the zero means donot need response.
        if (call->m_transactionId > 0) {
            CallResPacket* res = new CallResPacket(call->m_transactionId);
            res->m_commandObject = Amf0Any::Null();
            res->m_response = Amf0Any::Null();
            if ((err = m_rtmp->SendAndFreePacket(res, 0)) != SUCCESS) {
                return ERRORWRAP(err, "rtmp: send packets");
            }
        }
        return err;
    }

    // pause
    PausePacket* pause = dynamic_cast<PausePacket*>(pkt);
    if (pause) {
        if ((err = m_rtmp->OnPlayClientPause(m_info->m_res->m_streamId, pause->m_isPause)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: pause");
        }
        if ((err = consumer->OnPlayClientPause(pause->m_isPause)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp: pause");
        }
        return err;
    }

    // other msg.
    return err;
}

void RtmpConn::SetSockOptions()
{
    Request* req = m_info->m_req;

    bool nvalue = config->GetTcpNodelay(req->m_vhost);
    if (nvalue != m_tcpNodelay) {
        m_tcpNodelay = nvalue;

        error err = m_skt->SetTcpNodelay(m_tcpNodelay);
        if (err != SUCCESS) {
            warn("ignore err %s", ERRORDESC(err).c_str());
            Freep(err);
        }
    }
}

error RtmpConn::CheckEdgeTokenTraverseAuth()
{
    error err = SUCCESS;

    Request* req = m_info->m_req;
    Assert(req);

    std::vector<std::string> args = config->GetVhostEdgeOrigin(req->m_vhost)->m_args;
    if (args.empty()) {
        return err;
    }

    for (int i = 0; i < (int)args.size(); i++) {
        std::string hostport = args.at(i);

        // select the origin.
        std::string server;
        int port = CONSTS_RTMP_DEFAULT_PORT;
        ParseHostport(hostport, server, port);

        TcpClient* transport = new TcpClient(server, port, EDGE_TOKEN_TRAVERSE_TIMEOUT);
        AutoFree(TcpClient, transport);

        if ((err = transport->Connect()) != SUCCESS) {
            warn("Illegal edge token, tcUrl=%s, %s", req->m_tcUrl.c_str(), ERRORDESC(err).c_str());
            Freep(err);
            continue;
        }

        RtmpClient* client = new RtmpClient(transport);
        AutoFree(RtmpClient, client);
        return DoTokenTraverseAuth(client);
    }

    return ERRORNEW(ERROR_EDGE_PORT_INVALID, "rtmp: Illegal edge token, server=%d", (int)args.size());
}

error RtmpConn::DoTokenTraverseAuth(RtmpClient *client)
{
    error err = SUCCESS;

    Request* req = m_info->m_req;
    Assert(client);

    client->SetRecvTimeout(CONSTS_RTMP_TIMEOUT);
    client->SetSendTimeout(CONSTS_RTMP_TIMEOUT);

    if ((err = client->Handshake()) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: handshake");
    }

    // for token tranverse, always take the debug info(which carries token).
    ServerInfo si;
    if ((err = client->ConnectApp(req->m_app, req->m_tcUrl, req, true, &si)) != SUCCESS) {
        return ERRORWRAP(err, "rtmp: connect tcUrl");
    }

    trace("edge token auth ok, tcUrl=%s", req->m_tcUrl.c_str());
    return err;
}

error RtmpConn::OnDisconnect()
{
    error err = SUCCESS;

    HttpHooksOnClose();

    // TODO: FIXME: Implements it.

    return err;
}

error RtmpConn::HttpHooksOnConnect()
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (!config->GetVhostHttpHooksEnabled(req->m_vhost)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        ConfDirective* conf = config->GetVhostOnConnect(req->m_vhost);

        if (!conf) {
            return err;
        }

        hooks = conf->m_args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = HttpHooks::OnConnect(url, req)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp on_connect %s", url.c_str());
        }
    }

    return err;
}

void RtmpConn::HttpHooksOnClose()
{
    Request* req = m_info->m_req;

    if (!config->GetVhostHttpHooksEnabled(req->m_vhost)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        ConfDirective* conf = config->GetVhostOnClose(req->m_vhost);

        if (!conf) {
            return;
        }

        hooks = conf->m_args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        HttpHooks::OnClose(url, req, m_skt->GetSendBytes(), m_skt->GetRecvBytes());
    }
}

error RtmpConn::HttpHooksOnPublish()
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (!config->GetVhostHttpHooksEnabled(req->m_vhost)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        ConfDirective* conf = config->GetVhostOnPublish(req->m_vhost);

        if (!conf) {
            return err;
        }

        hooks = conf->m_args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = HttpHooks::OnPublish(url, req)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp on_publish %s", url.c_str());
        }
    }

    return err;
}

void RtmpConn::HttpHooksOnUnpublish()
{
    Request* req = m_info->m_req;

    if (!config->GetVhostHttpHooksEnabled(req->m_vhost)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        ConfDirective* conf = config->GetVhostOnUnpublish(req->m_vhost);

        if (!conf) {
            return;
        }

        hooks = conf->m_args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        HttpHooks::OnUnpublish(url, req);
    }
}

error RtmpConn::HttpHooksOnPlay()
{
    error err = SUCCESS;

    Request* req = m_info->m_req;

    if (!config->GetVhostHttpHooksEnabled(req->m_vhost)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        ConfDirective* conf = config->GetVhostOnPlay(req->m_vhost);

        if (!conf) {
            return err;
        }

        hooks = conf->m_args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = HttpHooks::OnPlay(url, req)) != SUCCESS) {
            return ERRORWRAP(err, "rtmp on_play %s", url.c_str());
        }
    }

    return err;
}

void RtmpConn::HttpHooksOnStop()
{
    Request* req = m_info->m_req;

    if (!config->GetVhostHttpHooksEnabled(req->m_vhost)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    std::vector<std::string> hooks;

    if (true) {
        ConfDirective* conf = config->GetVhostOnStop(req->m_vhost);

        if (!conf) {
            return;
        }

        hooks = conf->m_args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        HttpHooks::OnStop(url, req);
    }

    return;
}

error RtmpConn::Start()
{
    error err = SUCCESS;

    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "coroutine");
    }

    return err;
}

error RtmpConn::Cycle()
{
    error err = SUCCESS;

    // Serve the client.
    err = DoCycle();

    // Final APM span, parent is the last span, not the root span. Note that only client or server kind will be filtered
    // for error or exception report.
//    IApmSpan* span_final = _srs_apm->span("final")->set_kind(ApmKindServer)->as_child(m_spanClient);
//    AutoFree(IApmSpan, span_final);
//    if (ERRORCODE(err) != 0) {
//        span_final->record_error(err)->set_status(SrsApmStatusError, srs_fmt("fail code=%d", ERRORCODE(err)));
//    }

    // Update statistic when done.
    Statistic* stat = Statistic::Instance();
    stat->KbpsAddDelta(GetId().Cstr(), m_delta);
    stat->OnDisconnect(GetId().Cstr(), err);

    // Notify manager to remove it.
    // Note that we create this object, so we use manager to remove it.
    m_manager->Remove(this);

    // success.
    if (err == SUCCESS) {
        trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (ERRORCODE(err) == ERROR_SUCCESS) {
        trace("client finished%s.", ERRORSUMMARY(err).c_str());
        Freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (IsClientGracefullyClose(err)) {
        warn("client disconnect peer. ret=%d", ERRORCODE(err));
    } else if (IsServerGracefullyClose(err)) {
        warn("server disconnect. ret=%d", ERRORCODE(err));
    } else {
        ERROR("serve error %s", ERRORDESC(err).c_str());
    }

    Freep(err);
    return SUCCESS;
}

std::string RtmpConn::RemoteIp()
{
    return m_ip;
}

const ContextId &RtmpConn::GetId()
{
    return m_trd->Cid();
}

void RtmpConn::Expire()
{
    m_trd->Interrupt();
}

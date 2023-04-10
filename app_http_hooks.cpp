#include "app_http_hooks.h"

#include "app_config.h"
#include "app_statistic.h"
#include "consts.h"
#include "error.h"
#include "protocol_http_client.h"
#include "protocol_json.h"
#include "core_autofree.h"
#include "protocol_utility.h"
#include "protocol_http_stack.h"
#include "utility.h"
#include <inttypes.h>

#define HTTP_RESPONSE_OK    XSTR(ERROR_SUCCESS)

#define HTTP_HEADER_BUFFER 1024
#define HTTP_READ_BUFFER 4096
#define HTTP_BODY_BUFFER (32 * 1024)

// the timeout for hls notify, in srs_utime_t.
#define HLS_NOTIFY_TIMEOUT (10 * UTIME_SECONDS)

HttpHooks::HttpHooks()
{

}

HttpHooks::~HttpHooks()
{

}

error HttpHooks::OnConnect(std::string url, Request *req)
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_connect"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("pageUrl", JsonAny::Str(req->m_pageUrl.c_str()));

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        return ERRORWRAP(err, "http: on_connect failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }

    trace("http: on_connect ok, client_id=%s, url=%s, request=%s, response=%s",
              cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return err;
}

void HttpHooks::OnClose(std::string url, Request *req, int64_t send_bytes, int64_t recv_bytes)
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_close"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("send_bytes", JsonAny::Integer(send_bytes));
    obj->Set("recv_bytes", JsonAny::Integer(recv_bytes));

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        int ret = ERRORCODE(err);
        Freep(err);
        warn("http: ignore on_close failed, client_id=%s, url=%s, request=%s, response=%s, code=%d, ret=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }

    trace("http: on_close ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return;
}

error HttpHooks::OnPublish(std::string url, Request *req)
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_publish"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));

    obj->Set("stream_url", JsonAny::Str(req->GetStreamUrl().c_str()));
    StatisticStream* stream = stat->FindStreamByUrl(req->GetStreamUrl());
    if (stream) {
        obj->Set("stream_id", JsonAny::Str(stream->m_id.c_str()));
    }

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        return ERRORWRAP(err, "http: on_publish failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }

    trace("http: on_publish ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return err;
}

void HttpHooks::OnUnpublish(std::string url, Request *req)
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_unpublish"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));

    obj->Set("stream_url", JsonAny::Str(req->GetStreamUrl().c_str()));
    StatisticStream* stream = stat->FindStreamByUrl(req->GetStreamUrl());
    if (stream) {
        obj->Set("stream_id", JsonAny::Str(stream->m_id.c_str()));
    }

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        int ret = ERRORCODE(err);
        Freep(err);
        warn("http: ignore on_unpublish failed, client_id=%s, url=%s, request=%s, response=%s, status=%d, ret=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }

    trace("http: on_unpublish ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return;
}

error HttpHooks::OnPlay(std::string url, Request *req)
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_play"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));
    obj->Set("pageUrl", JsonAny::Str(req->m_pageUrl.c_str()));

    obj->Set("stream_url", JsonAny::Str(req->GetStreamUrl().c_str()));
    StatisticStream* stream = stat->FindStreamByUrl(req->GetStreamUrl());
    if (stream) {
        obj->Set("stream_id", JsonAny::Str(stream->m_id.c_str()));
    }

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        return ERRORWRAP(err, "http: on_play failed, client_id=%s, url=%s, request=%s, response=%s, status=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }

    trace("http: on_play ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return err;
}

void HttpHooks::OnStop(std::string url, Request *req)
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_stop"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));

    obj->Set("stream_url", JsonAny::Str(req->GetStreamUrl().c_str()));
    StatisticStream* stream = stat->FindStreamByUrl(req->GetStreamUrl());
    if (stream) {
        obj->Set("stream_id", JsonAny::Str(stream->m_id.c_str()));
    }

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        int ret = ERRORCODE(err);
        Freep(err);
        warn("http: ignore on_stop failed, client_id=%s, url=%s, request=%s, response=%s, code=%d, ret=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code, ret);
        return;
    }

    trace("http: on_stop ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return;
}

error HttpHooks::OnDvr(ContextId c, std::string url, Request *req, std::string file)
{
    error err = SUCCESS;

    ContextId cid = c;
    std::string cwd = config->Cwd();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_dvr"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));
    obj->Set("cwd", JsonAny::Str(cwd.c_str()));
    obj->Set("file", JsonAny::Str(file.c_str()));

    obj->Set("stream_url", JsonAny::Str(req->GetStreamUrl().c_str()));
    StatisticStream* stream = stat->FindStreamByUrl(req->GetStreamUrl());
    if (stream) {
        obj->Set("stream_id", JsonAny::Str(stream->m_id.c_str()));
    }

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        return ERRORWRAP(err, "http post on_dvr uri failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }

    trace("http hook on_dvr success. client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return err;
}

error HttpHooks::OnHls(ContextId c, std::string url, Request *req, std::string file, std::string ts_url, std::string m3u8, std::string m3u8_url, int sn, utime_t duration)
{
    error err = SUCCESS;

    ContextId cid = c;
    std::string cwd = config->Cwd();

    // the ts_url is under the same dir of m3u8_url.
    std::string prefix = PathDirname(m3u8_url);
    if (!prefix.empty() && !StringIsHttp(ts_url)) {
        ts_url = prefix + "/" + ts_url;
    }

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("action", JsonAny::Str("on_hls"));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));
    obj->Set("duration", JsonAny::Number(u2ms(duration)/1000.0));
    obj->Set("cwd", JsonAny::Str(cwd.c_str()));
    obj->Set("file", JsonAny::Str(file.c_str()));
    obj->Set("url", JsonAny::Str(ts_url.c_str()));
    obj->Set("m3u8", JsonAny::Str(m3u8.c_str()));
    obj->Set("m3u8_url", JsonAny::Str(m3u8_url.c_str()));
    obj->Set("seq_no", JsonAny::Integer(sn));

    obj->Set("stream_url", JsonAny::Str(req->GetStreamUrl().c_str()));
    StatisticStream* stream = stat->FindStreamByUrl(req->GetStreamUrl());
    if (stream) {
        obj->Set("stream_id", JsonAny::Str(stream->m_id.c_str()));
    }

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        return ERRORWRAP(err, "http: post %s with %s, status=%d, res=%s", url.c_str(), data.c_str(), status_code, res.c_str());
    }

    trace("http: on_hls ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return err;
}

error HttpHooks::OnHlsNotify(ContextId c, std::string url, Request *req, std::string ts_url, int nb_notify)
{
    error err = SUCCESS;

    ContextId cid = c;
    std::string cwd = config->Cwd();

    if (StringIsHttp(ts_url)) {
        url = ts_url;
    }

    Statistic* stat = Statistic::Instance();

    url = StringReplace(url, "[server_id]", stat->ServerId().c_str());
    url = StringReplace(url, "[app]", req->m_app);
    url = StringReplace(url, "[stream]", req->m_stream);
    url = StringReplace(url, "[ts_url]", ts_url);
    url = StringReplace(url, "[param]", req->m_param);

    int64_t starttime = u2ms(UpdateSystemTime());

    HttpUri uri;
    if ((err = uri.Initialize(url)) != SUCCESS) {
        return ERRORWRAP(err, "http: init url=%s", url.c_str());
    }

    HttpClient http;
    if ((err = http.Initialize(uri.GetSchema(), uri.GetHost(), uri.GetPort(), HLS_NOTIFY_TIMEOUT)) != SUCCESS) {
        return ERRORWRAP(err, "http: init client for %s", url.c_str());
    }

    std::string path = uri.GetQuery();
    if (path.empty()) {
        path = uri.GetPath();
    } else {
        path = uri.GetPath();
        path += "?";
        path += uri.GetQuery();
    }
    info("GET %s", path.c_str());

    IHttpMessage* msg = NULL;
    if ((err = http.Get(path.c_str(), "", &msg)) != SUCCESS) {
        return ERRORWRAP(err, "http: get %s", url.c_str());
    }
    AutoFree(IHttpMessage, msg);

    int nb_buf = MIN(nb_notify, HTTP_READ_BUFFER);
    char* buf = new char[nb_buf];
    AutoFreeA(char, buf);

    int nb_read = 0;
    IHttpResponseReader* br = msg->BodyReader();
    while (nb_read < nb_notify && !br->Eof()) {
        ssize_t nb_bytes = 0;
        if ((err = br->Read(buf, nb_buf, &nb_bytes)) != SUCCESS) {
            break;
        }
        nb_read += (int)nb_bytes;
    }

    int spenttime = (int)(u2ms(UpdateSystemTime()) - starttime);
    trace("http hook on_hls_notify success. client_id=%s, url=%s, code=%d, spent=%dms, read=%dB, err=%s",
        cid.Cstr(), url.c_str(), msg->StatusCodes(), spenttime, nb_read, ERRORDESC(err).c_str());

    // ignore any error for on_hls_notify.
    ERRORRESET(err);
    return SUCCESS;
}

error HttpHooks::DiscoverCoWorkers(std::string url, std::string &host, int &port)
{
    error err = SUCCESS;

    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, "", status_code, res)) != SUCCESS) {
        return ERRORWRAP(err, "http: post %s, status=%d, res=%s", url.c_str(), status_code, res.c_str());
    }

    JsonObject* robj = NULL;
    AutoFree(JsonObject, robj);

    if (true) {
        JsonAny* jr = NULL;
        if ((jr = JsonAny::Loads(res)) == NULL) {
            return ERRORNEW(ERROR_OCLUSTER_DISCOVER, "load json from %s", res.c_str());
        }

        if (!jr->IsObject()) {
            Freep(jr);
            return ERRORNEW(ERROR_OCLUSTER_DISCOVER, "response %s", res.c_str());
        }

        robj = jr->ToObject();
    }

    JsonAny* prop = NULL;
    if ((prop = robj->EnsurePropertyObject("data")) == NULL) {
        return ERRORNEW(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }

    JsonObject* p = prop->ToObject();
    if ((prop = p->EnsurePropertyObject("origin")) == NULL) {
        return ERRORNEW(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }
    p = prop->ToObject();

    if ((prop = p->EnsurePropertyString("ip")) == NULL) {
        return ERRORNEW(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }
    host = prop->ToStr();

    if ((prop = p->EnsurePropertyInteger("port")) == NULL) {
        return ERRORNEW(ERROR_OCLUSTER_DISCOVER, "parse data %s", res.c_str());
    }
    port = (int)prop->ToInteger();

    trace("http: cluster redirect %s:%d ok, url=%s, response=%s", host.c_str(), port, url.c_str(), res.c_str());

    return err;
}

error HttpHooks::OnForwardBackend(std::string url, Request *req, std::vector<std::string> &rtmp_urls)
{
    error err = SUCCESS;

    ContextId cid = Context->GetId();

    Statistic* stat = Statistic::Instance();

    JsonObject* obj = JsonAny::Object();
    AutoFree(JsonObject, obj);

    obj->Set("action", JsonAny::Str("on_forward"));
    obj->Set("server_id", JsonAny::Str(stat->ServerId().c_str()));
    obj->Set("client_id", JsonAny::Str(cid.Cstr()));
    obj->Set("ip", JsonAny::Str(req->m_ip.c_str()));
    obj->Set("vhost", JsonAny::Str(req->m_vhost.c_str()));
    obj->Set("app", JsonAny::Str(req->m_app.c_str()));
    obj->Set("tcUrl", JsonAny::Str(req->m_tcUrl.c_str()));
    obj->Set("stream", JsonAny::Str(req->m_stream.c_str()));
    obj->Set("param", JsonAny::Str(req->m_param.c_str()));

    std::string data = obj->Dumps();
    std::string res;
    int status_code;

    HttpClient http;
    if ((err = DoPost(&http, url, data, status_code, res)) != SUCCESS) {
        return ERRORWRAP(err, "http: on_forward_backend failed, client_id=%s, url=%s, request=%s, response=%s, code=%d",
            cid.Cstr(), url.c_str(), data.c_str(), res.c_str(), status_code);
    }

    // parse string res to json.
    JsonAny* info = JsonAny::Loads(res);
    if (!info) {
        return ERRORNEW(ERROR_SYSTEM_FORWARD_LOOP, "load json from %s", res.c_str());
    }
    AutoFree(JsonAny, info);

    // response error code in string.
    if (!info->IsObject()) {
        return ERRORNEW(ERROR_SYSTEM_FORWARD_LOOP, "response %s", res.c_str());
    }

    JsonAny* prop = NULL;
    // response standard object, format in json: {}
    JsonObject* res_info = info->ToObject();
    if ((prop = res_info->EnsurePropertyObject("data")) == NULL) {
        return ERRORNEW(ERROR_SYSTEM_FORWARD_LOOP, "parse data %s", res.c_str());
    }

    JsonObject* p = prop->ToObject();
    if ((prop = p->EnsurePropertyArray("urls")) == NULL) {
        return ERRORNEW(ERROR_SYSTEM_FORWARD_LOOP, "parse urls %s", res.c_str());
    }

    JsonArray* urls = prop->ToArray();
    for (int i = 0; i < urls->Count(); i++) {
        prop = urls->At(i);
        std::string rtmp_url = prop->ToStr();
        if (!rtmp_url.empty()) {
            rtmp_urls.push_back(rtmp_url);
        }
    }

    trace("http: on_forward_backend ok, client_id=%s, url=%s, request=%s, response=%s",
        cid.Cstr(), url.c_str(), data.c_str(), res.c_str());

    return err;
}

error HttpHooks::DoPost(HttpClient *hc, std::string url, std::string req, int &code, std::string &res)
{
    error err = SUCCESS;

    HttpUri uri;
    if ((err = uri.Initialize(url)) != SUCCESS) {
        return ERRORWRAP(err, "http: post failed. url=%s", url.c_str());
    }

    if ((err = hc->Initialize(uri.GetSchema(), uri.GetHost(), uri.GetPort())) != SUCCESS) {
        return ERRORWRAP(err, "http: init client");
    }

    std::string path = uri.GetPath();
    if (!uri.GetQuery().empty()) {
        path += "?" + uri.GetQuery();
    }

    IHttpMessage* msg = NULL;
    if ((err = hc->Post(path, req, &msg)) != SUCCESS) {
        return ERRORWRAP(err, "http: client post");
    }
    AutoFree(IHttpMessage, msg);

    code = msg->StatusCodes();
    if ((err = msg->BodyReadAll(res)) != SUCCESS) {
        return ERRORWRAP(err, "http: body read");
    }

    // ensure the http status is ok.
    if (code != CONSTS_HTTP_OK && code != CONSTS_HTTP_Created) {
        return ERRORNEW(ERROR_HTTP_STATUS_INVALID, "http: status %d", code);
    }

    // should never be empty.
    if (res.empty()) {
        return ERRORNEW(ERROR_HTTP_DATA_INVALID, "http: empty response");
    }

    // parse string res to json.
    JsonAny* info = JsonAny::Loads(res);
    if (!info) {
        return ERRORNEW(ERROR_HTTP_DATA_INVALID, "http: not json %s", res.c_str());
    }
    AutoFree(JsonAny, info);

    // response error code in string.
    if (!info->IsObject()) {
        if (res == HTTP_RESPONSE_OK) {
            return err;
        }
        return ERRORNEW(ERROR_HTTP_DATA_INVALID, "http: response number code %s", res.c_str());
    }

    // response standard object, format in json: {"code": 0, "data": ""}
    JsonObject* res_info = info->ToObject();
    JsonAny* res_code = NULL;
    if ((res_code = res_info->EnsurePropertyInteger("code")) == NULL) {
        return ERRORNEW(ERROR_RESPONSE_CODE, "http: response object no code %s", res.c_str());
    }

    if ((res_code->ToInteger()) != ERROR_SUCCESS) {
        return ERRORNEW(ERROR_RESPONSE_CODE, "http: response object code %" PRId64 " %s", res_code->ToInteger(), res.c_str());
    }

    return err;
}

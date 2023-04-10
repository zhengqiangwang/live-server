#include "app_statistic.h"
#include "app_config.h"
#include "app_utility.h"
#include "protocol_json.h"
#include "protocol_utility.h"
#include "protocol_kbps.h"
#include "utility.h"

StatisticVhost::StatisticVhost()
{
    m_id = GenerateStatVid();

    m_kbps = new Kbps();

    m_nbClients = 0;
    m_nbStreams = 0;
}

StatisticVhost::~StatisticVhost()
{
    Freep(m_kbps);
}

error StatisticVhost::Dumps(JsonObject *obj)
{
    error err = SUCCESS;

    // dumps the config of vhost.
    bool hls_enabled = config->GetHlsEnabled(m_vhost);
    bool enabled = config->GetVhostEnabled(m_vhost);

    obj->Set("id", JsonAny::Str(m_id.c_str()));
    obj->Set("name", JsonAny::Str(m_vhost.c_str()));
    obj->Set("enabled", JsonAny::Boolean(enabled));
    obj->Set("clients", JsonAny::Integer(m_nbClients));
    obj->Set("streams", JsonAny::Integer(m_nbStreams));
    obj->Set("send_bytes", JsonAny::Integer(m_kbps->GetSendBytes()));
    obj->Set("recv_bytes", JsonAny::Integer(m_kbps->GetRecvBytes()));

    JsonObject* okbps = JsonAny::Object();
    obj->Set("kbps", okbps);

    okbps->Set("recv_30s", JsonAny::Integer(m_kbps->GetRecvKbps30s()));
    okbps->Set("send_30s", JsonAny::Integer(m_kbps->GetSendKbps30s()));

    JsonObject* hls = JsonAny::Object();
    obj->Set("hls", hls);

    hls->Set("enabled", JsonAny::Boolean(hls_enabled));
    if (hls_enabled) {
        hls->Set("fragment", JsonAny::Number(u2msi(config->GetHlsFragment(m_vhost))/1000.0));
    }

    return err;
}

StatisticStream::StatisticStream()
{
    m_id = GenerateStatVid();
    m_vhost = NULL;
    m_active = false;

    m_hasVideo = false;
    m_vcodec = VideoCodecIdReserved;
    m_avcProfile = AvcProfileReserved;
    m_avcLevel = AvcLevelReserved;

    m_hasAudio = false;
    m_acodec = AudioCodecIdReserved1;
    m_asampleRate = AudioSampleRateReserved;
    m_asoundType = AudioChannelsReserved;
    m_aacObject = AacObjectTypeReserved;
    m_width = 0;
    m_height = 0;

    m_kbps = new Kbps();

    m_nbClients = 0;
    m_frames = new Pps();
}

StatisticStream::~StatisticStream()
{
    Freep(m_kbps);
    Freep(m_frames);
}

error StatisticStream::Dumps(JsonObject *obj)
{
    error err = SUCCESS;

    obj->Set("id", JsonAny::Str(m_id.c_str()));
    obj->Set("name", JsonAny::Str(m_stream.c_str()));
    obj->Set("vhost", JsonAny::Str(m_vhost->m_id.c_str()));
    obj->Set("app", JsonAny::Str(m_app.c_str()));
    obj->Set("tcUrl", JsonAny::Str(m_tcUrl.c_str()));
    obj->Set("url", JsonAny::Str(m_url.c_str()));
    obj->Set("live_ms", JsonAny::Integer(u2ms(GetSystemTime())));
    obj->Set("clients", JsonAny::Integer(m_nbClients));
    obj->Set("frames", JsonAny::Integer(m_frames->m_sugar));
    obj->Set("send_bytes", JsonAny::Integer(m_kbps->GetSendBytes()));
    obj->Set("recv_bytes", JsonAny::Integer(m_kbps->GetRecvBytes()));

    JsonObject* okbps = JsonAny::Object();
    obj->Set("kbps", okbps);

    okbps->Set("recv_30s", JsonAny::Integer(m_kbps->GetRecvKbps30s()));
    okbps->Set("send_30s", JsonAny::Integer(m_kbps->GetSendKbps30s()));

    JsonObject* publish = JsonAny::Object();
    obj->Set("publish", publish);

    publish->Set("active", JsonAny::Boolean(m_active));
    if (!m_publisherId.empty()) {
        publish->Set("cid", JsonAny::Str(m_publisherId.c_str()));
    }

    if (!m_hasVideo) {
        obj->Set("video", JsonAny::Null());
    } else {
        JsonObject* video = JsonAny::Object();
        obj->Set("video", video);

        video->Set("codec", JsonAny::Str(VideoCodecId2str(m_vcodec).c_str()));
        video->Set("profile", JsonAny::Str(AvcProfile2str(m_avcProfile).c_str()));
        video->Set("level", JsonAny::Str(AvcLevel2str(m_avcLevel).c_str()));
        video->Set("width", JsonAny::Integer(m_width));
        video->Set("height", JsonAny::Integer(m_height));
    }

    if (!m_hasAudio) {
        obj->Set("audio", JsonAny::Null());
    } else {
        JsonObject* audio = JsonAny::Object();
        obj->Set("audio", audio);

        audio->Set("codec", JsonAny::Str(AudioCodecId2str(m_acodec).c_str()));
        audio->Set("sample_rate", JsonAny::Integer(flv_srates[m_asampleRate]));
        audio->Set("channel", JsonAny::Integer(m_asoundType + 1));
        audio->Set("profile", JsonAny::Str(AacObject2str(m_aacObject).c_str()));
    }

    return err;
}

void StatisticStream::Publish(std::string id)
{
    // To prevent duplicated publish event by bridger.
    if (m_active) {
        return;
    }

    m_publisherId = id;
    m_active = true;

    m_vhost->m_nbStreams++;
}

void StatisticStream::Close()
{
    // To prevent duplicated close event.
    if (!m_active) {
        return;
    }

    m_hasVideo = false;
    m_hasAudio = false;
    m_active = false;

    m_vhost->m_nbStreams--;
}

StatisticClient::StatisticClient()
{
    m_stream = NULL;
    m_conn = NULL;
    m_req = NULL;
    m_type = RtmpConnUnknown;
    m_create = GetSystemTime();

    m_kbps = new Kbps();
}

StatisticClient::~StatisticClient()
{
    Freep(m_kbps);
    Freep(m_req);
}

error StatisticClient::Dumps(JsonObject *obj)
{
    error err = SUCCESS;

    obj->Set("id", JsonAny::Str(m_id.c_str()));
    obj->Set("vhost", JsonAny::Str(m_stream->m_vhost->m_id.c_str()));
    obj->Set("stream", JsonAny::Str(m_stream->m_id.c_str()));
    obj->Set("ip", JsonAny::Str(m_req->m_ip.c_str()));
    obj->Set("pageUrl", JsonAny::Str(m_req->m_pageUrl.c_str()));
    obj->Set("swfUrl", JsonAny::Str(m_req->m_swfUrl.c_str()));
    obj->Set("tcUrl", JsonAny::Str(m_req->m_tcUrl.c_str()));
    obj->Set("url", JsonAny::Str(m_req->GetStreamUrl().c_str()));
    obj->Set("name", JsonAny::Str(m_req->m_stream.c_str()));
    obj->Set("type", JsonAny::Str(ClientTypeString(m_type).c_str()));
    obj->Set("publish", JsonAny::Boolean(ClientTypeIsPublish(m_type)));
    obj->Set("alive", JsonAny::Number(u2ms(GetSystemTime() - m_create) / 1000.0));
    obj->Set("send_bytes", JsonAny::Integer(m_kbps->GetSendBytes()));
    obj->Set("recv_bytes", JsonAny::Integer(m_kbps->GetRecvBytes()));

    JsonObject* okbps = JsonAny::Object();
    obj->Set("kbps", okbps);

    okbps->Set("recv_30s", JsonAny::Integer(m_kbps->GetRecvKbps30s()));
    okbps->Set("send_30s", JsonAny::Integer(m_kbps->GetSendKbps30s()));

    return err;
}

Statistic* Statistic::_instance = NULL;

Statistic::Statistic()
{
    m_kbps = new Kbps();

    m_nbClients = 0;
    m_nbErrs = 0;
}

Statistic::~Statistic()
{
    Freep(m_kbps);

    if (true) {
        std::map<std::string, StatisticVhost*>::iterator it;
        for (it = m_vhosts.begin(); it != m_vhosts.end(); it++) {
            StatisticVhost* vhost = it->second;
            Freep(vhost);
        }
    }
    if (true) {
        std::map<std::string, StatisticStream*>::iterator it;
        for (it = m_streams.begin(); it != m_streams.end(); it++) {
            StatisticStream* stream = it->second;
            Freep(stream);
        }
    }
    if (true) {
        std::map<std::string, StatisticClient*>::iterator it;
        for (it = m_clients.begin(); it != m_clients.end(); it++) {
            StatisticClient* client = it->second;
            Freep(client);
        }
    }

    m_vhosts.clear();
    m_rvhosts.clear();
    m_streams.clear();
    m_rstreams.clear();
}

Statistic *Statistic::Instance()
{
    if (_instance == NULL) {
        _instance = new Statistic();
    }
    return _instance;
}

StatisticVhost *Statistic::FindVhostById(std::string vid)
{
    std::map<std::string, StatisticVhost*>::iterator it;
    if ((it = m_vhosts.find(vid)) != m_vhosts.end()) {
        return it->second;
    }
    return NULL;
}

StatisticVhost *Statistic::FindVhostByName(std::string name)
{
    if (m_rvhosts.empty()) {
        return NULL;
    }

    std::map<std::string, StatisticVhost*>::iterator it;
    if ((it = m_rvhosts.find(name)) != m_rvhosts.end()) {
        return it->second;
    }
    return NULL;
}

StatisticStream *Statistic::FindStream(std::string sid)
{
    std::map<std::string, StatisticStream*>::iterator it;
    if ((it = m_streams.find(sid)) != m_streams.end()) {
        return it->second;
    }
    return NULL;
}

StatisticStream *Statistic::FindStreamByUrl(std::string url)
{
    std::map<std::string, StatisticStream*>::iterator it;
    if ((it = m_rstreams.find(url)) != m_rstreams.end()) {
        return it->second;
    }
    return NULL;
}

StatisticClient *Statistic::FindClient(std::string client_id)
{
    std::map<std::string, StatisticClient*>::iterator it;
    if ((it = m_clients.find(client_id)) != m_clients.end()) {
        return it->second;
    }
    return NULL;
}

error Statistic::OnVideoInfo(Request *req, VideoCodecId vcodec, AvcProfile avc_profile, AvcLevel avc_level, int width, int height)
{
    error err = SUCCESS;

    StatisticVhost* vhost = CreateVhost(req);
    StatisticStream* stream = CreateStream(vhost, req);

    stream->m_hasVideo = true;
    stream->m_vcodec = vcodec;
    stream->m_avcProfile = avc_profile;
    stream->m_avcLevel = avc_level;

    stream->m_width = width;
    stream->m_height = height;

    return err;
}

error Statistic::OnAudioInfo(Request *req, AudioCodecId acodec, AudioSampleRate asample_rate, AudioChannels asound_type, AacObjectType aac_object)
{
    error err = SUCCESS;

    StatisticVhost* vhost = CreateVhost(req);
    StatisticStream* stream = CreateStream(vhost, req);

    stream->m_hasAudio = true;
    stream->m_acodec = acodec;
    stream->m_asampleRate = asample_rate;
    stream->m_asoundType = asound_type;
    stream->m_aacObject = aac_object;

    return err;
}

error Statistic::OnVideoFrames(Request *req, int nb_frames)
{
    error err = SUCCESS;

    StatisticVhost* vhost = CreateVhost(req);
    StatisticStream* stream = CreateStream(vhost, req);

    stream->m_frames->m_sugar += nb_frames;

    return err;
}

void Statistic::OnStreamPublish(Request *req, std::string publisher_id)
{
    StatisticVhost* vhost = CreateVhost(req);
    StatisticStream* stream = CreateStream(vhost, req);

    stream->Publish(publisher_id);
}

void Statistic::OnStreamClose(Request *req)
{
    StatisticVhost* vhost = CreateVhost(req);
    StatisticStream* stream = CreateStream(vhost, req);
    stream->Close();
}

error Statistic::OnClient(std::string id, Request *req, IExpire *conn, RtmpConnType type)
{
    error err = SUCCESS;

    StatisticVhost* vhost = CreateVhost(req);
    StatisticStream* stream = CreateStream(vhost, req);

    // create client if not exists
    StatisticClient* client = NULL;
    if (m_clients.find(id) == m_clients.end()) {
        client = new StatisticClient();
        client->m_id = id;
        client->m_stream = stream;
        m_clients[id] = client;
    } else {
        client = m_clients[id];
    }

    // got client.
    client->m_conn = conn;
    client->m_type = type;
    stream->m_nbClients++;
    vhost->m_nbClients++;

    // The req might be freed, in such as SrsLiveStream::update, so we must copy it.
    // @see https://github.com/ossrs/srs/issues/2311
    Freep(client->m_req);
    client->m_req = req->Copy();

    m_nbClients++;

    return err;
}

void Statistic::OnDisconnect(std::string id, error err)
{
    std::map<std::string, StatisticClient*>::iterator it = m_clients.find(id);
    if (it == m_clients.end()) return;

    StatisticClient* client = it->second;
    StatisticStream* stream = client->m_stream;
    StatisticVhost* vhost = stream->m_vhost;

    Freep(client);
    m_clients.erase(it);

    stream->m_nbClients--;
    vhost->m_nbClients--;

    if (ERRORCODE(err) != ERROR_SUCCESS) {
        m_nbErrs++;
    }

    CleanupStream(stream);
}

void Statistic::CleanupStream(StatisticStream *stream)
{
    // If stream has publisher(not active) or player(clients), never cleanup it.
    if (stream->m_active || stream->m_nbClients > 0) {
        return;
    }

    // There should not be any clients referring to the stream.
    for (std::map<std::string, StatisticClient*>::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
        StatisticClient* client = it->second;
        Assert(client->m_stream != stream);
    }

    // Do cleanup streams.
    if (true) {
        std::map<std::string, StatisticStream *>::iterator it;
        if ((it = m_streams.find(stream->m_id)) != m_streams.end()) {
            m_streams.erase(it);
        }
    }

    if (true) {
        std::map<std::string, StatisticStream *>::iterator it;
        if ((it = m_rstreams.find(stream->m_url)) != m_rstreams.end()) {
            m_rstreams.erase(it);
        }
    }

    // It's safe to delete the stream now.
    Freep(stream);
}

void Statistic::KbpsAddDelta(std::string id, IKbpsDelta *delta)
{
    if (!delta) return;

    std::map<std::string, StatisticClient*>::iterator it = m_clients.find(id);
    if (it == m_clients.end()) return;

    StatisticClient* client = it->second;

    // resample the kbps to collect the delta.
    int64_t in, out;
    delta->Remark(&in, &out);

    // add delta of connection to kbps.
    // for next sample() of server kbps can get the stat.
    m_kbps->AddDelta(in, out);
    client->m_kbps->AddDelta(in, out);
    client->m_stream->m_kbps->AddDelta(in, out);
    client->m_stream->m_vhost->m_kbps->AddDelta(in, out);
}

void Statistic::KbpsSample()
{
    m_kbps->Sample();
    if (true) {
        std::map<std::string, StatisticVhost*>::iterator it;
        for (it = m_vhosts.begin(); it != m_vhosts.end(); it++) {
            StatisticVhost* vhost = it->second;
            vhost->m_kbps->Sample();
        }
    }
    if (true) {
        std::map<std::string, StatisticStream*>::iterator it;
        for (it = m_streams.begin(); it != m_streams.end(); it++) {
            StatisticStream* stream = it->second;
            stream->m_kbps->Sample();
            stream->m_frames->Update();
        }
    }
    if (true) {
        std::map<std::string, StatisticClient*>::iterator it;
        for (it = m_clients.begin(); it != m_clients.end(); it++) {
            StatisticClient* client = it->second;
            client->m_kbps->Sample();
        }
    }

    // Update server level data.
    UpdateRtmpServer((int)m_clients.size(), m_kbps);
}

std::string Statistic::ServerId()
{
    if (m_serverId.empty()) {
        m_serverId = config->GetServerId();
    }
    return m_serverId;
}

error Statistic::DumpsVhosts(JsonArray *arr)
{
    error err = SUCCESS;

    std::map<std::string, StatisticVhost*>::iterator it;
    for (it = m_vhosts.begin(); it != m_vhosts.end(); it++) {
        StatisticVhost* vhost = it->second;

        JsonObject* obj = JsonAny::Object();
        arr->Append(obj);

        if ((err = vhost->Dumps(obj)) != SUCCESS) {
            return ERRORWRAP(err, "dump vhost");
        }
    }

    return err;
}

error Statistic::DumpsStreams(JsonArray *arr, int start, int count)
{
    error err = SUCCESS;

    std::map<std::string, StatisticStream*>::iterator it = m_streams.begin();
    for (int i = 0; i < start + count && it != m_streams.end(); it++, i++) {
        if (i < start) {
            continue;
        }

        StatisticStream* stream = it->second;

        JsonObject* obj = JsonAny::Object();
        arr->Append(obj);

        if ((err = stream->Dumps(obj)) != SUCCESS) {
            return ERRORWRAP(err, "dump stream");
        }
    }

    return err;
}

error Statistic::DumpsClients(JsonArray *arr, int start, int count)
{
    error err = SUCCESS;

    std::map<std::string, StatisticClient*>::iterator it = m_clients.begin();
    for (int i = 0; i < start + count && it != m_clients.end(); it++, i++) {
        if (i < start) {
            continue;
        }

        StatisticClient* client = it->second;

        JsonObject* obj = JsonAny::Object();
        arr->Append(obj);

        if ((err = client->Dumps(obj)) != SUCCESS) {
            return ERRORWRAP(err, "dump client");
        }
    }

    return err;
}

void Statistic::DumpsHintsKv(std::stringstream &ss)
{
    if (!m_streams.empty()) {
        ss << "&streams=" << m_streams.size();
    }
    if (!m_clients.empty()) {
        ss << "&clients=" << m_clients.size();
    }
    if (m_kbps->GetRecvKbps30s()) {
        ss << "&recv=" << m_kbps->GetRecvKbps30s();
    }
    if (m_kbps->GetSendKbps30s()) {
        ss << "&send=" << m_kbps->GetSendKbps30s();
    }
}

void Statistic::DumpsClsSummaries(ClsSugar *sugar)
{
//    if (!m_vhosts.empty()) {
//        sugar->kv("vhosts", Fmt("%d", (int)vhosts.size()));
//    }
//    if (!m_streams.empty()) {
//        sugar->kv("streams", Fmt("%d", (int)streams.size()));
//    }
//    if (!m_clients.empty()) {
//        sugar->kv("clients", Fmt("%d", (int)clients.size()));
//    }
}

void Statistic::DumpsClsStreams(ClsSugars *sugars)
{
//    for (std::map<std::string, StatisticStream*>::iterator it = m_streams.begin(); it != m_streams.end(); ++it) {
//        StatisticStream* stream = it->second;
//        if (!stream->m_active || !stream->m_nbClients) {
//            continue;
//        }

//        ClsSugar* sugar = sugars->create();
//        sugar->kv("hint", "stream");
//        sugar->kv("version", RTMP_SIG_SRS_VERSION);
//        sugar->kv("pid", srs_fmt("%d", getpid()));

//        sugar->kv("sid", stream->id);
//        sugar->kv("url", stream->url);

//        if (stream->frames->r30s()) {
//            sugar->kv("fps", srs_fmt("%d", stream->frames->r30s()));
//        }
//        if (stream->width) {
//            sugar->kv("width", srs_fmt("%d", stream->width));
//        }
//        if (stream->height) {
//            sugar->kv("height", srs_fmt("%d", stream->height));
//        }

//        StatisticClient* pub = find_client(stream->publisher_id);
//        if (pub) {
//            if (pub->kbps->get_recv_kbps_30s()) {
//                sugar->kv("recv", srs_fmt("%d", pub->kbps->get_recv_kbps_30s()));
//            }
//            if (pub->kbps->get_send_kbps_30s()) {
//                sugar->kv("send", srs_fmt("%d", pub->kbps->get_send_kbps_30s()));
//            }
//        }

//        sugar->kv("clients", srs_fmt("%d", stream->nb_clients));
//        if (stream->kbps->get_recv_kbps_30s()) {
//            sugar->kv("recv2", srs_fmt("%d", stream->kbps->get_recv_kbps_30s()));
//        }
//        if (stream->kbps->get_send_kbps_30s()) {
//            sugar->kv("send2", srs_fmt("%d", stream->kbps->get_send_kbps_30s()));
//        }
//    }
}

StatisticVhost *Statistic::CreateVhost(Request *req)
{
    StatisticVhost* vhost = NULL;

    // create vhost if not exists.
    if (m_rvhosts.find(req->m_vhost) == m_rvhosts.end()) {
        vhost = new StatisticVhost();
        vhost->m_vhost = req->m_vhost;
        m_rvhosts[req->m_vhost] = vhost;
        m_vhosts[vhost->m_id] = vhost;
        return vhost;
    }

    vhost = m_rvhosts[req->m_vhost];

    return vhost;
}

StatisticStream *Statistic::CreateStream(StatisticVhost *vhost, Request *req)
{
    // To identify a stream, use url without extension, for example, the bellow are the same stream:
    //      ossrs.io/live/livestream
    //      ossrs.io/live/livestream.flv
    //      ossrs.io/live/livestream.m3u8
    // Note that we also don't use schema, and vhost is optional.
    std::string url = req->GetStreamUrl();

    StatisticStream* stream = NULL;

    // create stream if not exists.
    if (m_rstreams.find(url) == m_rstreams.end()) {
        stream = new StatisticStream();
        stream->m_vhost = vhost;
        stream->m_stream = req->m_stream;
        stream->m_app = req->m_app;
        stream->m_url = url;
        stream->m_tcUrl = req->m_tcUrl;
        m_rstreams[url] = stream;
        m_streams[stream->m_id] = stream;
        return stream;
    }

    stream = m_rstreams[url];

    return stream;
}

error Statistic::DumpsMetrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    error err = SUCCESS;

    send_bytes = m_kbps->GetSendBytes();
    recv_bytes = m_kbps->GetRecvBytes();

    nstreams = m_streams.size();
    nclients = m_clients.size();

    total_nclients = m_nbClients;
    nerrs = m_nbErrs;

    return err;
}

std::string GenerateStatVid()
{
    return "vid-" + RandomStr(7);
}

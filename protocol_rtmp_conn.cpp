#include "protocol_rtmp_conn.h"
#include "protocol_amf0.h"
#include "protocol_rtmp_stack.h"
#include "protocol_utility.h"
#include "protocol_kbps.h"
#include "protocol_st.h"

#include <inttypes.h>
#include <unistd.h>


BasicRtmpClient::BasicRtmpClient(std::string r, utime_t ctm, utime_t stm)
{
    m_kbps = new NetworkKbps();

    m_url = r;
    m_connectTimeout = ctm;
    m_streamTimeout = stm;

    m_req = new Request();
    ParseRtmpUrl(m_url, m_req->m_tcUrl, m_req->m_stream);
    DiscoveryTcUrl(m_req->m_tcUrl, m_req->m_schema, m_req->m_host, m_req->m_vhost, m_req->m_app, m_req->m_stream, m_req->m_port, m_req->m_param);

    m_transport = NULL;
    m_client = NULL;

    m_streamId = 0;
}

BasicRtmpClient::~BasicRtmpClient()
{
    Close();
    Freep(m_kbps);
    Freep(m_req);
}

Amf0Object *BasicRtmpClient::ExtraArgs()
{
    if (m_req->m_args == NULL) {
        m_req->m_args = Amf0Any::Object();
    }
    return m_req->m_args;
}

error BasicRtmpClient::Connect()
{
    error err = SUCCESS;

    Close();

    m_transport = new TcpClient(m_req->m_host, m_req->m_port, utime_t(m_connectTimeout));
    m_client = new RtmpClient(m_transport);
    m_kbps->SetIo(m_transport, m_transport);

    if ((err = m_transport->Connect()) != SUCCESS) {
        Close();
        return ERRORWRAP(err, "connect");
    }

    m_client->SetRecvTimeout(m_streamTimeout);
    m_client->SetSendTimeout(m_streamTimeout);

    // connect to vhost/app
    if ((err = m_client->Handshake()) != SUCCESS) {
        return ERRORWRAP(err, "handshake");
    }
    if ((err = ConnectApp()) != SUCCESS) {
        return ERRORWRAP(err, "connect app");
    }
    if ((err = m_client->CreateStream(m_streamId)) != SUCCESS) {
        return ERRORWRAP(err, "create stream_id=%d", m_streamId);
    }

    return err;
}

void BasicRtmpClient::Close()
{
    m_kbps->SetIo(NULL, NULL);
    Freep(m_client);
    Freep(m_transport);
}

error BasicRtmpClient::ConnectApp()
{
    return DoConnectApp(GetPublicInternetAddress(), false);
}

error BasicRtmpClient::DoConnectApp(std::string local_ip, bool debug)
{
    error err = SUCCESS;

    // notify server the edge identity,
    Amf0Object* data = ExtraArgs();
    data->Set("srs_sig", Amf0Any::Str(RTMP_SIG_KEY));
    data->Set("srs_server", Amf0Any::Str(RTMP_SIG_SERVER));
    data->Set("srs_license", Amf0Any::Str(RTMP_SIG_LICENSE));
    data->Set("srs_url", Amf0Any::Str(RTMP_SIG_URL));
    data->Set("srs_version", Amf0Any::Str(RTMP_SIG_VERSION));
    // for edge to directly get the id of client.
    data->Set("srs_pid", Amf0Any::Number(getpid()));
    data->Set("srs_id", Amf0Any::Str(Context->GetId().Cstr()));

    // local ip of edge
    data->Set("srs_server_ip", Amf0Any::Str(local_ip.c_str()));

    // generate the tcUrl
    std::string param = "";
    std::string target_vhost = m_req->m_vhost;
    std::string tc_url = GenerateTcUrl("rtmp", m_req->m_host, m_req->m_vhost, m_req->m_app, m_req->m_port);

    // replace the tcUrl in request,
    // which will replace the tc_url in client.connect_app().
    m_req->m_tcUrl = tc_url;

    // upnode server identity will show in the connect_app of client.
    // the debug_srs_upnode is config in vhost and default to true.
    ServerInfo si;
    if ((err = m_client->ConnectApp(m_req->m_app, tc_url, m_req, debug, &si)) != SUCCESS) {
        return ERRORWRAP(err, "connect app tcUrl=%s, debug=%d", tc_url.c_str(), debug);
    }

    return err;
}

error BasicRtmpClient::Publish(int chunk_size, bool with_vhost, std::string *pstream)
{
    error err = SUCCESS;

    // Pass params in stream, @see https://github.com/ossrs/srs/issues/1031#issuecomment-409745733
    std::string stream = GenerateStreamWithQuery(m_req->m_host, m_req->m_vhost, m_req->m_stream, m_req->m_param, with_vhost);

    // Return the generated stream.
    if (pstream) {
        *pstream = stream;
    }

    // publish.
    if ((err = m_client->Publish(stream, m_streamId, chunk_size)) != SUCCESS) {
        return ERRORWRAP(err, "publish failed, stream=%s, stream_id=%d", stream.c_str(), m_streamId);
    }

    return err;
}

error BasicRtmpClient::Play(int chunk_size, bool with_vhost, std::string *pstream)
{
    error err = SUCCESS;

    // Pass params in stream, @see https://github.com/ossrs/srs/issues/1031#issuecomment-409745733
    std::string stream = GenerateStreamWithQuery(m_req->m_host, m_req->m_vhost, m_req->m_stream, m_req->m_param, with_vhost);

    // Return the generated stream.
    if (pstream) {
        *pstream = stream;
    }

    if ((err = m_client->Play(stream, m_streamId, chunk_size)) != SUCCESS) {
        return ERRORWRAP(err, "connect with server failed, stream=%s, stream_id=%d", stream.c_str(), m_streamId);
    }

    return err;
}

void BasicRtmpClient::KbpsSample(const char *label, utime_t age)
{
    m_kbps->Sample();

    int sr = m_kbps->GetSendKbps();
    int sr30s = m_kbps->GetSendKbps30s();
    int sr5m = m_kbps->GetSendKbps5m();
    int rr = m_kbps->GetRecvKbps();
    int rr30s = m_kbps->GetRecvKbps30s();
    int rr5m = m_kbps->GetRecvKbps5m();

    trace("<- %s time=%" PRId64 ", okbps=%d,%d,%d, ikbps=%d,%d,%d", label, u2ms(age), sr, sr30s, sr5m, rr, rr30s, rr5m);
}

void BasicRtmpClient::KbpsSample(const char *label, utime_t age, int msgs)
{
    m_kbps->Sample();

    int sr = m_kbps->GetSendKbps();
    int sr30s = m_kbps->GetSendKbps30s();
    int sr5m = m_kbps->GetSendKbps5m();
    int rr = m_kbps->GetRecvKbps();
    int rr30s = m_kbps->GetRecvKbps30s();
    int rr5m = m_kbps->GetRecvKbps5m();

    trace("<- %s time=%" PRId64 ", msgs=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d", label, u2ms(age), msgs, sr, sr30s, sr5m, rr, rr30s, rr5m);
}

int BasicRtmpClient::Sid()
{
    return m_streamId;
}

error BasicRtmpClient::RecvMessage(CommonMessage **pmsg)
{
    return m_client->RecvMessage(pmsg);
}

error BasicRtmpClient::DecodeMessage(CommonMessage *msg, Packet **ppacket)
{
    return m_client->DecodeMessage(msg, ppacket);
}

error BasicRtmpClient::SendAndFreeMessages(SharedPtrMessage **msgs, int nb_msgs)
{
    return m_client->SendAndFreeMessages(msgs, nb_msgs, m_streamId);
}

error BasicRtmpClient::SendAndFreeMessage(SharedPtrMessage *msg)
{
    return m_client->SendAndFreeMessage(msg, m_streamId);
}

void BasicRtmpClient::SetRecvTimeout(utime_t timeout)
{
    m_transport->SetRecvTimeout(timeout);
}

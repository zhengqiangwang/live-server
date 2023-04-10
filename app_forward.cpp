#include "app_forward.h"

#include "app_config.h"
#include "app_pithy_print.h"
#include "app_rtmp_conn.h"
#include "app_source.h"
#include "protocol_rtmp_msg_array.h"
#include "protocol_rtmp_stack.h"
#include "consts.h"
#include "utility.h"
#include "core_autofree.h"
#include "codec.h"

Forwarder::Forwarder(OriginHub *h)
{
    m_hub = h;

    m_req = NULL;
    m_shVideo = m_shAudio = NULL;

    m_sdk = NULL;
    m_trd = new DummyCoroutine();
    m_queue = new MessageQueue();
    m_jitter = new RtmpJitter();
}

Forwarder::~Forwarder()
{
    Freep(m_sdk);
    Freep(m_trd);
    Freep(m_queue);
    Freep(m_jitter);

    Freep(m_shVideo);
    Freep(m_shAudio);

    Freep(m_req);
}

error Forwarder::Initialize(Request *r, std::string ep)
{
    error err = SUCCESS;

    // it's ok to use the request object,
    // SrsLiveSource already copy it and never delete it.
    m_req = r->Copy();

    // the ep(endpoint) to forward to
    m_epForward = ep;

    return err;
}

void Forwarder::SetQueueSize(utime_t queue_size)
{
    m_queue->SetQueueSize(queue_size);
}

error Forwarder::OnPublish()
{
    error err = SUCCESS;

    Freep(m_trd);
    m_trd = new STCoroutine("forward", this);
    if ((err = m_trd->Start()) != SUCCESS) {
        return ERRORWRAP(err, "start thread");
    }

    return err;
}

void Forwarder::OnUnpublish()
{
    m_trd->Stop();
    m_sdk->Close();
}

error Forwarder::OnMetaData(SharedPtrMessage *shared_metadata)
{
    error err = SUCCESS;

    SharedPtrMessage* metadata = shared_metadata->Copy();

    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = m_jitter->Correct(metadata, RtmpJitterAlgorithmOFF)) != SUCCESS) {
        return ERRORWRAP(err, "jitter");
    }

    if ((err = m_queue->Enqueue(metadata)) != SUCCESS) {
        return ERRORWRAP(err, "enqueue metadata");
    }

    return err;
}

error Forwarder::OnAudio(SharedPtrMessage *shared_audio)
{
    error err = SUCCESS;

    SharedPtrMessage* msg = shared_audio->Copy();

    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = m_jitter->Correct(msg, RtmpJitterAlgorithmOFF)) != SUCCESS) {
        return ERRORWRAP(err, "jitter");
    }

    if (FlvAudio::Sh(msg->m_payload, msg->m_size)) {
        Freep(m_shAudio);
        m_shAudio = msg->Copy();
    }

    if ((err = m_queue->Enqueue(msg)) != SUCCESS) {
        return ERRORWRAP(err, "enqueue audio");
    }

    return err;
}

error Forwarder::OnVideo(SharedPtrMessage *shared_video)
{
    error err = SUCCESS;

    SharedPtrMessage* msg = shared_video->Copy();

    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = m_jitter->Correct(msg, RtmpJitterAlgorithmOFF)) != SUCCESS) {
        return ERRORWRAP(err, "jitter");
    }

    if (FlvVideo::Sh(msg->m_payload, msg->m_size)) {
        Freep(m_shVideo);
        m_shVideo = msg->Copy();
    }

    if ((err = m_queue->Enqueue(msg)) != SUCCESS) {
        return ERRORWRAP(err, "enqueue video");
    }

    return err;
}

// when error, forwarder sleep for a while and retry.
#define FORWARDER_CIMS (3 * UTIME_SECONDS)

error Forwarder::Cycle()
{
    error err = SUCCESS;

    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "forwarder");
        }

        if ((err = DoCycle()) != SUCCESS) {
            warn("Forwarder: Ignore error, %s", ERRORDESC(err).c_str());
            Freep(err);
        }

        // Never wait if thread error, fast quit.
        // @see https://github.com/ossrs/srs/pull/2284
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "forwarder");
        }

        Usleep(FORWARDER_CIMS);
    }

    return err;
}

error Forwarder::DoCycle()
{
    error err = SUCCESS;

    std::string url;
    if (true) {
        std::string server;
        int port = CONSTS_RTMP_DEFAULT_PORT;

        // parse host:port from hostport.
        ParseHostport(m_epForward, server, port);

        // generate url
        url = GenerateRtmpUrl(server, port, m_req->m_host, m_req->m_vhost, m_req->m_app, m_req->m_stream, m_req->m_param);
    }

    Freep(m_sdk);
    utime_t cto = FORWARDER_CIMS;
    utime_t sto = CONSTS_RTMP_TIMEOUT;
    m_sdk = new SimpleRtmpClient(url, cto, sto);

    if ((err = m_sdk->Connect()) != SUCCESS) {
        return ERRORWRAP(err, "sdk connect url=%s, cto=%dms, sto=%dms.", url.c_str(), u2msi(cto), u2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    std::string stream;
    if ((err = m_sdk->Publish(config->GetChunkSize(m_req->m_vhost), false, &stream)) != SUCCESS) {
        return ERRORWRAP(err, "sdk publish");
    }

    if ((err = m_hub->OnForwarderStart(this)) != SUCCESS) {
        return ERRORWRAP(err, "notify hub start");
    }

    if ((err = Forward()) != SUCCESS) {
        return ERRORWRAP(err, "forward");
    }

    trace("forward publish url %s, stream=%s%s as %s", url.c_str(), m_req->m_stream.c_str(), m_req->m_param.c_str(), stream.c_str());

    return err;
}

#define SYS_MAX_FORWARD_SEND_MSGS 128

error Forwarder::Forward()
{
    error err = SUCCESS;

    m_sdk->SetRecvTimeout(CONSTS_RTMP_PULSE);

    PithyPrint* pprint = PithyPrint::CreateForwarder();
    AutoFree(PithyPrint, pprint);

    MessageArray msgs(SYS_MAX_FORWARD_SEND_MSGS);

    // update sequence header
    // TODO: FIXME: maybe need to zero the sequence header timestamp.
    if (m_shVideo) {
        if ((err = m_sdk->SendAndFreeMessage(m_shVideo->Copy())) != SUCCESS) {
            return ERRORWRAP(err, "send video sh");
        }
    }
    if (m_shAudio) {
        if ((err = m_sdk->SendAndFreeMessage(m_shAudio->Copy())) != SUCCESS) {
            return ERRORWRAP(err, "send audio sh");
        }
    }

    while (true) {
        if ((err = m_trd->Pull()) != SUCCESS) {
            return ERRORWRAP(err, "thread quit");
        }

        pprint->Elapse();

        // read from client.
        if (true) {
            CommonMessage* msg = NULL;
            err = m_sdk->RecvMessage(&msg);

            if (err != SUCCESS && ERRORCODE(err) != ERROR_SOCKET_TIMEOUT) {
                return ERRORWRAP(err, "receive control message");
            }
            ERRORRESET(err);

            Freep(msg);
        }

        // forward all messages.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = m_queue->DumpPackets(msgs.m_max, msgs.m_msgs, count)) != SUCCESS) {
            return ERRORWRAP(err, "dump packets");
        }

        // pithy print
        if (pprint->CanPrint()) {
            m_sdk->KbpsSample(CONSTS_LOG_FOWARDER, pprint->Age(), count);
        }

        // ignore when no messages.
        if (count <= 0) {
            continue;
        }

        // sendout messages, all messages are freed by send_and_free_messages().
        if ((err = m_sdk->SendAndFreeMessages(msgs.m_msgs, count)) != SUCCESS) {
            return ERRORWRAP(err, "send messages");
        }
    }

    return err;
}

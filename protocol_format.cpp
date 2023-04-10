#include "protocol_format.h"
#include "error.h"
#include "flv.h"

RtmpFormat::RtmpFormat()
{

}

RtmpFormat::~RtmpFormat()
{

}

error RtmpFormat::OnMetadata(OnMetaDataPacket *meta)
{
    // TODO: FIXME: Try to initialize format from metadata.
    return SUCCESS;
}

error RtmpFormat::OnAudio(SharedPtrMessage *shared_audio)
{
    SharedPtrMessage* msg = shared_audio;
    char* data = msg->m_payload;
    int size = msg->m_size;

    return Format::OnAudio(msg->m_timestamp, data, size);
}

error RtmpFormat::OnAudio(int64_t timestamp, char *data, int size)
{
    return Format::OnAudio(timestamp, data, size);
}

error RtmpFormat::OnVideo(SharedPtrMessage *shared_video)
{
    SharedPtrMessage* msg = shared_video;
    char* data = msg->m_payload;
    int size = msg->m_size;

    return Format::OnVideo(msg->m_timestamp, data, size);
}

error RtmpFormat::OnVideo(int64_t timestamp, char *data, int size)
{
    return Format::OnVideo(timestamp, data, size);
}

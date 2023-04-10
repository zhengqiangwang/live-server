#ifndef PROTOCOL_FORMAT_H
#define PROTOCOL_FORMAT_H


#include "log.h"
#include "codec.h"
#include <cstdint>

class OnMetaDataPacket;
class SharedPtrMessage;

/**
 * Create special structure from RTMP stream, for example, the metadata.
 */
class RtmpFormat : public Format
{
public:
    RtmpFormat();
    virtual ~RtmpFormat();
public:
    // Initialize the format from metadata, optional.
    virtual error OnMetadata(OnMetaDataPacket* meta);
    // When got a parsed audio packet.
    virtual error OnAudio(SharedPtrMessage* shared_audio);
    virtual error OnAudio(int64_t timestamp, char* data, int size);
    // When got a parsed video packet.
    virtual error OnVideo(SharedPtrMessage* shared_video);
    virtual error OnVideo(int64_t timestamp, char* data, int size);
};

#endif // PROTOCOL_FORMAT_H

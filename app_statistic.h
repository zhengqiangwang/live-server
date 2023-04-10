#ifndef APP_STATISTIC_H
#define APP_STATISTIC_H


#include "codec.h"
#include "core_time.h"
#include "log.h"
#include "protocol_rtmp_stack.h"
#include <string>
#include <map>

class Kbps;
class WallClock;
class Request;
class IExpire;
class JsonObject;
class JsonArray;
class IKbpsDelta;
class ClsSugar;
class ClsSugars;
class Pps;

struct StatisticVhost
{
public:
    std::string m_id;
    std::string m_vhost;
    int m_nbStreams;
    int m_nbClients;
public:
    // The vhost total kbps.
    Kbps* m_kbps;
public:
    StatisticVhost();
    virtual ~StatisticVhost();
public:
    virtual error Dumps(JsonObject* obj);
};

struct StatisticStream
{
public:
    std::string m_id;
    StatisticVhost* m_vhost;
    std::string m_app;
    std::string m_stream;
    std::string m_url;
    std::string m_tcUrl;
    bool m_active;
    // The publisher connection id.
    std::string m_publisherId;
    int m_nbClients;
public:
    // The stream total kbps.
    Kbps* m_kbps;
    // The fps of stream.
    Pps* m_frames;
public:
    bool m_hasVideo;
    VideoCodecId m_vcodec;
    // The profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    AvcProfile m_avcProfile;
    // The level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    AvcLevel m_avcLevel;
    // The width and height in codec info.
    int m_width;
    int m_height;
public:
    bool m_hasAudio;
    AudioCodecId m_acodec;
    AudioSampleRate m_asampleRate;
    AudioChannels m_asoundType;
    // The audio specified
    // audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
    // 1.5.1.1 Audio object type definition, page 23,
    //           in ISO_IEC_14496-3-AAC-2001.pdf.
    AacObjectType m_aacObject;
public:
    StatisticStream();
    virtual ~StatisticStream();
public:
    virtual error Dumps(JsonObject* obj);
public:
    // Publish the stream, id is the publisher.
    virtual void Publish(std::string id);
    // Close the stream.
    virtual void Close();
};

struct StatisticClient
{
public:
    // For HTTP-API to kickoff this connection by expiring it.
    IExpire* m_conn;
public:
    StatisticStream* m_stream;
    Request* m_req;
    RtmpConnType m_type;
    std::string m_id;
    utime_t m_create;
public:
    // The stream total kbps.
    Kbps* m_kbps;
public:
    StatisticClient();
    virtual ~StatisticClient();
public:
    virtual error Dumps(JsonObject* obj);
};

class Statistic
{
private:
    static Statistic *_instance;
    // The id to identify the sever.
    std::string m_serverId;
private:
    // The key: vhost id, value: vhost object.
    std::map<std::string, StatisticVhost*> m_vhosts;
    // The key: vhost url, value: vhost Object.
    // @remark a fast index for vhosts.
    std::map<std::string, StatisticVhost*> m_rvhosts;
private:
    // The key: stream id, value: stream Object.
    std::map<std::string, StatisticStream*> m_streams;
    // The key: stream url, value: stream Object.
    // @remark a fast index for streams.
    std::map<std::string, StatisticStream*> m_rstreams;
private:
    // The key: client id, value: stream object.
    std::map<std::string, StatisticClient*> m_clients;
    // The server total kbps.
    Kbps* m_kbps;
private:
    // The total of clients connections.
    int64_t m_nbClients;
    // The total of clients errors.
    int64_t m_nbErrs;
private:
    Statistic();
    virtual ~Statistic();
public:
    static Statistic *Instance();
public:
    virtual StatisticVhost* FindVhostById(std::string vid);
    virtual StatisticVhost* FindVhostByName(std::string name);
    virtual StatisticStream* FindStream(std::string sid);
    virtual StatisticStream* FindStreamByUrl(std::string url);
    virtual StatisticClient* FindClient(std::string client_id);
public:
    // When got video info for stream.
    virtual error OnVideoInfo(Request* req, VideoCodecId vcodec, AvcProfile avc_profile,
        AvcLevel avc_level, int width, int height);
    // When got audio info for stream.
    virtual error OnAudioInfo(Request* req, AudioCodecId acodec, AudioSampleRate asample_rate,
        AudioChannels asound_type, AacObjectType aac_object);
    // When got videos, update the frames.
    // We only stat the total number of video frames.
    virtual error OnVideoFrames(Request* req, int nb_frames);
    // When publish stream.
    // @param req the request object of publish connection.
    // @param publisher_id The id of publish connection.
    virtual void OnStreamPublish(Request* req, std::string publisher_id);
    // When close stream.
    virtual void OnStreamClose(Request* req);
public:
    // When got a client to publish/play stream,
    // @param id, the client srs id.
    // @param req, the client request object.
    // @param conn, the physical absract connection object.
    // @param type, the type of connection.
    virtual error OnClient(std::string id, Request* req, IExpire* conn, RtmpConnType type);
    // Client disconnect
    // @remark the on_disconnect always call, while the on_client is call when
    //      only got the request object, so the client specified by id maybe not
    //      exists in stat.
    virtual void OnDisconnect(std::string id, error err);
private:
    // Cleanup the stream if stream is not active and for the last client.
    void CleanupStream(StatisticStream* stream);
public:
    // Sample the kbps, add delta bytes of conn.
    // Use kbps_sample() to get all result of kbps stat.
    virtual void KbpsAddDelta(std::string id, IKbpsDelta* delta);
    // Calc the result for all kbps.
    virtual void KbpsSample();
public:
    // Get the server id, used to identify the server.
    // For example, when restart, the server id must changed.
    virtual std::string ServerId();
    // Dumps the vhosts to amf0 array.
    virtual error DumpsVhosts(JsonArray* arr);
    // Dumps the streams to amf0 array.
    // @param start the start index, from 0.
    // @param count the max count of streams to dump.
    virtual error DumpsStreams(JsonArray* arr, int start, int count);
    // Dumps the clients to amf0 array
    // @param start the start index, from 0.
    // @param count the max count of clients to dump.
    virtual error DumpsClients(JsonArray* arr, int start, int count);
    // Dumps the hints about SRS server.
    void DumpsHintsKv(std::stringstream & ss);
public:
    // Dumps the CLS summary.
    void DumpsClsSummaries(ClsSugar* sugar);
    void DumpsClsStreams(ClsSugars* sugars);
private:
    virtual StatisticVhost* CreateVhost(Request* req);
    virtual StatisticStream* CreateStream(StatisticVhost* vhost, Request* req);
public:
    // Dumps exporter metrics.
    virtual error DumpsMetrics(int64_t& send_bytes, int64_t& recv_bytes, int64_t& nstreams, int64_t& nclients, int64_t& total_nclients, int64_t& nerrs);
};

// Generate a random string id, with constant prefix.
extern std::string GenerateStatVid();


#endif // APP_STATISTIC_H

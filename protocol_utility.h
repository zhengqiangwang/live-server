#ifndef PROTOCOL_UTILITY_H
#define PROTOCOL_UTILITY_H


#include "core.h"
#include <bits/types/struct_iovec.h>
#include <map>
#include <string>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <vector>
#include <sstream>

class IHttpMessage;
class MessageHeader;
class SharedPtrMessage;
class CommonMessage;
class IProtocolReadWriter;
class IReader;

//parse the tcUrl, output the schema, host, vhost, app and port
//@param tcUrl, the input tcUrl, for example rtmp://192.168.1.10:19350/live?vhost=vhost.ossrs.net
//@param schema, for example, rtmp
//@param host, for example, 192.168.1.10
//@param vhost,for example, vhost.ossrs.net.
    //vhost default to host, when user not set vhost in query of app.
//@param app, for example, live
//@param port, for example, 19350
//      default to 1935 if not specified.
//@param param, for example, vhost=vhost.ossrs.net
//@remark the param stream is input and output param, that is:
//  input: tcUrl + stream
//  output: schema, host, vhost, app, stream, port, param
extern void DiscoveryTcUrl(std::string tcUrl, std::string& schema, std::string& host, std::string& vhost, std::string& app, std::string& stream, int& port, std::string& param);

//guessing stream by app and param, to make obs happy. for example:
//  rtmp://ip/live/livestream
//  rtmp://ip/live/livestream?secret=xxx
//  rtmp://ip/live?secret=xxx/livestream
extern void GuessStreamByApp(std::string& app, std::string& param, std::string& stream);

//parse query string to map(k, v).
//must format as key=value&...&keyN=valueN
extern void ParseQueryString(std::string q, std::map<std::string, std::string>& query);

//generate random data for handshake
extern void RandomGenerate(char* bytes, int size);

//generate random string [0-9a-z] in size of len bytes.
extern std::string RandomStr(int len);

//generate random value
extern long Random();

//generate the tcUrl without param.
extern std::string GenerateTcUrl(std::string schema, std::string host, std::string vhost, std::string app, int port);

//generate the stream with param
extern std::string GenerateStreamWithQuery(std::string host, std::string vhost, std::string stream, std::string param, bool with_vhost = true);

//create shared ptr message from bytes.
extern error RtmpCreateMsg(char type, uint32_t timestamp, char* data, int size, int stream_id, SharedPtrMessage** ppmsg);
extern error RtmpCreateMsg(char type, uint32_t timestamp, char* data, int size, int stream_id, CommonMessage** ppmsg);

//get the stream identify, vhost/app/stream
extern std::string GenerateStreamUrl(std::string vhost, std::string app, std::string stream);

//parse the rtmp url to tcUrl/stream,
//for example, rtmp://v.ossrs.net/live/livestream to
//  tcUrl: rtmp://v.ossrs.net/live
//  stream: livestream
extern void ParseRtmpUrl(std::string url, std::string& tcUrl, std::string& stream);

//generate the rtmp url, for instance, rtmp://server:port/app/stream?param
extern std::string GenerateRtmpUrl(std::string server, int port, std::string host, std::string vhost, std::string app, std::string stream, std::string param);

//write large numbers of iovs.
extern error WriteLargeIovs(IProtocolReadWriter* skt, iovec* iovs, int size, ssize_t* pnwrite = NULL);

//join string in vector with indicated separator
template <typename T>
std::string JoinVectorString(std::vector<T>& vs, std::string separator)
{
    std::stringstream ss;

    for(int i = 0; i < (int)vs.size(); i++){
        ss << vs.at(i);
        if(i != (int)vs.size() - 1){
            ss << separator;
        }
    }

    return ss.str();
}

//whether domain is an IPV4 address
extern bool IsIpv4(std::string domain);

//convert an IPv4 from string to uint32_t.
extern uint32_t Ipv4ToNum(std::string ip);

//whether the IPv4 is in an IP mask
extern bool Ipv4WithinMask(std::string ip, std::string network, std::string mask);

//get the CIDR(Classless Inter-Domain Routing) mask for a network address
extern std::string GetCidrMask(std::string network_address);

//get the cidr(classless Inter-Domain Routing) IPv4 for a network address
extern std::string GetCidrIpv4(std::string network_address);

//whether the url is starts with http:// of https://
extern bool StringIsHttp(std::string url);
extern bool StringIsRtmp(std::string url);

//whether string is digit number, only number consist return true other return false
extern bool IsDigitNumber(std::string str);

//get local ip, fill to @param ips
struct IPAddress
{
    //the network interface name, such as eth0, en0
    std::string m_ifname;
    //the ip v4 or v6 address
    std::string m_ip;
    //whether the ip is IPv4 address
    bool m_isIpv4;
    //whether the ip is internet public IP address
    bool m_isInternet;
    //whether the ip is loopback, such as 127.0.0.1
    bool m_isLoopback;
};

extern std::vector<IPAddress*>& GetLocalIps();

//get local public ip, empty string if no public internet address found.
extern std::string GetPublicInternetAddress(bool ipv4_only = false);

//detect whether specified device is internet public address
extern bool NetDeviceIsInternet(std::string ifname);
extern bool NetDeviceIsInternet(const sockaddr* addr);

//Get the original ip from query and header by proxy
extern std::string GetOriginalIp(IHttpMessage* r);

//get hostname
extern std::string GetSystemHostname(void);

//read all ccontent util EOF
extern error IoutilReadAll(IReader* in, std::string& content);

#if defined(__linux__) || defined(OSX)
//get system uname info
extern utsname* GetSystemUnameInfo();
#endif

//input need encrypt data and output date accept buffer,the out length is min 64, return the out data len
int Sha256Encrypt(std::string in, std::string& out);

int Base64Encode(const char *in, int len, std::string& out);
int Base64Decode(const char* in, int len, std::string& out);
std::string AesEncode(const std::string& password, const std::string& data);
std::string AesDecode(const std::string& password, const std::string& data);

#endif // PROTOCOL_UTILITY_H

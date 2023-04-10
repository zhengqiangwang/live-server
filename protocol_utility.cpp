#include "protocol_utility.h"
#include "codec.h"
#include "consts.h"
#include "error.h"
#include "flv.h"
#include "log.h"
#include "protocol_io.h"
#include "protocol_http_stack.h"
#include "core_autofree.h"
#include "utility.h"
#include <cstring>
#include <math.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <openssl/ssl.h>
#include <openssl/aes.h>
#include <openssl/crypto.h>

void DiscoveryTcUrl(std::string tcUrl, std::string &schema, std::string &host, std::string &vhost, std::string &app, std::string &stream, int &port, std::string &param)
{
    //standard url is:
    //  rtmp://ip/app/app2/stream?k=v
    //where after last slash is stream.
    std::string fullUrl = tcUrl;
    fullUrl += stream.empty() ? "/" : (stream.at(0) == '/' ? stream : "/" + stream);
    fullUrl += param.empty() ? "" : (param.at(0) == '?' ? param : "?" + param);

    //first, we covert the FMLE url to standard url:
    //  rtmp://ip/app/app2?k=v/stream
    size_t pos_query = fullUrl.find("?");
    size_t pos_rslash = fullUrl.rfind("/");
    if(pos_rslash != std::string::npos && pos_query != std::string::npos && pos_query < pos_rslash){
        fullUrl = fullUrl.substr(0, pos_query)  //rtmp://ip/app/app2
                + fullUrl.substr(pos_rslash)   // /stream
                + fullUrl.substr(pos_query, pos_rslash - pos_query); // ?k=v
    }

    //remove the _definst_ of FMLE URL
    if(fullUrl.find("/_definst_") != std::string::npos){
        fullUrl = StringReplace(fullUrl, "/_definst_", "");
    }

    // Parse the standard URL.
    HttpUri uri;
    error err = SUCCESS;
    if ((err = uri.Initialize(fullUrl)) != SUCCESS) {
        warn("Ignore parse url=%s err %s", fullUrl.c_str(), ERRORDESC(err).c_str());
        Freep(err);
        return;
    }

    schema = uri.GetSchema();
    host = uri.GetHost();
    port = uri.GetPort();
    stream = PathBasename(uri.GetPath());
    param = uri.GetQuery().empty() ? "" : "?" + uri.GetQuery();

    // Parse app without the prefix slash.
    app = PathDirname(uri.GetPath());
    if (!app.empty() && app.at(0) == '/') app = app.substr(1);
    if (app.empty()) app = CONSTS_RTMP_DEFAULT_APP;

    // Try to parse vhost from query, or use host if not specified.
    std::string vhost_in_query = uri.GetQueryByKey("vhost");
    if (vhost_in_query.empty()) vhost_in_query = uri.GetQueryByKey("domain");
    if (!vhost_in_query.empty() && vhost_in_query != CONSTS_RTMP_DEFAULT_VHOST) vhost = vhost_in_query;
    if (vhost.empty()) vhost = host;

    // Only one param, the default vhost, clear it.
    if (param.find("&") == std::string::npos && vhost_in_query == CONSTS_RTMP_DEFAULT_VHOST) {
        param = "";
    }
}

void GuessStreamByApp(std::string& app, std::string& param, std::string& stream)
{
    size_t pos = std::string::npos;

    // Extract stream from app, if contains slash.
    if ((pos = app.find("/")) != std::string::npos) {
        stream = app.substr(pos + 1);
        app = app.substr(0, pos);

        if ((pos = stream.find("?")) != std::string::npos) {
            param = stream.substr(pos);
            stream = stream.substr(0, pos);
        }
        return;
    }

    // Extract stream from param, if contains slash.
    if ((pos = param.find("/")) != std::string::npos) {
        stream = param.substr(pos + 1);
        param = param.substr(0, pos);
    }
}

void ParseQueryString(std::string q, std::map<std::string,std::string>& query)
{
    // query string flags.
    static std::vector<std::string> flags;
    if (flags.empty()) {
        flags.push_back("=");
        flags.push_back(",");
        flags.push_back("&&");
        flags.push_back("&");
        flags.push_back(";");
    }

    std::vector<std::string> kvs = StringSplit(q, flags);
    for (int i = 0; i < (int)kvs.size(); i+=2) {
        std::string k = kvs.at(i);
        std::string v = (i < (int)kvs.size() - 1)? kvs.at(i+1):"";

        query[k] = v;
    }
}

void RandomGenerate(char* bytes, int size)
{
    for (int i = 0; i < size; i++) {
        // the common value in [0x0f, 0xf0]
        bytes[i] = 0x0f + (Random() % (256 - 0x0f - 0x0f));
    }
}

std::string RandomStr(int len)
{
    static std::string random_table = "01234567890123456789012345678901234567890123456789abcdefghijklmnopqrstuvwxyz";

    std::string ret;
    ret.reserve(len);
    for(int i = 0; i < len; ++i){
        ret.append(1, random_table[Random() % random_table.size()]);
    }

    return ret;
}

long Random()
{
    static bool random_initialized = false;
    if(!random_initialized){
        random_initialized = true;
        ::srandom((unsigned long)(UpdateSystemTime() | (::getpid() << 13)));
    }

    return random();
}

std::string GenerateTcUrl(std::string schema, std::string host, std::string vhost, std::string app, int port)
{
    std::string tcUrl = schema + "://";

    if (vhost == CONSTS_RTMP_DEFAULT_VHOST) {
        tcUrl += host.empty() ? CONSTS_RTMP_DEFAULT_VHOST : host;
    } else {
        tcUrl += vhost;
    }

    if (port && port != CONSTS_RTMP_DEFAULT_PORT) {
        tcUrl += ":" + Int2Str(port);
    }

    tcUrl += "/" + app;

    return tcUrl;
}

std::string GenerateStreamWithQuery(std::string host, std::string vhost, std::string stream, std::string param, bool with_vhost)
{
    std::string url = stream;
    std::string query = param;

    // If no vhost in param, try to append one.
    std::string guessVhost;
    if (query.find("vhost=") == std::string::npos) {
        if (vhost != CONSTS_RTMP_DEFAULT_VHOST) {
            guessVhost = vhost;
        } else if (!IsIpv4(host)) {
            guessVhost = host;
        }
    }

    // Well, if vhost exists, always append in query string.
    if (!guessVhost.empty() && query.find("vhost=") == std::string::npos) {
        query += "&vhost=" + guessVhost;
    }

    // If not pass in query, remove it.
    if (!with_vhost) {
        size_t pos = query.find("&vhost=");
        if (pos == std::string::npos) {
            pos = query.find("vhost=");
        }

        size_t end = query.find("&", pos + 1);
        if (end == std::string::npos) {
            end = query.length();
        }

        if (pos != std::string::npos && end != std::string::npos && end > pos) {
            query = query.substr(0, pos) + query.substr(end);
        }
    }

    // Remove the start & when param is empty.
    query = StringTrimStart(query, "&");

    // Prefix query with ?.
    if (!query.empty() && !StringStartsWith(query, "?")) {
        url += "?";
    }

    // Append query to url.
    if (!query.empty()) {
        url += query;
    }

    return url;
}

template<typename T>
error DoRtmpCreateMsg(char type, uint32_t timestamp, char* data, int size, int stream_id, T** ppmsg)
{
    error err = SUCCESS;

    *ppmsg = NULL;
    T* msg = NULL;

    if (type == FrameTypeAudio) {
        MessageHeader header;
        header.InitializeAudio(size, timestamp, stream_id);

        msg = new T();
        if ((err = msg->Create(&header, data, size)) != SUCCESS) {
            Freep(msg);
            return ERRORWRAP(err, "create message");
        }
    } else if (type == FrameTypeVideo) {
        MessageHeader header;
        header.InitializeVideo(size, timestamp, stream_id);

        msg = new T();
        if ((err = msg->Create(&header, data, size)) != SUCCESS) {
            Freep(msg);
            return ERRORWRAP(err, "create message");
        }
    } else if (type == FrameTypeScript) {
        MessageHeader header;
        header.InitializeAmf0Script(size, stream_id);

        msg = new T();
        if ((err = msg->Create(&header, data, size)) != SUCCESS) {
            Freep(msg);
            return ERRORWRAP(err, "create message");
        }
    } else {
        return ERRORNEW(ERROR_STREAM_CASTER_FLV_TAG, "unknown tag=%#x", (uint8_t)type);
    }

    *ppmsg = msg;

    return err;
}

error RtmpCreateMsg(char type, uint32_t timestamp, char* data, int size, int stream_id, SharedPtrMessage** ppmsg)
{
    error err = SUCCESS;

    // only when failed, we must free the data.
    if ((err = DoRtmpCreateMsg(type, timestamp, data, size, stream_id, ppmsg)) != SUCCESS) {
        Freepa(data);
        return ERRORWRAP(err, "create message");
    }

    return err;
}

error RtmpCreateMsg(char type, uint32_t timestamp, char* data, int size, int stream_id, CommonMessage** ppmsg)
{
    error err = SUCCESS;

    // only when failed, we must free the data.
    if ((err = DoRtmpCreateMsg(type, timestamp, data, size, stream_id, ppmsg)) != SUCCESS) {
        Freepa(data);
        return ERRORWRAP(err, "create message");
    }

    return err;
}

std::string GenerateStreamUrl(std::string vhost, std::string app, std::string stream)
{
    std::string url = "";

    if (CONSTS_RTMP_DEFAULT_VHOST != vhost){
        url += vhost;
    }
    url += "/" + app;
    // Note that we ignore any extension.
    url += "/" + PathFilename(stream);

    return url;
}

void ParseRtmpUrl(std::string url, std::string& tcUrl, std::string& stream)
{
    size_t pos;

    if ((pos = url.rfind("/")) != std::string::npos) {
        stream = url.substr(pos + 1);
        tcUrl = url.substr(0, pos);
    } else {
        tcUrl = url;
    }
}

std::string GenerateRtmpUrl(std::string server, int port, std::string host, std::string vhost, std::string app, std::string stream, std::string param)
{
    std::string tcUrl = "rtmp://" + server + ":" + Int2Str(port) + "/"  + app;
    std::string streamWithQuery = GenerateStreamWithQuery(host, vhost, stream, param);
    std::string url = tcUrl + "/" + streamWithQuery;
    return url;
}

error WriteLargeIovs(IProtocolReadWriter* skt, iovec* iovs, int size, ssize_t* pnwrite)
{
    error err = SUCCESS;

    // the limits of writev iovs.
#ifndef _WIN32
    // for linux, generally it's 1024.
    static int limits = (int)sysconf(_SC_IOV_MAX);
#else
    static int limits = 1024;
#endif

    // send in a time.
    if (size <= limits) {
        if ((err = skt->Writev(iovs, size, pnwrite)) != SUCCESS) {
            return ERRORWRAP(err, "writev");
        }
        return err;
    }

    // send in multiple times.
    int cur_iov = 0;
    ssize_t nwrite = 0;
    while (cur_iov < size) {
        int cur_count = MIN(limits, size - cur_iov);
        if ((err = skt->Writev(iovs + cur_iov, cur_count, &nwrite)) != SUCCESS) {
            return ERRORWRAP(err, "writev");
        }
        cur_iov += cur_count;
        if (pnwrite) {
            *pnwrite += nwrite;
        }
    }

    return err;
}

bool IsIpv4(std::string domain)
{
    for (int i = 0; i < (int)domain.length(); i++) {
        char ch = domain.at(i);
        if (ch == '.') {
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            continue;
        }

        return false;
    }

    return true;
}

uint32_t Ipv4ToNum(std::string ip) {
    uint32_t addr = 0;
    if (inet_pton(AF_INET, ip.c_str(), &addr) <= 0) {
        return 0;
    }

    return ntohl(addr);
}

bool Ipv4WithinMask(std::string ip, std::string network, std::string mask) {
    uint32_t ip_addr = Ipv4ToNum(ip);
    uint32_t mask_addr = Ipv4ToNum(mask);
    uint32_t network_addr = Ipv4ToNum(network);

    return (ip_addr & mask_addr) == (network_addr & mask_addr);
}

static struct CIDR_VALUE {
    size_t length;
    std::string mask;
} CIDR_VALUES[32] = {
    { 1,  "128.0.0.0" },
    { 2,  "192.0.0.0" },
    { 3,  "224.0.0.0" },
    { 4,  "240.0.0.0" },
    { 5,  "248.0.0.0" },
    { 6,  "252.0.0.0" },
    { 7,  "254.0.0.0" },
    { 8,  "255.0.0.0" },
    { 9,  "255.128.0.0" },
    { 10, "255.192.0.0" },
    { 11, "255.224.0.0" },
    { 12, "255.240.0.0" },
    { 13, "255.248.0.0" },
    { 14, "255.252.0.0" },
    { 15, "255.254.0.0" },
    { 16, "255.255.0.0" },
    { 17, "255.255.128.0" },
    { 18, "255.255.192.0" },
    { 19, "255.255.224.0" },
    { 20, "255.255.240.0" },
    { 21, "255.255.248.0" },
    { 22, "255.255.252.0" },
    { 23, "255.255.254.0" },
    { 24, "255.255.255.0" },
    { 25, "255.255.255.128" },
    { 26, "255.255.255.192" },
    { 27, "255.255.255.224" },
    { 28, "255.255.255.240" },
    { 29, "255.255.255.248" },
    { 30, "255.255.255.252" },
    { 31, "255.255.255.254" },
    { 32, "255.255.255.255" },
};

std::string GetCidrMask(std::string network_address) {
    std::string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == std::string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (IsIpv4(network_address)) {
            return CIDR_VALUES[32-1].mask;
        }
        return "";
    }

    // Change here to include IPv6 support.
    std::string is_ipv4_address = network_address.substr(0, delimiter_position);
    if (!IsIpv4(is_ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    std::string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    size_t cidr_length_num = 31;
    try {
        cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return CIDR_VALUES[cidr_length_num-1].mask;
}

std::string GetCidrIpv4(std::string network_address) {
    std::string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == std::string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (IsIpv4(network_address)) {
            return network_address;
        }
        return "";
    }

    // Change here to include IPv6 support.
    std::string ipv4_address = network_address.substr(0, delimiter_position);
    if (!IsIpv4(ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    std::string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    try {
        size_t cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return ipv4_address;
}

bool StringIsHttp(std::string url)
{
    return StringStartsWith(url, "http://", "https://");
}

bool StringIsRtmp(std::string url)
{
    return StringStartsWith(url, "rtmp://");
}

bool IsDigitNumber(std::string str)
{
    if (str.empty()) {
        return false;
    }

    const char* p = str.c_str();
    const char* p_end = str.data() + str.length();
    for (; p < p_end; p++) {
        if (*p != '0') {
            break;
        }
    }
    if (p == p_end) {
        return true;
    }

    int64_t v = ::atoll(p);
    int64_t powv = (int64_t)pow(10, p_end - p - 1);
    return  v / powv >= 1 && v / powv <= 9;
}

// we detect all network device as internet or intranet device, by its ip address.
//      key is device name, for instance, eth0
//      value is whether internet, for instance, true.
static std::map<std::string, bool> device_ifs;

bool NetDeviceIsInternet(std::string ifname)
{
    info("check ifname=%s", ifname.c_str());

    if (device_ifs.find(ifname) == device_ifs.end()) {
        return false;
    }
    return device_ifs[ifname];
}

bool NetDeviceIsInternet(const sockaddr* addr)
{
    if(addr->sa_family == AF_INET) {
        const in_addr inaddr = ((sockaddr_in*)addr)->sin_addr;
        const uint32_t addr_h = ntohl(inaddr.s_addr);

        // lo, 127.0.0.0-127.0.0.1
        if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
            return false;
        }

        // Class A 10.0.0.0-10.255.255.255
        if (addr_h >= 0x0a000000 && addr_h <= 0x0affffff) {
            return false;
        }

        // Class B 172.16.0.0-172.31.255.255
        if (addr_h >= 0xac100000 && addr_h <= 0xac1fffff) {
            return false;
        }

        // Class C 192.168.0.0-192.168.255.255
        if (addr_h >= 0xc0a80000 && addr_h <= 0xc0a8ffff) {
            return false;
        }
    } else if(addr->sa_family == AF_INET6) {
        const sockaddr_in6* a6 = (const sockaddr_in6*)addr;

        // IPv6 loopback is ::1
        if (IN6_IS_ADDR_LOOPBACK(&a6->sin6_addr)) {
            return false;
        }

        // IPv6 unspecified is ::
        if (IN6_IS_ADDR_UNSPECIFIED(&a6->sin6_addr)) {
            return false;
        }

        // From IPv4, you might know APIPA (Automatic Private IP Addressing) or AutoNet.
        // Whenever automatic IP configuration through DHCP fails.
        // The prefix of a site-local address is FE80::/10.
        if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr)) {
            return false;
        }

        // Site-local addresses are equivalent to private IP addresses in IPv4.
        // The prefix of a site-local address is FEC0::/10.
        // https://4sysops.com/archives/ipv6-tutorial-part-6-site-local-addresses-and-link-local-addresses/
        if (IN6_IS_ADDR_SITELOCAL(&a6->sin6_addr)) {
            return false;
        }

        // Others.
        if (IN6_IS_ADDR_MULTICAST(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_NODELOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_LINKLOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_SITELOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_ORGLOCAL(&a6->sin6_addr)) {
            return false;
        }
        if (IN6_IS_ADDR_MC_GLOBAL(&a6->sin6_addr)) {
            return false;
        }
    }

    return true;
}

std::vector<IPAddress*> system_ips;

void DiscoverNetworkIface(ifaddrs* cur, std::vector<IPAddress*>& ips, std::stringstream& ss0, std::stringstream& ss1, bool ipv6, bool loopback)
{
    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo(cur->ifa_addr, sizeof(sockaddr_storage), h, nbh, NULL, 0, NI_NUMERICHOST);
    if(r0) {
        warn("convert local ip failed: %s", gai_strerror(r0));
        return;
    }

    std::string ip(saddr, strlen(saddr));
    ss0 << ", iface[" << (int)ips.size() << "] " << cur->ifa_name << " " << (ipv6? "ipv6":"ipv4")
        << " 0x" << std::hex << cur->ifa_flags  << std::dec << " " << ip;

    IPAddress* ip_address = new IPAddress();
    ip_address->m_ip = ip;
    ip_address->m_isIpv4 = !ipv6;
    ip_address->m_isLoopback = loopback;
    ip_address->m_ifname = cur->ifa_name;
    ip_address->m_isInternet = NetDeviceIsInternet(cur->ifa_addr);
    ips.push_back(ip_address);

    // set the device internet status.
    if (!ip_address->m_isInternet) {
        ss1 << ", intranet ";
        device_ifs[cur->ifa_name] = false;
    } else {
        ss1 << ", internet ";
        device_ifs[cur->ifa_name] = true;
    }
    ss1 << cur->ifa_name << " " << ip;
}

void retrieve_local_ips()
{
    std::vector<IPAddress*>& ips = system_ips;

    // Release previous IPs.
    for (int i = 0; i < (int)ips.size(); i++) {
        IPAddress* ip = ips[i];
        Freep(ip);
    }
    ips.clear();

    // Get the addresses.
    ifaddrs* ifap;
    if (getifaddrs(&ifap) == -1) {
        warn("retrieve local ips, getifaddrs failed.");
        return;
    }

    std::stringstream ss0;
    ss0 << "ips";

    std::stringstream ss1;
    ss1 << "devices";

    // Discover IPv4 first.
    for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
        ifaddrs* cur = p;

        // Ignore if no address for this interface.
        // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
        if (!cur->ifa_addr) {
            continue;
        }

        // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
        // @see: https://github.com/ossrs/srs/issues/141
        bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
        bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
        // Ignore IFF_PROMISC(Interface is in promiscuous mode), which may be set by Wireshark.
        bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_LOOPBACK) || (cur->ifa_flags & IFF_POINTOPOINT);
        bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
        if (ipv4 && ready && !ignored) {
            DiscoverNetworkIface(cur, ips, ss0, ss1, false, loopback);
        }
    }

    // Then, discover IPv6 addresses.
    for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
        ifaddrs* cur = p;

        // Ignore if no address for this interface.
        // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
        if (!cur->ifa_addr) {
            continue;
        }

        // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
        // @see: https://github.com/ossrs/srs/issues/141
        bool ipv6 = (cur->ifa_addr->sa_family == AF_INET6);
        bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
        bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC) || (cur->ifa_flags & IFF_LOOPBACK);
        bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
        if (ipv6 && ready && !ignored) {
            DiscoverNetworkIface(cur, ips, ss0, ss1, true, loopback);
        }
    }

    // If empty, disover IPv4 loopback.
    if (ips.empty()) {
        for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
            ifaddrs* cur = p;

            // Ignore if no address for this interface.
            // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
            if (!cur->ifa_addr) {
                continue;
            }

            // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
            // @see: https://github.com/ossrs/srs/issues/141
            bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
            bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
            bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC);
            bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
            if (ipv4 && ready && !ignored) {
                DiscoverNetworkIface(cur, ips, ss0, ss1, false, loopback);
            }
        }
    }

    trace("%s", ss0.str().c_str());
    trace("%s", ss1.str().c_str());

    freeifaddrs(ifap);
}


std::vector<IPAddress *> &GetLocalIps()
{
    if (system_ips.empty()) {
        retrieve_local_ips();
    }

    return system_ips;
}

std::string _public_internet_address;

std::string GetPublicInternetAddress(bool ipv4_only)
{
    if (!_public_internet_address.empty()) {
        return _public_internet_address;
    }

    std::vector<IPAddress*>& ips = GetLocalIps();

    // find the best match public address.
    for (int i = 0; i < (int)ips.size(); i++) {
        IPAddress* ip = ips[i];
        if (!ip->m_isInternet) {
            continue;
        }
        if (ipv4_only && !ip->m_isIpv4) {
            continue;
        }

        warn("use public address as ip: %s, ifname=%s", ip->m_ip.c_str(), ip->m_ifname.c_str());
        _public_internet_address = ip->m_ip;
        return ip->m_ip;
    }

    // no public address, use private address.
    for (int i = 0; i < (int)ips.size(); i++) {
        IPAddress* ip = ips[i];
        if (ip->m_isLoopback) {
            continue;
        }
        if (ipv4_only && !ip->m_isIpv4) {
            continue;
        }

        warn("use private address as ip: %s, ifname=%s", ip->m_ip.c_str(), ip->m_ifname.c_str());
        _public_internet_address = ip->m_ip;
        return ip->m_ip;
    }

    // Finally, use first whatever kind of address.
    if (!ips.empty() && _public_internet_address.empty()) {
        IPAddress* ip = ips[0];

        warn("use first address as ip: %s, ifname=%s", ip->m_ip.c_str(), ip->m_ifname.c_str());
        _public_internet_address = ip->m_ip;
        return ip->m_ip;
    }

    return "";
}


std::string GetOriginalIp(IHttpMessage *r)
{
    HttpHeader* h = r->Header();

    std::string x_forwarded_for = h->Get("X-Forwarded-For");
    if (!x_forwarded_for.empty()) {
        size_t pos = std::string::npos;
        if ((pos = x_forwarded_for.find(",")) == std::string::npos) {
            return x_forwarded_for;
        }
        return x_forwarded_for.substr(0, pos);
    }

    std::string x_real_ip = h->Get("X-Real-IP");
    if (!x_real_ip.empty()) {
        size_t pos = std::string::npos;
        if ((pos = x_real_ip.find(":")) == std::string::npos) {
            return x_real_ip;
        }
        return x_real_ip.substr(0, pos);
    }

    return "";
}

std::string _system_hostname;

std::string GetSystemHostname()
{
    if (!_system_hostname.empty()) {
        return _system_hostname;
    }

    char buf[256];
    if (-1 == gethostname(buf, sizeof(buf))) {
        warn("gethostbyname fail");
        return "";
    }

    _system_hostname = std::string(buf);
    return _system_hostname;
}

error IoutilReadAll(IReader *in, std::string &content)
{
    error err = SUCCESS;

    // Cache to read, it might cause coroutine switch, so we use local cache here.
    char* buf = new char[HTTP_READ_CACHE_BYTES];
    AutoFreeA(char, buf);

    // Whatever, read util EOF.
    while (true) {
        ssize_t nb_read = 0;
        if ((err = in->Read(buf, HTTP_READ_CACHE_BYTES, &nb_read)) != SUCCESS) {
            int code = ERRORCODE(err);
            if (code == ERROR_SYSTEM_FILE_EOF || code == ERROR_HTTP_RESPONSE_EOF || code == ERROR_HTTP_REQUEST_EOF
                || code == ERROR_HTTP_STREAM_EOF
            ) {
                Freep(err);
                return err;
            }
            return ERRORWRAP(err, "read body");
        }

        if (nb_read > 0) {
            content.append(buf, nb_read);
        }
    }

    return err;
}

#if defined(__linux__) || defined(SRS_OSX)

utsname *GetSystemUnameInfo()
{
    static utsname* system_info = NULL;

    if (system_info != NULL) {
        return system_info;
    }

    system_info = new utsname();
    memset(system_info, 0, sizeof(utsname));
    if (uname(system_info) < 0) {
        warn("uname failed");
    }

    return system_info;
}
#endif

int Sha256Encrypt(std::string in, std::string& out)
{
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;


    md = EVP_get_digestbyname("SHA256");
    if (md == NULL) {
        printf("Unknown message digest %s\n", "SHA256");
        return -1;
    }

    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, in.c_str(), in.size());
    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_free(mdctx);

    out.resize(md_len);
    for(int i = 0; i < md_len; i++)
    {
        out[i] = md_value[i];
    }

    return md_len;
}

int Base64Encode(const char* in, int len, std::string& out)
{
    if (!in || len <= 0)
        return 0;
    //内存源 source
    auto mem_bio = BIO_new(BIO_s_mem());
    if (!mem_bio)return 0;

    //base64 filter
    auto b64_bio = BIO_new(BIO_f_base64());
    if (!b64_bio)
    {
        BIO_free(mem_bio);
        return 0;
    }

    //形成BIO链
    //b64-mem
    BIO_push(b64_bio, mem_bio);
    //超过64字节不添加换行（\n）,编码的数据在一行中
    // 默认结尾有换行符\n 超过64字节再添加\n
    BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

    // 写入到base64 filter 进行编码，结果会传递到链表的下一个节点
    // 到mem中读取结果(链表头部代表了整个链表)
    // BIO_write 编码 3字节=》4字节  不足3字节补充0 和 =
    // 编码数据每64字节（不确定）会加\n 换行符
    int re = BIO_write(b64_bio, in, len);
    if (re <= 0)
    {
        //情况整个链表节点
        BIO_free_all(b64_bio);
        return 0;
    }

    //刷新缓存，写入链表的mem
    BIO_flush(b64_bio);

    int outsize = 0;
    //从链表源内存读取
    BUF_MEM* p_data = 0;
    BIO_get_mem_ptr(b64_bio, &p_data);
    if (p_data)
    {
        out.resize(p_data->length);
        memcpy(out.data(), p_data->data, p_data->length);
        outsize = p_data->length;
    }
    BIO_free_all(b64_bio);
    return outsize;
}

int Base64Decode(const char* in, int len, std::string& out)
{
    if (!in || len <= 0)
        return 0;
    //内存源 （密文）
    auto mem_bio = BIO_new_mem_buf(in, len);
    if (!mem_bio)return 0;
    //base64 过滤器
    auto b64_bio = BIO_new(BIO_f_base64());
    if (!b64_bio)
    {
        BIO_free(mem_bio);
        return 0;
    }
    //形成BIO链
    BIO_push(b64_bio, mem_bio);

    //默认读取换行符做结束
    //设置后编码中如果有\n会失败
    BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

    //读取 解码 4字节转3字节
    size_t size = 0;
    out.resize(len);
    BIO_read_ex(b64_bio, out.data(),len,&size);
    BIO_free_all(b64_bio);
    out.resize(size);
    return size;
}

std::string AesDecode(const std::string &password, const std::string &data)
{
    // 这里默认将iv全置为字符0
    unsigned char iv[AES_BLOCK_SIZE] = { '0','0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0' };

    AES_KEY aes_key;
    if (AES_set_decrypt_key((const unsigned char*)password.c_str(), password.length() * 8, &aes_key) < 0)
    {
        //assert(false);
        return "";
    }
    std::string strRet;
    for (unsigned int i = 0; i < data.length() / AES_BLOCK_SIZE; i++)
    {
        std::string str16 = data.substr(i*AES_BLOCK_SIZE, AES_BLOCK_SIZE);
        unsigned char out[AES_BLOCK_SIZE];
        ::memset(out, 0, AES_BLOCK_SIZE);
        AES_cbc_encrypt((const unsigned char*)str16.c_str(), out, AES_BLOCK_SIZE, &aes_key, iv, AES_DECRYPT);
        strRet += std::string((const char*)out, AES_BLOCK_SIZE);
    }
    return strRet;
}

std::string AesEncode(const std::string &password, const std::string &data)
{
    // 这里默认将iv全置为字符0
    unsigned char iv[AES_BLOCK_SIZE] = { '0','0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0' };

    AES_KEY aes_key;
    if (AES_set_encrypt_key((const unsigned char*)password.c_str(), password.length() * 8, &aes_key) < 0)
    {
        //assert(false);
        return "";
    }
    std::string strRet;
    std::string data_bak = data;
    unsigned int data_length = data_bak.length();

    // ZeroPadding
    int padding = 0;
    if (data_bak.length() % (AES_BLOCK_SIZE) > 0)
    {
        padding = AES_BLOCK_SIZE - data_bak.length() % (AES_BLOCK_SIZE);
    }
    // 在一些软件实现中，即使是16的倍数也进行了16长度的补齐
    /*else
    {
        padding = AES_BLOCK_SIZE;
    }*/

    data_length += padding;
    while (padding > 0)
    {
        data_bak += '\0';
        padding--;
    }

    for (unsigned int i = 0; i < data_length / (AES_BLOCK_SIZE); i++)
    {
        std::string str16 = data_bak.substr(i*AES_BLOCK_SIZE, AES_BLOCK_SIZE);
        unsigned char out[AES_BLOCK_SIZE];
        ::memset(out, 0, AES_BLOCK_SIZE);
        AES_cbc_encrypt((const unsigned char*)str16.c_str(), out, AES_BLOCK_SIZE, &aes_key, iv, AES_ENCRYPT);
        strRet += std::string((const char*)out, AES_BLOCK_SIZE);
    }
    return strRet;
}

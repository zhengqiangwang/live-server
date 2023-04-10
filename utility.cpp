#include "utility.h"
#include "buffer.h"
#include "error.h"
#include "flv.h"
#include "log.h"
#include "consts.h"
#include "core_autofree.h"

#include <cstdio>
#include <algorithm>
#include <arpa/inet.h>
#include <string>
#include <cinttypes>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdarg>

#define SYS_TIME_RESOLUTION_US 300*1000

error AvcNaluReadUev(BitBuffer *stream, int32_t &v)
{
    error err = SUCCESS;

    if(stream->Empty()){
        return ERRORNEW(ERROR_AVC_NALU_UEV, "empty stream");
    }

    int leadingZeroBits = -1;
    for(int8_t b = 0; !b && !stream->Empty(); leadingZeroBits++){
        b = stream->ReadBit();
    }

    if(leadingZeroBits >= 31){
        return ERRORNEW(ERROR_AVC_NALU_UEV, "%dbits overflow 31bits", leadingZeroBits);
    }

    v = (1 << leadingZeroBits) - 1;
    for(int i = 0; i < (int)leadingZeroBits; i++){
        if(stream->Empty()){
            return ERRORNEW(ERROR_AVC_NALU_UEV, "no bytes for leadingZeroBits=%d", leadingZeroBits);
        }

        int32_t b = stream->ReadBit();
        v += b << (leadingZeroBits - 1 - i);
    }

    return err;
}

error AvcNaluReadBit(BitBuffer *stream, int8_t &v)
{
    error err = SUCCESS;

    if(stream->Empty()){
        return ERRORNEW(ERROR_AVC_NALU_UEV, "empty stream");
    }

    v = stream->ReadBit();

    return err;
}

utime_t _system_time_us_cache = 0;
utime_t _system_time_startup_time = 0;

utime_t GetSystemTime()
{
    if(_system_time_us_cache <= 0)
    {
        UpdateSystemTime();
    }

    return _system_time_us_cache;
}

utime_t GetSystemStartupTime()
{
    if(_system_time_startup_time <= 0){
        UpdateSystemTime();
    }

    return _system_time_startup_time;
}

#ifndef OSX
gettimeofday_t _gettimeofday = (gettimeofday_t)::gettimeofday;
#endif

utime_t UpdateSystemTime()
{
    timeval now;
    if(_gettimeofday(&now, nullptr) < 0){
        warn("gettimeofday failed, ignore");
        return -1;
    }

    int64_t now_us = ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;

    //for some arm os, the starttime maybe invalid, so we use relative time

    if(_system_time_us_cache <= 0){
        _system_time_startup_time = _system_time_us_cache = now_us;
        return _system_time_us_cache;
    }

    int64_t diff = now_us - _system_time_us_cache;
    diff = MAX(0, diff);
    if(diff < 0 || diff > 1000 * SYS_TIME_RESOLUTION_US){
        warn("clock jump, history=%" PRId64 "us, now=%" PRId64 "us, diff=%" PRId64 "us", _system_time_us_cache, now_us, diff);
        _system_time_startup_time += diff;
    }

    _system_time_us_cache = now_us;
    info("clock updated, startup=%" PRId64 "us, now=%" PRId64 "us", _system_time_startup_time, _system_time_us_cache);

    return _system_time_us_cache;
}

std::string AnyAddressForListener()
{
    bool ipv4 = false;
    bool ipv6 = false;

    if(true){
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(fd != -1){
            ipv4 = true;
            close(fd);
        }
    }
    if(true){
        int fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if(fd != -1){
            ipv6  = true;
            close(fd);
        }
    }

    if(ipv6 && !ipv4){
        return CONSTS_LOOPBACK6;
    }

    return CONSTS_LOOPBACK;
}

std::string DnsResolve(std::string host, int &family)
{
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;

    addrinfo* r = nullptr;
    AutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(host.c_str(), NULL, &hints, &r)) {
        return "";
    }

    char shost[64];
    memset(shost, 0, sizeof(shost));
    if (getnameinfo(r->ai_addr, r->ai_addrlen, shost, sizeof(shost), NULL, 0, NI_NUMERICHOST)) {
        return "";
    }

   family = r->ai_family;
   return std::string(shost);
}

void ParseHostport(std::string hostport, std::string &host, int &port)
{
    // No host or port.
    if (hostport.empty()) {
        return;
    }

    size_t pos = std::string::npos;

    // Host only for ipv4.
    if ((pos = hostport.rfind(":")) == std::string::npos) {
        host = hostport;
        return;
    }

    // For ipv4(only one colon), host:port.
    if (hostport.find(":") == pos) {
        host = hostport.substr(0, pos);
        std::string p = hostport.substr(pos + 1);
        if (!p.empty() && p != "0") {
            port = ::atoi(p.c_str());
        }
        return;
    }

    // Host only for ipv6.
    if (hostport.at(0) != '[' || (pos = hostport.rfind("]:")) == std::string::npos) {
        host = hostport;
        return;
    }

    // For ipv6, [host]:port.
    host = hostport.substr(1, pos - 1);
    std::string p = hostport.substr(pos + 2);
    if (!p.empty() && p != "0") {
        port = ::atoi(p.c_str());
    }
}

void ParseEndpoint(std::string hostport, std::string &ip, int &port)
{
    const size_t pos = hostport.rfind(":");   // Look for ":" from the end, to work with IPv6.
    if (pos != std::string::npos) {
        if ((pos >= 1) && (hostport[0] == '[') && (hostport[pos - 1] == ']')) {
            // Handle IPv6 in RFC 2732 format, e.g. [3ffe:dead:beef::1]:1935
            ip = hostport.substr(1, pos - 2);
        } else {
            // Handle IP address
            ip = hostport.substr(0, pos);
        }

        const std::string sport = hostport.substr(pos + 1);
        port = ::atoi(sport.c_str());
    } else {
        ip = AnyAddressForListener();
        port = ::atoi(hostport.c_str());
    }
}

bool CheckIpAddrValid(std::string ip)
{
    unsigned char buf[sizeof(struct in6_addr)];

    // check ipv4
    int ret = inet_pton(AF_INET, ip.data(), buf);
    if (ret > 0) {
        return true;
    }

    ret = inet_pton(AF_INET6, ip.data(), buf);
    if (ret > 0) {
        return true;
    }

    return false;
}

std::string Int2Str(int64_t value)
{
    return Fmt("%" PRId64, value);
}

std::string Float2Str(double value)
{
    // len(max int64_t) is 20, plus one "+-."
    char tmp[21 + 1];
    snprintf(tmp, sizeof(tmp), "%.2f", value);
    return tmp;
}

std::string Bool2Switch(bool v)
{
    return v? "on" : "off";
}

bool IsLittleEndian()
{
    // convert to network(big-endian) order, if not equals,
    // the system is little-endian, so need to convert the int64
    static int little_endian_check = -1;

    if(little_endian_check == -1) {
        union {
            int32_t i;
            int8_t c;
        } little_check_union;

        little_check_union.i = 0x01;
        little_endian_check = little_check_union.c;
    }

    return (little_endian_check == 1);
}

std::string StringReplace(std::string str, std::string old_str, std::string new_str)
{
    std::string ret = str;

    if(old_str == new_str){
        return ret;
    }

    size_t pos = 0;
    while((pos = ret.find(old_str, pos)) != std::string::npos){
        ret = ret.replace(pos, old_str.length(), new_str);
        pos += new_str.length();
    }

    return ret;
}

std::string StringTrimEnd(std::string str, std::string trim_chars)
{
    std::string ret = str;

    for (int i = 0; i < (int)trim_chars.length(); i++) {
        char ch = trim_chars.at(i);

        while (!ret.empty() && ret.at(ret.length() - 1) == ch) {
            ret.erase(ret.end() - 1);

            // ok, matched, should reset the search
            i = -1;
        }
    }

    return ret;
}

std::string StringTrimStart(std::string str, std::string trim_chars)
{
    std::string ret = str;

    for (int i = 0; i < (int)trim_chars.length(); i++) {
        char ch = trim_chars.at(i);

        while (!ret.empty() && ret.at(0) == ch) {
            ret.erase(ret.begin());

            // ok, matched, should reset the search
            i = -1;
        }
    }

    return ret;
}

std::string StringRemove(std::string str, std::string remove_chars)
{
    std::string ret = str;

    for (int i = 0; i < (int)remove_chars.length(); i++) {
        char ch = remove_chars.at(i);

        for (std::string::iterator it = ret.begin(); it != ret.end();) {
            if (ch == *it) {
                it = ret.erase(it);

                // ok, matched, should reset the search
                i = -1;
            } else {
                ++it;
            }
        }
    }

    return ret;
}

std::string EraseFirstSubstr(std::string str, std::string erase_string)
{
    std::string ret = str;

    size_t pos = ret.find(erase_string);

    if (pos != std::string::npos) {
        ret.erase(pos, erase_string.length());
    }

    return ret;
}

std::string EraseLastSubstr(std::string str, std::string erase_string)
{
    std::string ret = str;

    size_t pos = ret.rfind(erase_string);

    if (pos != std::string::npos) {
        ret.erase(pos, erase_string.length());
    }

    return ret;
}

bool StringEndsWith(std::string str, std::string flag)
{
    const size_t pos = str.rfind(flag);
    return (pos != std::string::npos) && (pos == str.length() - flag.length());
}

bool StringEndsWith(std::string str, std::string flag0, std::string flag1)
{
    return StringEndsWith(str, flag0) || StringEndsWith(str, flag1);
}

bool StringEndsWith(std::string str, std::string flag0, std::string flag1, std::string flag2)
{
    return StringEndsWith(str, flag0) || StringEndsWith(str, flag1) || StringEndsWith(str, flag2);
}

bool StringEndsWith(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3)
{
    return StringEndsWith(str, flag0) || StringEndsWith(str, flag1) || StringEndsWith(str, flag2) || StringEndsWith(str, flag3);
}

bool StringStartsWith(std::string str, std::string flag)
{
    return str.find(flag) == 0;
}

bool StringStartsWith(std::string str, std::string flag0, std::string flag1)
{
    return StringStartsWith(str, flag0) || StringStartsWith(str, flag1);
}

bool StringStartsWith(std::string str, std::string flag0, std::string flag1, std::string flag2)
{
    return StringStartsWith(str, flag0, flag1) || StringStartsWith(str, flag2);
}

bool StringStartsWith(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3)
{
    return StringStartsWith(str, flag0, flag1, flag2) || StringStartsWith(str, flag3);
}

bool StringContains(std::string str, std::string flag)
{
    return str.find(flag) != std::string::npos;
}

bool StringContains(std::string str, std::string flag0, std::string flag1)
{
    return str.find(flag0) != std::string::npos || str.find(flag1) != std::string::npos;
}

bool StringContains(std::string str, std::string flag0, std::string flag1, std::string flag2)
{
    return str.find(flag0) != std::string::npos || str.find(flag1) != std::string::npos || str.find(flag2) != std::string::npos;
}

int StringCount(std::string str, std::string flag)
{
    int nn = 0;
    for (int i = 0; i < (int)flag.length(); i++) {
        char ch = flag.at(i);
        nn += std::count(str.begin(), str.end(), ch);
    }
    return nn;
}

std::string StringMinMatch(std::string str, std::vector<std::string> seperators)
{
    std::string match;

    if (seperators.empty()) {
        return str;
    }

    size_t min_pos = std::string::npos;
    for (std::vector<std::string>::iterator it = seperators.begin(); it != seperators.end(); ++it) {
        std::string seperator = *it;

        size_t pos = str.find(seperator);
        if (pos == std::string::npos) {
            continue;
        }

        if (min_pos == std::string::npos || pos < min_pos) {
            min_pos = pos;
            match = seperator;
        }
    }

    return match;
}

std::vector<std::string> StringSplit(std::string s, std::string seperator)
{
    std::vector<std::string> result;
    if(seperator.empty()){
        result.push_back(s);
        return result;
    }

    size_t posBegin = 0;
    size_t posSeperator = s.find(seperator);
    while (posSeperator != std::string::npos) {
        result.push_back(s.substr(posBegin, posSeperator - posBegin));
        posBegin = posSeperator + seperator.length(); // next byte of seperator
        posSeperator = s.find(seperator, posBegin);
    }
    // push the last element
    result.push_back(s.substr(posBegin));
    return result;
}

std::vector<std::string> StringSplit(std::string str, std::vector<std::string> seperators)
{
    std::vector<std::string> arr;

    size_t pos = std::string::npos;
    std::string s = str;

    while (true) {
        std::string seperator = StringMinMatch(s, seperators);
        if (seperator.empty()) {
            break;
        }

        if ((pos = s.find(seperator)) == std::string::npos) {
            break;
        }

        arr.push_back(s.substr(0, pos));
        s = s.substr(pos + seperator.length());
    }

    if (!s.empty()) {
        arr.push_back(s);
    }

    return arr;
}

std::string Fmt(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    static char buf[8192];
    int r0 = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::string v;
    if (r0 > 0 && r0 < (int)sizeof(buf)) {
        v.append(buf, r0);
    }

    return v;
}

int DoCreateDirRecursively(std::string dir)
{
    int ret = ERROR_SUCCESS;

    // stat current dir, if exists, return error.
    if (PathExists(dir)) {
        return ERROR_SYSTEM_DIR_EXISTS;
    }

    // create parent first.
    size_t pos;
    if ((pos = dir.rfind("/")) != std::string::npos) {
        std::string parent = dir.substr(0, pos);
        ret = DoCreateDirRecursively(parent);
        // return for error.
        if (ret != ERROR_SUCCESS && ret != ERROR_SYSTEM_DIR_EXISTS) {
            return ret;
        }
        // parent exists, set to ok.
        ret = ERROR_SUCCESS;
    }

    // create curren dir.
#ifdef _WIN32
    if (::_mkdir(dir.c_str()) < 0) {
#else
    mode_t mode = S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH;
    if (::mkdir(dir.c_str(), mode) < 0) {
#endif
        if (errno == EEXIST) {
            return ERROR_SYSTEM_DIR_EXISTS;
        }

        ret = ERROR_SYSTEM_CREATE_DIR;
        ERROR("create dir %s failed. ret=%d", dir.c_str(), ret);
        return ret;
    }

    info("create dir %s success.", dir.c_str());

    return ret;
}

bool BytesEquals(void *pa, void *pb, int size)
{
    uint8_t* a = (uint8_t*)pa;
    uint8_t* b = (uint8_t*)pb;

    if (!a && !b) {
        return true;
    }

    if (!a || !b) {
        return false;
    }

    for(int i = 0; i < size; i++){
        if(a[i] != b[i]){
            return false;
        }
    }

    return true;
}

error CreateDirRecursivel(std::string dir)
{
    int ret = DoCreateDirRecursively(dir);

    if (ret == ERROR_SYSTEM_DIR_EXISTS || ret == ERROR_SUCCESS) {
        return SUCCESS;
    }

    return ERRORNEW(ret, "create dir %s", dir.c_str());
}

bool PathExists(std::string path)
{
    struct stat st;

    // stat current dir, if exists, return error.
    if (stat(path.c_str(), &st) == 0) {
        return true;
    }

    return false;
}

std::string PathDirname(std::string path)
{
    std::string dirname = path;

    // No slash, it must be current dir.
    size_t pos = std::string::npos;
    if ((pos = dirname.rfind("/")) == std::string::npos) {
        return "./";
    }

    // Path under root.
    if (pos == 0) {
        return "/";
    }

    // Fetch the directory.
    dirname = dirname.substr(0, pos);
    return dirname;
}

std::string PathBasename(std::string path)
{
    std::string dirname = path;
    size_t pos = std::string::npos;

    if ((pos = dirname.rfind("/")) != std::string::npos) {
        // the basename("/") is "/"
        if (dirname.length() == 1) {
            return dirname;
        }
        dirname = dirname.substr(pos + 1);
    }

    return dirname;
}

std::string PathFilename(std::string path)
{
    std::string filename = path;
    size_t pos = std::string::npos;

    if ((pos = filename.rfind(".")) != std::string::npos) {
        return filename.substr(0, pos);
    }

    return filename;
}

std::string PathFilext(std::string path)
{
    size_t pos = std::string::npos;

    if ((pos = path.rfind(".")) != std::string::npos) {
        return path.substr(pos);
    }

    return "";
}

bool AvcStartswithAnnexb(Buffer *stream, int *pnb_start_code)
{
    if (!stream) {
        return false;
    }

    char* bytes = stream->Data() + stream->Pos();
    char* p = bytes;

    for (;;) {
        if (!stream->Require((int)(p - bytes + 3))) {
            return false;
        }

        // not match
        if (p[0] != (char)0x00 || p[1] != (char)0x00) {
            return false;
        }

        // match N[00] 00 00 01, where N>=0
        if (p[2] == (char)0x01) {
            if (pnb_start_code) {
                *pnb_start_code = (int)(p - bytes) + 3;
            }
            return true;
        }

        p++;
    }

    return false;
}

bool AacStartswithAdts(Buffer *stream)
{
    if (!stream) {
        return false;
    }

    char* bytes = stream->Data() + stream->Pos();
    char* p = bytes;

    if (!stream->Require((int)(p - bytes) + 2)) {
        return false;
    }

    // matched 12bits 0xFFF,
    // @remark, we must cast the 0xff to char to compare.
    if (p[0] != (char)0xff || (char)(p[1] & 0xf0) != (char)0xf0) {
        return false;
    }

    return true;
}

// @see pycrc reflect at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L107
uint64_t Crc32Reflect(uint64_t data, int width)
{
    uint64_t res = data & 0x01;

    for (int i = 0; i < (int)width - 1; i++) {
        data >>= 1;
        res = (res << 1) | (data & 0x01);
    }

    return res;
}

// @see pycrc gen_table at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L178
void Crc32MakeTable(uint32_t t[256], uint32_t poly, bool reflect_in)
{
    int width = 32; // 32bits checksum.
    uint64_t msb_mask = (uint32_t)(0x01 << (width - 1));
    uint64_t mask = (uint32_t)(((msb_mask - 1) << 1) | 1);

    int tbl_idx_width = 8; // table index size.
    int tbl_width = 0x01 << tbl_idx_width; // table size: 256

    for (int i = 0; i < (int)tbl_width; i++) {
        uint64_t reg = uint64_t(i);

        if (reflect_in) {
            reg = Crc32Reflect(reg, tbl_idx_width);
        }

        reg = reg << (width - tbl_idx_width);
        for (int j = 0; j < tbl_idx_width; j++) {
            if ((reg&msb_mask) != 0) {
                reg = (reg << 1) ^ poly;
            } else {
                reg = reg << 1;
            }
        }

        if (reflect_in) {
            reg = Crc32Reflect(reg, width);
        }

        t[i] = (uint32_t)(reg & mask);
    }
}

// @see pycrc table_driven at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L207
uint32_t Crc32TableDriven(uint32_t* t, const void* buf, int size, uint32_t previous, bool reflect_in, uint32_t xor_in, bool reflect_out, uint32_t xor_out)
{
    int width = 32; // 32bits checksum.
    uint64_t msb_mask = (uint32_t)(0x01 << (width - 1));
    uint64_t mask = (uint32_t)(((msb_mask - 1) << 1) | 1);

    int tbl_idx_width = 8; // table index size.

    uint8_t* p = (uint8_t*)buf;
    uint64_t reg = 0;

    if (!reflect_in) {
        reg = xor_in;

        for (int i = 0; i < (int)size; i++) {
            uint8_t tblidx = (uint8_t)((reg >> (width - tbl_idx_width)) ^ p[i]);
            reg = t[tblidx] ^ (reg << tbl_idx_width);
        }
    } else {
        reg = previous ^ Crc32Reflect(xor_in, width);

        for (int i = 0; i < (int)size; i++) {
            uint8_t tblidx = (uint8_t)(reg ^ p[i]);
            reg = t[tblidx] ^ (reg >> tbl_idx_width);
        }

        reg = Crc32Reflect(reg, width);
    }

    if (reflect_out) {
        reg = Crc32Reflect(reg, width);
    }

    reg ^= xor_out;
    return (uint32_t)(reg & mask);
}

// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L207
// IEEETable is the table for the IEEE polynomial.
static uint32_t __crc32_IEEE_table[256];
static bool __crc32_IEEE_table_initialized = false;


// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L238
// IEEETable is the table for the MPEG polynomial.
static uint32_t __crc32_MPEG_table[256];
static bool __crc32_MPEG_table_initialized = false;

// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/models.py#L238
//      crc32('123456789') = 0x0376e6e7
// where it's defined as model:
//      'name':         'crc-32',
//      'width':         32,
//      'poly':          0x4c11db7,
//      'reflect_in':    False,
//      'xor_in':        0xffffffff,
//      'reflect_out':   False,
//      'xor_out':       0x0,
//      'check':         0x0376e6e7,
uint32_t Crc32Mpegts(const void *buf, int size)
{
    // @see golang IEEE of hash/crc32/crc32.go
    // IEEE is by far and away the most common CRC-32 polynomial.
    // Used by ethernet (IEEE 802.3), v.42, fddi, gzip, zip, png, ...
    // @remark The poly of CRC32 IEEE is 0x04C11DB7, its reverse is 0xEDB88320,
    //      please read https://en.wikipedia.org/wiki/Cyclic_redundancy_check
    uint32_t poly = 0x04C11DB7;

    bool reflect_in = false;
    uint32_t xor_in = 0xffffffff;
    bool reflect_out = false;
    uint32_t xor_out = 0x0;

    if (!__crc32_MPEG_table_initialized) {
        Crc32MakeTable(__crc32_MPEG_table, poly, reflect_in);
        __crc32_MPEG_table_initialized = true;
    }

    return Crc32TableDriven(__crc32_MPEG_table, buf, size, 0x00, reflect_in, xor_in, reflect_out, xor_out);
}

// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/models.py#L220
//      crc32('123456789') = 0xcbf43926
// where it's defined as model:
//      'name':         'crc-32',
//      'width':         32,
//      'poly':          0x4c11db7,
//      'reflect_in':    True,
//      'xor_in':        0xffffffff,
//      'reflect_out':   True,
//      'xor_out':       0xffffffff,
//      'check':         0xcbf43926,

uint32_t Crc32Ieee(const void *buf, int size, uint32_t previous)
{
    // @see golang IEEE of hash/crc32/crc32.go
    // IEEE is by far and away the most common CRC-32 polynomial.
    // Used by ethernet (IEEE 802.3), v.42, fddi, gzip, zip, png, ...
    // @remark The poly of CRC32 IEEE is 0x04C11DB7, its reverse is 0xEDB88320,
    //      please read https://en.wikipedia.org/wiki/Cyclic_redundancy_check
    uint32_t poly = 0x04C11DB7;

    bool reflect_in = true;
    uint32_t xor_in = 0xffffffff;
    bool reflect_out = true;
    uint32_t xor_out = 0xffffffff;

    if (!__crc32_IEEE_table_initialized) {
        Crc32MakeTable(__crc32_IEEE_table, poly, reflect_in);
        __crc32_IEEE_table_initialized = true;
    }

    return Crc32TableDriven(__crc32_IEEE_table, buf, size, previous, reflect_in, xor_in, reflect_out, xor_out);
}

// We use the standard encoding:
//      var StdEncoding = NewEncoding(encodeStd)
// StdEncoding is the standard base64 encoding, as defined in RFC 4648.
namespace {
    char padding = '=';
    std::string encoder = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}
// @see golang encoding/base64/base64.go

error AvBase64Decode(std::string cipher, std::string &plaintext)
{
    error err = SUCCESS;

    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));

    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }

    // decode is like Decode but returns an additional 'end' value, which
    // indicates if end-of-message padding or a partial quantum was encountered
    // and thus any additional data is an error.
    int si = 0;

    // skip over newlines
    for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
    }

    for (bool end = false; si < (int)cipher.length() && !end;) {
        // Decode quantum using the base64 alphabet
        uint8_t dbuf[4];
        memset(dbuf, 0x00, sizeof(dbuf));

        int dinc = 3;
        int dlen = 4;
        Assert(dinc > 0);

        for (int j = 0; j < (int)sizeof(dbuf); j++) {
            if (si == (int)cipher.length()) {
                if (padding != -1 || j < 2) {
                    return ERRORNEW(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }

                dinc = j - 1;
                dlen = j;
                end = true;
                break;
            }

            char in = cipher.at(si);

            si++;
            // skip over newlines
            for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
            }

            if (in == padding) {
                // We've reached the end and there's padding
                switch (j) {
                    case 0:
                    case 1:
                        // incorrect padding
                        return ERRORNEW(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                    case 2:
                        // "==" is expected, the first "=" is already consumed.
                        if (si == (int)cipher.length()) {
                            return ERRORNEW(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                        }
                        if (cipher.at(si) != padding) {
                            // incorrect padding
                            return ERRORNEW(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                        }

                        si++;
                        // skip over newlines
                        for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
                        }
                }

                if (si < (int)cipher.length()) {
                    // trailing garbage
                    err = ERRORNEW(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }
                dinc = 3;
                dlen = j;
                end = true;
                break;
            }

            dbuf[j] = decodeMap[(uint8_t)in];
            if (dbuf[j] == 0xff) {
                return ERRORNEW(ERROR_BASE64_DECODE, "corrupt input at %d", si);
            }
        }

        // Convert 4x 6bit source bytes into 3 bytes
        uint32_t val = uint32_t(dbuf[0])<<18 | uint32_t(dbuf[1])<<12 | uint32_t(dbuf[2])<<6 | uint32_t(dbuf[3]);
        if (dlen >= 2) {
            plaintext.append(1, char(val >> 16));
        }
        if (dlen >= 3) {
            plaintext.append(1, char(val >> 8));
        }
        if (dlen >= 4) {
            plaintext.append(1, char(val));
        }
    }

    return err;
}

error AvBase64Encode(std::string plaintext, std::string &cipher)
{
    error err = SUCCESS;
    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));

    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }
    cipher.clear();

    uint32_t val = 0;
    int si = 0;
    int n = (plaintext.length() / 3) * 3;
    uint8_t* p =  (uint8_t*)plaintext.c_str();
    while(si < n) {
        // Convert 3x 8bit source bytes into 4 bytes
        val = (uint32_t(p[si + 0]) << 16) | (uint32_t(p[si + 1])<< 8) | uint32_t(p[si + 2]);

        cipher += encoder[val>>18&0x3f];
        cipher += encoder[val>>12&0x3f];
        cipher += encoder[val>>6&0x3f];
        cipher += encoder[val&0x3f];

        si += 3;
    }

    int remain = plaintext.length() - si;
    if(0 == remain) {
        return err;
    }

    val = uint32_t(p[si + 0]) << 16;
    if( 2 == remain) {
        val |= uint32_t(p[si + 1]) << 8;
    }

    cipher += encoder[val>>18&0x3f];
    cipher += encoder[val>>12&0x3f];

    switch (remain) {
    case 2:
        cipher += encoder[val>>6&0x3f];
        cipher += padding;
        break;
    case 1:
        cipher += padding;
        cipher += padding;
        break;
    }


    return err;
}

#define SPACE_CHARS " \t\r\n"

int AvToupper(int c)
{
    if (c >= 'a' && c <= 'z') {
        c ^= 0x20;
    }
    return c;
}

// fromHexChar converts a hex character into its value and a success flag.
uint8_t FromHexChar(uint8_t c)
{
    if ('0' <= c && c <= '9') {
        return c - '0';
    }
    if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    }
    if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

char* DataToHex(char* des, const u_int8_t* src, int len)
{
    if(src == NULL || len == 0 || des == NULL){
        return NULL;
    }

    const char *hex_table = "0123456789ABCDEF";

    for (int i=0; i<len; i++) {
        des[i * 2]     = hex_table[src[i] >> 4];
        des[i * 2 + 1] = hex_table[src[i] & 0x0F];
    }

    return des;
}

char* DataToHexLowercase(char* des, const u_int8_t* src, int len)
{
    if(src == NULL || len == 0 || des == NULL){
        return NULL;
    }

    const char *hex_table = "0123456789abcdef";

    for (int i=0; i<len; i++) {
        des[i * 2]     = hex_table[src[i] >> 4];
        des[i * 2 + 1] = hex_table[src[i] & 0x0F];
    }

    return des;
}

int HexToData(uint8_t *data, const char *p, int size)
{
    if (size <= 0 || (size%2) == 1) {
        return -1;
    }

    for (int i = 0; i < (int)size / 2; i++) {
        uint8_t a = FromHexChar(p[i*2]);
        if (a == (uint8_t)-1) {
            return -1;
        }

        uint8_t b = FromHexChar(p[i*2 + 1]);
        if (b == (uint8_t)-1) {
            return -1;
        }

        data[i] = (a << 4) | b;
    }

    return size / 2;
}


int ChunkHeaderC0(int perfer_cid, uint32_t timestamp, int32_t payload_length, int8_t message_type, int32_t stream_id, char *cache, int nb_cache)
{
    // to directly set the field.
    char* pp = NULL;

    // generate the header.
    char* p = cache;

    // no header.
    if (nb_cache < CONSTS_RTMP_MAX_FMT0_HEADER_SIZE) {
        return 0;
    }

    // write new chunk stream header, fmt is 0
    *p++ = 0x00 | (perfer_cid & 0x3F);

    // chunk message header, 11 bytes
    // timestamp, 3bytes, big-endian
    if (timestamp < RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    } else {
        *p++ = (char)0xFF;
        *p++ = (char)0xFF;
        *p++ = (char)0xFF;
    }

    // message_length, 3bytes, big-endian
    pp = (char*)&payload_length;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];

    // message_type, 1bytes
    *p++ = message_type;

    // stream_id, 4bytes, little-endian
    pp = (char*)&stream_id;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];

    // for c0
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    //
    // for c3:
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 6.1.3. Extended Timestamp
    // This field is transmitted only when the normal time stamp in the
    // chunk message header is set to 0x00ffffff. If normal time stamp is
    // set to any value less than 0x00ffffff, this field MUST NOT be
    // present. This field MUST NOT be present if the timestamp field is not
    // present. Type 3 chunks MUST NOT have this field.
    // adobe changed for Type3 chunk:
    //        FMLE always sendout the extended-timestamp,
    //        must send the extended-timestamp to FMS,
    //        must send the extended-timestamp to flash-player.
    // @see: ngx_rtmp_prepare_message
    // @see: http://blog.csdn.net/win_lin/article/details/13363699
    // TODO: FIXME: extract to outer.
    if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }

    // always has header
    return (int)(p - cache);
}

int ChunkHeaderC3(int perfer_cid, uint32_t timestamp, char *cache, int nb_cache)
{
    // to directly set the field.
    char* pp = NULL;

    // generate the header.
    char* p = cache;

    // no header.
    if (nb_cache < CONSTS_RTMP_MAX_FMT3_HEADER_SIZE) {
        return 0;
    }

    // write no message header chunk stream, fmt is 3
    // @remark, if perfer_cid > 0x3F, that is, use 2B/3B chunk header,
    // SRS will rollback to 1B chunk header.
    *p++ = 0xC0 | (perfer_cid & 0x3F);

    // for c0
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    //
    // for c3:
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 6.1.3. Extended Timestamp
    // This field is transmitted only when the normal time stamp in the
    // chunk message header is set to 0x00ffffff. If normal time stamp is
    // set to any value less than 0x00ffffff, this field MUST NOT be
    // present. This field MUST NOT be present if the timestamp field is not
    // present. Type 3 chunks MUST NOT have this field.
    // adobe changed for Type3 chunk:
    //        FMLE always sendout the extended-timestamp,
    //        must send the extended-timestamp to FMS,
    //        must send the extended-timestamp to flash-player.
    // @see: ngx_rtmp_prepare_message
    // @see: http://blog.csdn.net/win_lin/article/details/13363699
    // TODO: FIXME: extract to outer.
    if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }

    // always has header
    return (int)(p - cache);
}

#ifndef UTILITY_H
#define UTILITY_H

#include "core.h"
#include "core_time.h"
#include <vector>

class Buffer;
class BitBuffer;

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))

//to read h.264 NALU uev
extern error AvcNaluReadUev(BitBuffer* stream, int32_t& v);
extern error AvcNaluReadBit(BitBuffer* stream, int8_t& v);

//get current system time in utime_t, use cache to avoid performance problem
extern utime_t GetSystemTime();
extern utime_t GetSystemStartupTime();
//a daemon st-thread updates it
extern utime_t UpdateSystemTime();

//the "ANY" address to listen, it's "0.0.0.0" for ipv4, and "::" for ipv6
extern std::string AnyAddressForListener();

//the dns resolve utility, return the resolved ip address
extern std::string DnsResolve(std::string host, int& family);

//split the host:port to host and port
extern void ParseHostport(std::string hostport, std::string& host, int& port);

//parse the endpoint to ip and port
extern void ParseEndpoint(std::string hostport, std::string& ip, int& port);

//check whether the ip is valid
extern bool CheckIpAddrValid(std::string ip);

//parse the int64 value to string
extern std::string Int2Str(int64_t value);
//parse the float value to string, precise is 2.
extern std::string Float2Str(double value);
//convert bool to switch value, true to "on", false to "off"
extern std::string Bool2Switch(bool v);

//whether system is little endian
extern bool IsLittleEndian();

//replace old_str to new_str of str
extern std::string StringReplace(std::string str, std::string old_str, std::string new_str);
//trim char in trim_chars of str
extern std::string StringTrimEnd(std::string str, std::string trim_chars);
//trim char in trim_chars of str
extern std::string StringTrimStart(std::string str, std::string trim_chars);
//remove char in remove_chars of str
extern std::string StringRemove(std::string str, std::string remove_chars);
//remove first substring from str
extern std::string EraseFirstSubstr(std::string str, std::string erase_string);
//remove last substring from str
extern std::string EraseLastSubstr(std::string str, std::string erase_string);
//whether string end with
extern bool StringEndsWith(std::string str, std::string flag);
extern bool StringEndsWith(std::string str, std::string flag0, std::string flag1);
extern bool StringEndsWith(std::string str, std::string flag0, std::string flag1, std::string flag2);
extern bool StringEndsWith(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3);
//whether string starts with
extern bool StringStartsWith(std::string str, std::string flag);
extern bool StringStartsWith(std::string str, std::string flag0, std::string flag1);
extern bool StringStartsWith(std::string str, std::string flag0, std::string flag1, std::string flag2);
extern bool StringStartsWith(std::string str, std::string flag0, std::string flag1, std::string flag2, std::string flag3);
//whether string contains with
extern bool StringContains(std::string str, std::string flag);
extern bool StringContains(std::string str, std::string flag0, std::string flag1);
extern bool StringContains(std::string str, std::string flag0, std::string flag1, std::string flag2);
//Count each char of flag in string
extern int StringCount(std::string str, std::string flag);
//find the min match in str for flags
extern std::string StringMinMatch(std::string str, std::vector<std::string> flags);
//split the string by seperator to array
extern std::vector<std::string> StringSplit(std::string s, std::string seperator);
extern std::vector<std::string> StringSplit(std::string s, std::vector<std::string> seperators);
//format to a string
extern std::string Fmt(const char* fmt, ...);

//compare the memory in bytes
extern bool BytesEquals(void* pa, void* pb, int size);

//create dir recursively
extern error CreateDirRecursivel(std::string dir);

//whether path exists
extern bool PathExists(std::string path);
//get the dirname of path, for instance, dirname("/live/livestream")="/live"
extern std::string PathDirname(std::string path);
//get the basename of path, for instance, basename("/live/livestream")="livestream"
extern std::string PathBasename(std::string path);
//get the filename of path, for instance,filename("livestream.flv")="livestream"
extern std::string PathFilename(std::string path);
//get the file extension of path, for instance, filext("live.flv")=".flv"
extern std::string PathFilext(std::string path);

//whether stream starts with the avc NALU in "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
//the start code must be "N[00] 00 00 01" where N >= 0
extern bool AvcStartswithAnnexb(Buffer* stream, int* pnb_start_code = nullptr);

//whether stream starts with the aac ADTS from ISO_IEC_14496-3-AAC-2001.pdf, page 75, 1.A.2.2 ADTS
//the start code must be '1111 1111 1111'B, that is 0xFFF
extern bool AacStartswithAdts(Buffer* stream);

//cacl the crc32 of bytes in buf, for ffmpeg
extern uint32_t Crc32Mpegts(const void* buf, int size);

//cacl the crc32 of bytes in buf by IEEE, for zip
extern uint32_t Crc32Ieee(const void* buf, int size, uint32_t previous = 0);

//decode a base64-encoded string
extern error AvBase64Decode(std::string cipher, std::string& plaintext);
//encode a plaintext to base64-encoded string
extern error AvBase64Encode(std::string plaintext, std::string& cipher);

//calculate the output size needed to base64-encode x bytes to a null-terminated string
#define AV_BASE64_SIZE(x) (((x)+2) / 3 * 4 + 1)

//covert hex string to uint8 data, for example:
//  HexToData(data, string("139056E5A0"))
//  which outputs the data in hex {0x13, 0x90, 0x56, 0xe5, 0xa0}.
extern int HexToData(uint8_t* data, const char* p, int size);

//convert data string to hex, for example:
//  DataToHex(des, {0xf3, 0x3f}, 2)
//  which outputs the des is string("F33F").
extern char* DataToHex(char* des, const uint8_t* src, int len);
//output in lowercase, such as string("f33f")
extern char* DataToHexLowercase(char* des, const uint8_t* src, int len);

//generate the c0 chunk header for msg
extern int ChunkHeaderC0(int perfer_cid, uint32_t timestamp, int32_t payload_length, int8_t message_type, int32_t stream_id, char* cache, int nb_cache);

//generate the c3 chunk header for msg
extern int ChunkHeaderC3(int perfer_cid, uint32_t timestamp, char* cache, int nb_cache);

//for utest to mock it
#include <sys/time.h>
#ifdef OSX
    #define
#else
    typedef int (*gettimeofday_t)(struct timeval* tv, struct timezone* tz);
#endif

#endif // UTILITY_H

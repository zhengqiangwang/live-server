#ifndef BUFFER_H
#define BUFFER_H

#include "core.h"
#include <sys/types.h>
#include <string>

class Buffer;

//encoder
class IEncoder
{
public:
    IEncoder();
    virtual ~IEncoder();
public:
    //get the number of bytes to code to
    virtual uint64_t NbBytes() = 0;
    //encode object to bytes in Buffer
    virtual error Encode(Buffer* buf) = 0;
};

//the codec, to code and decode object with bytes:
//  code: to encode/serialize object to bytes in buffer
//we use Buffer as bytes helper utility.
//for example, to code:
//  ICodec* obj = ...
//  char* bytes = new char[obj->Size()];
//  Buffer* buf = new Buffer();
//  buf->Initialize(bytes, obj->Size())
//  obj->Encode(buf);
//to decode
//  int nb_bytes = ...
//  char* bytes = ...
//  Buffer* buf = new Buffer();
//  buf->Initialize(bytes, nb_bytes);
//  ICodec* obj = ...
//  obj->Decode(buf);

class ICodec : public IEncoder
{
public:
    ICodec();
    virtual ~ICodec();
public:
    //get the number of bytes to code to.
    virtual uint64_t NbBytes() = 0;
    //encode object to bytes in Buffer
    virtual error Encode(Buffer* buf) = 0;
public:
    //decode object from bytes in Buffer
    virtual error Decode(Buffer* buf) = 0;
};

//bytes utility, used to:
//convert basic types to bytes,
//build basic types from bytes

class Buffer
{
private:
    //current position
    char *m_p = nullptr;
    //the bytes data for buffer to read or write
    char *m_base;
    //the total number of bytes
    int m_bytes;
public:
    Buffer(char *base, int len);
    ~Buffer();

    //copy object, keep position
    Buffer *Copy();

    //return data init
    char *Data();
    //return current position pointer
    char *Head();
    //return buffer data length
    int Size();
    //set the buffer size
    void SetSize(int v);
    //get current position
    int Pos();
    //return the buffer remain data length
    int Remain();
    //whether buffer is empty
    bool Empty();
    //whether buffer is able to supply required size of bytes.
    bool Require(int size);
    //skip size
    void Skip(int size);
public:
    //read 1bytes char from buffer
    int8_t Read1Bytes();
    //read 2bytes int from buffer(big endian)
    int16_t Read2Bytes();
    //read 2bytes int from buffer(little endian)
    int16_t ReadLe2Bytes();
    //read 3bytes int from buffer(big endian)
    int32_t Read3Bytes();
    //read 3bytes int from buffer(little endian)
    int32_t ReadLe3Bytes();
    //read 4bytes int from buffer(big endian)
    int32_t Read4Bytes();
    //read 4bytes int from buffer(little endian)
    int32_t ReadLe4Bytes();
    //read 8bytes int from buffer(big endian)
    int64_t Read8Bytes();
    //read 8bytes int from buffer(little endian)
    int64_t ReadLe8Bytes();
    //read string from buffer, length specifies by param size
    std::string ReadString(int len);
    //read bytes from buffer, length specifies by param len
    void ReadBytes(char *data, int len);

public:
    //write 1bytes char to buffer
    void Write1Bytes(int8_t value);
    //write 2bytes int to buffer(big endian)
    void Write2Bytes(int16_t value);
    //write 2bytes int to buffer(little endian)
    void WriteLe2Bytes(int16_t value);
    //write 3bytes int to buffer(big endian)
    void Write3Bytes(int32_t value);
    //write 3bytes int to buffer(little endian)
    void WriteLe3Bytes(int32_t value);
    //write 4bytes int to buffer(big endian)
    void Write4Bytes(int32_t value);
    //write 4bytes int to buffer(little endian)
    void WriteLe4Bytes(int32_t value);
    //write 8bytes int to buffer(big endian)
    void Write8Bytes(int64_t value);
    //write 8bytes int to buffer(little endian)
    void WriteLe8Bytes(int64_t value);
    //write string to buffer
    void WriteString(std::string value);
    //write bytes to buffer
    void WriteBytes(char *data, int len);
};

//the bit buffer, base on Buffer,
//for example,the h.264 avc buffer is bit buffer
class BitBuffer
{
private:
    //store a int value from buffer read
    int8_t m_cb;
    //store read index
    uint8_t m_cbLeft;
    Buffer* m_stream;
public:
    BitBuffer(Buffer* b);
    ~BitBuffer();
public:
    bool Empty();
    //read a bit
    int8_t ReadBit();
};

#endif // BUFFER_H

#include "buffer.h"
#include "error.h"
#include <cstring>

IEncoder::IEncoder()
{

}

IEncoder::~IEncoder()
{

}

ICodec::ICodec()
{

}

ICodec::~ICodec()
{

}

Buffer::Buffer(char *base, int len): m_p{base}, m_base{base}, m_bytes{len}
{

}

Buffer::~Buffer()
{

}

Buffer *Buffer::Copy()
{
    Buffer *cp = new Buffer(m_base, m_bytes);
    cp->m_p = m_p;
    return cp;
}

char *Buffer::Data()
{
    return m_base;
}

char *Buffer::Head()
{
    return m_p;
}

int Buffer::Size()
{
    return m_bytes;
}

void Buffer::SetSize(int v)
{
    m_bytes = v;
}

int Buffer::Pos()
{
    return (int)(m_p - m_base);
}

int Buffer::Remain()
{
    return m_bytes - (int)(m_p - m_base);
}

bool Buffer::Empty()
{
    return !m_base || (m_p >= m_base + m_bytes);
}

bool Buffer::Require(int size)
{
    if(size < 0)
    {
        return false;
    }

    return size <= (m_bytes - (int)(m_p - m_base));
}

void Buffer::Skip(int size)
{
    Assert(m_p);
    Assert(m_p + size >= m_base);
    Assert(m_p + size <= m_base + m_bytes);
    m_p += size;
}

int8_t Buffer::Read1Bytes()
{
    Assert(Require(1));

    return (int8_t)*m_p++;
}

int16_t Buffer::Read2Bytes()
{
    Assert(Require(2));

    int16_t value;
    char *pp = (char *)&value;
    pp[1] = *m_p++;
    pp[0] = *m_p++;

    return value;
}

int16_t Buffer::ReadLe2Bytes()
{
    Assert(Require(2));

    int16_t value;
    char *pp = (char *)&value;
    pp[0] = *m_p++;
    pp[1] = *m_p++;

    return value;
}

int32_t Buffer::Read3Bytes()
{
    Assert(Require(3));

    int32_t value;
    char *pp = (char *)&value;
    pp[2] = *m_p++;
    pp[1] = *m_p++;
    pp[0] = *m_p++;

    return value;
}

int32_t Buffer::ReadLe3Bytes()
{
    Assert(Require(3));

    int32_t value;
    char *pp = (char *)&value;
    pp[0] = *m_p++;
    pp[1] = *m_p++;
    pp[2] = *m_p++;

    return value;
}

int32_t Buffer::Read4Bytes()
{
    Assert(Require(4));

    int32_t value;
    char *pp = (char *)&value;
    pp[3] = *m_p++;
    pp[2] = *m_p++;
    pp[1] = *m_p++;
    pp[0] = *m_p++;

    return value;
}

int32_t Buffer::ReadLe4Bytes()
{
    Assert(Require(4));

    int32_t value;
    char *pp = (char *)&value;
    pp[0] = *m_p++;
    pp[1] = *m_p++;
    pp[2] = *m_p++;
    pp[3] = *m_p++;

    return value;
}

int64_t Buffer::Read8Bytes()
{
    Assert(Require(8));

    int64_t value;
    char *pp = (char *)&value;
    pp[7] = *m_p++;
    pp[6] = *m_p++;
    pp[5] = *m_p++;
    pp[4] = *m_p++;
    pp[3] = *m_p++;
    pp[2] = *m_p++;
    pp[1] = *m_p++;
    pp[0] = *m_p++;

    return value;
}

int64_t Buffer::ReadLe8Bytes()
{
    Assert(Require(8));

    int64_t value;
    char *pp = (char *)&value;
    pp[0] = *m_p++;
    pp[1] = *m_p++;
    pp[2] = *m_p++;
    pp[3] = *m_p++;
    pp[4] = *m_p++;
    pp[5] = *m_p++;
    pp[6] = *m_p++;
    pp[7] = *m_p++;

    return value;
}

std::string Buffer::ReadString(int len)
{
    Assert(Require(len));

    std::string value;
    value.append(m_p,len);

    m_p += len;

    return value;
}

void Buffer::ReadBytes(char *data, int len)
{
    Assert(Require(len));

    memcpy(data, m_p, len);

    m_p += len;
}

void Buffer::Write1Bytes(int8_t value)
{
    Assert(Require(1));

    *m_p++ = value;
}

void Buffer::Write2Bytes(int16_t value)
{
    Assert(Require(2));

    char *pp = (char *)&value;
    *m_p++ = pp[1];
    *m_p++ = pp[0];
}

void Buffer::WriteLe2Bytes(int16_t value)
{
    Assert(Require(2));

    char *pp = (char *)&value;
    *m_p++ = pp[0];
    *m_p++ = pp[1];
}

void Buffer::Write3Bytes(int32_t value)
{
    Assert(Require(3));

    char *pp = (char *)&value;
    *m_p++ = pp[2];
    *m_p++ = pp[1];
    *m_p++ = pp[0];
}

void Buffer::WriteLe3Bytes(int32_t value)
{
    Assert(Require(3));

    char *pp = (char *)&value;
    *m_p++ = pp[0];
    *m_p++ = pp[1];
    *m_p++ = pp[2];
}

void Buffer::Write4Bytes(int32_t value)
{
    Assert(Require(4));

    char *pp = (char *)&value;
    *m_p++ = pp[3];
    *m_p++ = pp[2];
    *m_p++ = pp[1];
    *m_p++ = pp[0];
}

void Buffer::WriteLe4Bytes(int32_t value)
{
    Assert(Require(4));

    char *pp = (char *)&value;
    *m_p++ = pp[0];
    *m_p++ = pp[1];
    *m_p++ = pp[2];
    *m_p++ = pp[3];
}

void Buffer::Write8Bytes(int64_t value)
{
    Assert(Require(8));

    char *pp = (char *)&value;
    *m_p++ = pp[7];
    *m_p++ = pp[6];
    *m_p++ = pp[5];
    *m_p++ = pp[4];
    *m_p++ = pp[3];
    *m_p++ = pp[2];
    *m_p++ = pp[1];
    *m_p++ = pp[0];
}

void Buffer::WriteLe8Bytes(int64_t value)
{
    Assert(Require(8));

    char *pp = (char *)&value;
    *m_p++ = pp[0];
    *m_p++ = pp[1];
    *m_p++ = pp[2];
    *m_p++ = pp[3];
    *m_p++ = pp[4];
    *m_p++ = pp[5];
    *m_p++ = pp[6];
    *m_p++ = pp[7];
}

void Buffer::WriteString(std::string value)
{
    if(value.empty())
    {
        return;
    }

    Assert(Require(value.size()));

    memcpy(m_p, value.data(), value.size());

    m_p += value.size();
}

void Buffer::WriteBytes(char *data, int len)
{
    if(len < 0)
    {
        return;
    }

    Assert(Require(len));
    memcpy(m_p, data, len);
    m_p += len;
}



BitBuffer::BitBuffer(Buffer *b)
{
    m_cb = 0;
    m_cbLeft = 0;
    m_stream = b;
}

BitBuffer::~BitBuffer()
{

}

bool BitBuffer::Empty()
{
    if(m_cbLeft){
        return false;
    }
    return m_stream->Empty();
}

int8_t BitBuffer::ReadBit()
{
    if(!m_cbLeft){
        Assert(!m_stream->Empty());
        m_cb = m_stream->Read1Bytes();
        m_cbLeft = 8;
    }

    int8_t v = (m_cb >> (m_cbLeft - 1)) &0x01;
    m_cbLeft--;
    return v;
}

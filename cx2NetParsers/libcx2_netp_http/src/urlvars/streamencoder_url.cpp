#include "streamencoder_url.h"

#include <limits>


using namespace CX2::Memory::Streams;
using namespace CX2::Memory::Streams::Encoders;

URL::URL(Memory::Streams::Streamable * orig)
{
    this->orig = orig;
    finalBytesWritten =0;
}

bool URL::streamTo(Memory::Streams::Streamable *, Status & )
{
    return false;
}

Status URL::write(const void *buf, const size_t &count, Status &wrStat)
{
    Status cur;
    size_t pos=0;

    size_t maxStream=std::numeric_limits<size_t>::max();
    maxStream/=3;
    maxStream-=3;

    if (count>maxStream)
    {
        cur.succeed=wrStat.succeed=setFailedWriteState();
        return cur;
    }

    ///////////////////////
    while (pos<count)
    {
        size_t bytesToTransmitInPlain;
        if ((bytesToTransmitInPlain=getPlainBytesSize( ((const unsigned char *)buf)+pos,count-pos))>0)
        {
            if (!(cur+=orig->writeFullStream( ((const unsigned char *)buf)+pos ,bytesToTransmitInPlain,wrStat)).succeed)
            {
                finalBytesWritten+=cur.bytesWritten;
                return cur;
            }
            pos+=bytesToTransmitInPlain;
        }
        else
        {
            char encodedByte[8];
            snprintf(encodedByte,8, "%%%02X", *(((const unsigned char *)buf)+pos));
            if (!(cur+=orig->writeFullStream(encodedByte,3, wrStat)).succeed)
            {
                finalBytesWritten+=cur.bytesWritten;
                return cur;
            }
            pos++;
        }
    }
    finalBytesWritten+=cur.bytesWritten;
    return cur;
}

size_t URL::getPlainBytesSize(const unsigned char *buf, size_t count)
{
    for (size_t i=0;i<count;i++)
    {
        if (shouldEncodeThisByte(buf[i])) return i;
    }
    return count;
}

inline bool URL::shouldEncodeThisByte(const unsigned char &byte) const
{
    return !(
            (byte>='A' && byte<='Z') ||
            (byte>='a' && byte<='z') ||
            (byte>='0' && byte<='9')
    );
}

uint64_t URL::getFinalBytesWritten() const
{
    return finalBytesWritten;
}

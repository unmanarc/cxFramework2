#include "http_content.h"
#include "http_hlp_chunked_retriever.h"

#include <limits>

// TODO: CHECK THIS CLASS
using namespace CX2::Network::HTTP;
using namespace CX2;

HTTP_Content::HTTP_Content()
{
    transmitionMode = HTTP_CONTENT_TRANSMODE_CONNECTION_CLOSE;
    currentMode = HTTP_CONTENTDATA_CURRMODE_CONTENT_LENGTH;
    currentContentLengthSize = 0;
    securityMaxPostDataSize = 17*MB_MULT; // 17Mb intermediate buffer (suitable for 16mb max chunk...).
    securityMaxHttpChunkSize = 16*MB_MULT; // 16mb.
    containerType = HTTP_CONTAINERTYPE_BIN;
    outStream = &binDataContainer;
    deleteOutStream = false;
}

HTTP_Content::~HTTP_Content()
{
    if (deleteOutStream) delete outStream;
}

bool HTTP_Content::isDefaultStreamableOutput()
{
    return outStream==&binDataContainer || outStream==&urlVars || outStream==&multiPartVars;
}

void HTTP_Content::setSecurityMaxPostDataSize(const uint64_t &value)
{
    securityMaxPostDataSize = value;
}


Memory::Streams::Parsing::ParseStatus HTTP_Content::parse()
{
    switch (currentMode)
    {
    case HTTP_CONTENTDATA_CURRMODE_CHUNK_SIZE:
    {
        size_t targetChunkSize;
        if ((targetChunkSize=parseHttpChunkSize())!=std::numeric_limits<uint32_t>::max())
        {
            if (targetChunkSize>0)
            {
                setParseMode(Memory::Streams::Parsing::PARSE_MODE_SIZE);
                setParseDataTargetSize(targetChunkSize);
                currentMode = HTTP_CONTENTDATA_CURRMODE_CHUNK_DATA;
                return Memory::Streams::Parsing::PARSE_STAT_GET_MORE_DATA;
            }
            else
            {
                // Done... last chunk.
                // report that is last chunk.
                outStream->writeEOF(true);
                return Memory::Streams::Parsing::PARSE_STAT_GOTO_NEXT_SUBPARSER;
            }
        }
        return Memory::Streams::Parsing::PARSE_STAT_ERROR;
    }
    case HTTP_CONTENTDATA_CURRMODE_CHUNK_DATA:
    {
        // Ok, continue
        setParseMode(Memory::Streams::Parsing::PARSE_MODE_SIZE);
        setParseDataTargetSize(2); // for CRLF.
        currentMode = HTTP_CONTENTDATA_CURRMODE_CHUNK_CRLF;
        // Proccess chunk into mem...
        getParsedData()->appendTo(*outStream);
        return Memory::Streams::Parsing::PARSE_STAT_GET_MORE_DATA;
    }
    case HTTP_CONTENTDATA_CURRMODE_CHUNK_CRLF:
    {
        setParseMode(Memory::Streams::Parsing::PARSE_MODE_DELIMITER);
        setParseDelimiter("\r\n");
        setParseDataTargetSize(1*KB_MULT); // !1kb max (until fails).
        currentMode = HTTP_CONTENTDATA_CURRMODE_CHUNK_SIZE;
        return Memory::Streams::Parsing::PARSE_STAT_GET_MORE_DATA;
    }
    case HTTP_CONTENTDATA_CURRMODE_CONTENT_LENGTH:
    {
        getParsedData()->appendTo(*outStream);
        if (getLeftToparse()>0)
        {
            return Memory::Streams::Parsing::PARSE_STAT_GET_MORE_DATA;
        }
        else
        {
            // End of stream reached...
            outStream->writeEOF(true);
            return Memory::Streams::Parsing::PARSE_STAT_GOTO_NEXT_SUBPARSER;
        }
    }
    case HTTP_CONTENTDATA_CURRMODE_CONNECTION_CLOSE:
    {
        // Content satisfied...
        if (getParsedData()->size())
        {
            // Parsing data...
            getParsedData()->appendTo(*outStream);
            return Memory::Streams::Parsing::PARSE_STAT_GET_MORE_DATA;
        }
        else
        {
            // No more data to parse, ending and processing it...
            outStream->writeEOF(true);
            return Memory::Streams::Parsing::PARSE_STAT_GOTO_NEXT_SUBPARSER;
        }
    }
    }
    return Memory::Streams::Parsing::PARSE_STAT_ERROR;
}

uint32_t HTTP_Content::parseHttpChunkSize()
{
    uint32_t parsedSize = getParsedData()->toUInt32(16);
    if ( parsedSize == std::numeric_limits<uint32_t>::max() || parsedSize>securityMaxHttpChunkSize)
        return std::numeric_limits<uint32_t>::max();
    else
        return parsedSize;
}

eHTTP_Content_Transmition_Mode HTTP_Content::getTransmitionMode() const
{
    return transmitionMode;
}

HTTP_URLVars *HTTP_Content::getUrlVars()
{
    return &urlVars;
}


eHTTP_ContainerType HTTP_Content::getContainerType() const
{
    return containerType;
}

Memory::Abstract::Vars *HTTP_Content::postVars()
{
    switch (containerType)
    {
    case HTTP_CONTAINERTYPE_MIME:
        return &multiPartVars;
    case HTTP_CONTAINERTYPE_URL:
    case HTTP_CONTAINERTYPE_BIN:
        return &urlVars; // url vars should be empty here...
    }
    return &urlVars;
}

Network::MIME::MIME_Vars *HTTP_Content::getMultiPartVars()
{
    return &multiPartVars;
}

void HTTP_Content::setContainerType(const eHTTP_ContainerType &value)
{
    containerType = value;
    if (isDefaultStreamableOutput())
    {
        switch (containerType)
        {
        case HTTP_CONTAINERTYPE_MIME:
            outStream = &multiPartVars;
            break;
        case HTTP_CONTAINERTYPE_URL:
            outStream = &urlVars;
            break;
        case HTTP_CONTAINERTYPE_BIN:
            outStream = &binDataContainer;
            break;
        }
    }
}

void HTTP_Content::setSecurityMaxHttpChunkSize(const uint32_t &value)
{
    securityMaxHttpChunkSize = value;
}

bool HTTP_Content::stream(Memory::Streams::Status & wrStat)
{
    // Act as a client. Send data from here.
    switch (transmitionMode)
    {
    case HTTP_CONTENT_TRANSMODE_CHUNKS:
    {
        HTTP_HLP_Chunked_Retriever retr(upStream);
        return outStream->streamTo(&retr,wrStat) && upStream->getFailedWriteState()==0;
    }
    case HTTP_CONTENT_TRANSMODE_CONTENT_LENGTH:
    case HTTP_CONTENT_TRANSMODE_CONNECTION_CLOSE:
    {
        return outStream->streamTo(upStream, wrStat) && upStream->getFailedWriteState()==0;
    }
    }
    return true;
}

void HTTP_Content::setTransmitionMode(const eHTTP_Content_Transmition_Mode &value)
{
    transmitionMode = value;

    switch (transmitionMode)
    {
    case HTTP_CONTENT_TRANSMODE_CONNECTION_CLOSE:
    {
        // TODO: disable connection-close for client->server
        setParseMode(Memory::Streams::Parsing::PARSE_MODE_DIRECT);
        setParseDataTargetSize(securityMaxPostDataSize); // !1kb max (until fails).
        currentMode = HTTP_CONTENTDATA_CURRMODE_CONNECTION_CLOSE;
    }break;
    case HTTP_CONTENT_TRANSMODE_CHUNKS:
    {
        // TODO: disable chunk transmition for client->server
        setParseMode(Memory::Streams::Parsing::PARSE_MODE_DELIMITER);
        setParseDelimiter("\r\n");
        setParseDataTargetSize(64); // 64 bytes max (until fails).
        currentMode = HTTP_CONTENTDATA_CURRMODE_CHUNK_SIZE;
    }break;
    case HTTP_CONTENT_TRANSMODE_CONTENT_LENGTH:
    {
        setParseMode(Memory::Streams::Parsing::PARSE_MODE_DIRECT);
        currentMode = HTTP_CONTENTDATA_CURRMODE_CONTENT_LENGTH;
    }break;
    }
}

bool HTTP_Content::setContentLenSize(const uint64_t &contentLengthSize)
{
    if (contentLengthSize>securityMaxPostDataSize)
    {
        // The content length specified in the
        currentContentLengthSize = 0;
        setParseDataTargetSize(0);
        return false;
    }

    currentContentLengthSize=contentLengthSize;
    setParseDataTargetSize(contentLengthSize);

    return true;
}

void HTTP_Content::setMaxPostSizeInMemBeforeGoingToFS(const uint64_t &value)
{
    //getParsedData()->setMaxContainerSizeUntilGoingToFS(value);
    binDataContainer.setMaxContainerSizeUntilGoingToFS(value);
}

void HTTP_Content::useFilesystem(const std::string &fsFilePath)
{
    // TODO:
    //binDataContainer.setFsBaseFileName();
}

void HTTP_Content::preemptiveDestroyStreamableOuput()
{
    binDataContainer.clear();
    if (this->deleteOutStream)
    {
        this->deleteOutStream = false;
        delete outStream;
        outStream = &binDataContainer;
    }
}

Memory::Streams::Streamable *HTTP_Content::getStreamableOuput()
{
    return outStream;
}

void HTTP_Content::setStreamableOutput(Memory::Streams::Streamable *outDataContainer, bool deleteOutStream)
{
    // This stream has been setted up before...
    // Delete the previous stream/data before replacing...
    preemptiveDestroyStreamableOuput();

    this->deleteOutStream = deleteOutStream;
    this->outStream = outDataContainer;

    if (this->outStream == nullptr)
    {
        // Point to empty container...
        outStream = &binDataContainer;
    }
}

uint64_t HTTP_Content::getStreamSize()
{
    return outStream->size();
}


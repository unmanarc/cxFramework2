#include "socket.h"

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>

#include <stdexcept>

#ifdef _WIN32
#include <cx2_mem_vars/w32compat.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
/*
#include <sys/types.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>*/

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#endif

using namespace CX2::Network::Sockets;

#ifdef _WIN32
// Init winsock when the program begins...
bool Socket::winSockInitialized = Socket::win32Init();
#endif

bool Socket::socketInitialized = false;
bool Socket::badSocket = false;

void Socket::initVars()
{
    useIPv6 = false;

    listenMode = false;
    readTimeout = 0;
    writeTimeout = 0;
    recvBuffer = 0;
    useWrite = false;
    lastError = "";
    sockfd = -1;

    shutdown_rd = false;
    shutdown_wr = false;

    shutdown_proto_rd = false;
    shutdown_proto_wr = false;


    memset(remotePair, 0, sizeof(remotePair));
}

bool Socket::bindTo(const char *bindAddress, const uint16_t & port)
{
    if (bindAddress == nullptr)
        return true;

    if (!useIPv6)
    {
        struct sockaddr_in serveraddr;
        memset(&serveraddr, 0, sizeof(serveraddr));

        serveraddr.sin_family = AF_INET;
        serveraddr.sin_port   = htons(port);

        if (bindAddress[0] == '*' && bindAddress[1] == 0)
            inet_pton(AF_INET, "0.0.0.0", &serveraddr.sin_addr);
        else
            inet_pton(AF_INET, bindAddress, &serveraddr.sin_addr);

        if (bind(sockfd,(struct sockaddr *)&serveraddr,sizeof(serveraddr)) < 0)
        {
            lastError = "bind() failed";
            closeSocket();
            return false;
        }
    }
    else
    {
        struct sockaddr_in6 serveraddr;
        memset(&serveraddr, 0, sizeof(serveraddr));

        serveraddr.sin6_family = AF_INET6;
        serveraddr.sin6_port   = htons(port);

        if (bindAddress[0] == '*' && bindAddress[1] == 0)
            inet_pton(AF_INET6, "::", &serveraddr.sin6_addr);
        else
            inet_pton(AF_INET6, bindAddress, &serveraddr.sin6_addr);

        if (bind(sockfd,(struct sockaddr *)&serveraddr,sizeof(serveraddr)) < 0)
        {
            lastError = "bind() failed";
            closeSocket();
            return false;
        }
    }

    return true;
}

bool Socket::getAddrInfo(const char *remoteHost, const uint16_t &remotePort, int ai_socktype, void **res)
{
    addrinfo hints;
    int rc;

    memset(&hints, 0x00, sizeof(hints));

#ifdef _WIN32
    hints.ai_flags    = 0;
#else
    hints.ai_flags    = AI_NUMERICSERV;
#endif
    hints.ai_socktype = ai_socktype;
    hints.ai_family   = AF_UNSPEC;

    if (useIPv6)
    {
        struct in6_addr serveraddr;
        rc = inet_pton(AF_INET6, remoteHost, &serveraddr);
        if (rc == 1)
        {
            hints.ai_family = AF_INET6;
            hints.ai_flags |= AI_NUMERICHOST;
        }
    }
    else
    {
        struct in_addr serveraddr;
        rc = inet_pton(AF_INET, remoteHost, &serveraddr);
        if (rc == 1)
        {
            hints.ai_family = AF_INET;
            hints.ai_flags |= AI_NUMERICHOST;
        }
    }

    char serverPort[8];
    snprintf(serverPort,8,"%u",remotePort);

    rc = getaddrinfo(remoteHost, serverPort, &hints, (addrinfo **)res);

    switch (rc)
    {
    case 0:
        return true;
    case EAI_ADDRFAMILY:
        lastError = "The specified network host does not have any network addresses in the requested address family.";
        return false;
    case EAI_AGAIN:
        lastError = "The name server returned a temporary failure indication.  Try again later.";
        return false;
    case EAI_BADFLAGS:
        lastError = "hints.ai_flags contains invalid flags; or, hints.ai_flags included AI_CANONNAME and name was NULL.";
        return false;
    case EAI_FAIL:
        lastError = "The name server returned a permanent failure indication.";
        return false;
    case EAI_FAMILY:
        lastError = "The requested address family is not supported.";
        return false;
    case EAI_MEMORY:
        lastError = "Out of memory during name resolution.";
        return false;
    case EAI_NODATA:
        lastError = "The specified network host exists, but does not have any network addresses defined.";
        return false;
    case EAI_NONAME:
        lastError = "The node or service is not known; or both node and service are NULL; or AI_NUMERICSERV was specified in  hints.ai_flags and service was not a numeric port-number string.";
        return false;
    case EAI_SERVICE:
        lastError = "The requested service is not available for the requested socket type.";
        return false;
    case EAI_SOCKTYPE:
        lastError = "The requested socket type is not supported.";
        return false;
    case EAI_SYSTEM:
        lastError = "System Error duing name resolution.";
        return false;
    default:
        lastError = "Unknown name resolution error.";
        break;
    }

    return false;
}

bool Socket::getUseIPv6() const
{
    return useIPv6;
}

void Socket::setUseIPv6(bool value)
{
    useIPv6 = value;
}

Socket::Socket()
{
    initVars();
}

Socket::~Socket()
{
    closeSocket();
}

Socket::Socket(const Socket &)
{
    // If your program copy+construct the socket with other, it will crash for sanity here (preventing socket file descriptor duplication):
    throw std::runtime_error("Socket does not handle copy-constructor, please use shared_ptr");
}

Socket &Socket::operator=(Socket)
{
    // If your program copies the socket, it will crash for sanity here (preventing socket file descriptor duplication):
    throw std::runtime_error("Socket does not handle copies/assignment, please use shared_ptr");
}

#ifdef _WIN32
bool Socket::win32Init()
{
    socketSystemInitialization();
    return true;
}
#endif

void Socket::setUseWrite()
{
    // prevent the application from crash, ignore the sigpipes:
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    // use write/read functions instead send/recv
    useWrite = true;
}

void Socket::setRecvBuffer(int buffsize)
{
    recvBuffer = buffsize;

    if (!isActive()) return;
#ifdef _WIN32
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *) &buffsize, sizeof(buffsize));
#else
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buffsize, sizeof(buffsize));
#endif
}

bool Socket::isConnected()
{
    return false;
}

bool Socket::connectTo(const char *remoteHost, const uint16_t &remotePort, const uint32_t &timeout)
{
    return connectTo(nullptr,remoteHost,remotePort,timeout);
}

bool Socket::connectTo(const char *localBindAddress, const char *remoteHost, const uint16_t &port, const uint32_t &timeout)
{
    return false;
}

void Socket::tryConnect(const char *remoteHost, const uint16_t &port,
                        const uint32_t &timeout)
{
    while (!connectTo(remoteHost, port, timeout))
    {
        // Try to reconnect if fail...
    }
}

bool Socket::listenOn(const uint16_t &port, const char *listenOnAddr, const int32_t &recvbuffer, const int32_t &backlog)
{
    return false;
}

int Socket::closeSocket()
{
    if (!isActive()) return 0;
#ifdef _WIN32
    int i = closesocket(sockfd);
#else
    int i = close(sockfd);
#endif
    sockfd = -1;
    return i;
}

std::string Socket::getLastError() const
{
    return lastError;
}

uint16_t Socket::getPort()
{
    if (!isActive()) return 0;

    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *) &sin, &len) == -1)
    {
        lastError = "Error resolving port";
        return 0;
    }
    return ntohs(sin.sin_port);
}

int Socket::partialRead(void *data, const uint32_t &datalen)
{
    if (!isActive()) return -1;
    if (!datalen) return 0;
    if (!useWrite)
    {
        ssize_t recvLen = recv(sockfd, (char *) data, datalen, 0);
        return recvLen;
    }
    else
    {
        ssize_t recvLen = read(sockfd, (char *) data, datalen);
        return recvLen;
    }
}

int Socket::partialWrite(const void *data, const uint32_t &datalen)
{
    if (!isActive()) return -1;
    if (!datalen) return 0;
    if (!useWrite)
    {
#ifdef _WIN32
        ssize_t sendLen = send(sockfd, (char *) data, datalen, 0);
#else
        ssize_t sendLen = send(sockfd, (char *) data, datalen, MSG_NOSIGNAL);
#endif
        return sendLen;
    }
    else
    {
        ssize_t sendLen = write(sockfd, (char *) data, datalen);
        return sendLen;
    }
}

int Socket::iShutdown(int mode)
{
    if (   (mode == SHUT_RDWR && shutdown_proto_rd == false && shutdown_proto_wr == false)
           || (mode == SHUT_RD && shutdown_proto_rd == false)
           || (mode == SHUT_WR && shutdown_proto_wr == false) )
    {

        switch (mode)
        {
        case SHUT_WR:
            shutdown_proto_wr = true;
            break;
        case SHUT_RD:
            shutdown_proto_rd = true;
            break;
        case SHUT_RDWR:
            shutdown_proto_rd = true;
            shutdown_proto_wr = true;
            break;
        default:
            break;
        }

        return 0;
    }

    // Double shutdown?
    throw std::runtime_error("Double shutdown on Socket");

    return -1;
}

void Socket::socketSystemInitialization()
{
    if (!socketInitialized)
    {
#ifdef _WIN32
        int wsaerr;

        WORD wVersionRequested;
        WSADATA wsaData;

        wVersionRequested = MAKEWORD(2, 2);
        wsaerr = WSAStartup(wVersionRequested, &wsaData);
        if (wsaerr != 0)
        {
            // dll not found.
            badSocket = true;
            return;
        }

        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2 )
        {
            // not supported.
            WSACleanup();
            badSocket = true;
            return;
        }
#endif
        socketInitialized = true;
    }
}


unsigned short Socket::getRemotePort() const
{
    return remotePort;
}

void Socket::setRemotePort(unsigned short value)
{
    remotePort = value;
}

int Socket::getSockOpt(int level, int optname, void *optval, socklen_t *optlen)
{
#ifdef WIN32
    return getsockopt(sockfd, level, optname, (char *)optval, optlen);
#else
    return getsockopt(sockfd, level, optname, optval, optlen);
#endif
}

int Socket::setSockOpt(int level, int optname, const void *optval, socklen_t optlen)
{
#ifdef WIN32
    return setsockopt(sockfd,  level, optname, (char *)optval, optlen);
#else
    return setsockopt(sockfd,  level, optname, optval, optlen);
#endif
}

bool Socket::setReadTimeout(unsigned int _timeout)
{
    if (!isActive()) return false;

    readTimeout = _timeout;

    if (listenMode) return true;

#ifdef _WIN32
    DWORD tout = _timeout*1000;
    if ((setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tout, sizeof(DWORD))) == -1)
#else
    struct timeval timeout;
    timeout.tv_sec = _timeout;
    timeout.tv_usec = 0;
    if ((setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) == -1)
#endif
    {
        return false;
    }
    return true;
}

bool Socket::setWriteTimeout(unsigned int _timeout)
{
    if (!isActive()) return false;
    writeTimeout = _timeout;
    if (listenMode) return true;
#ifdef _WIN32
    int tout = _timeout*1000;
    if ((setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tout, sizeof(int))) == -1)
#else
    struct timeval timeout;
    timeout.tv_sec = _timeout;
    timeout.tv_usec = 0;
    if ((setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) == -1)
#endif
    {
        return false;
    }
    return true;
}

bool Socket::isActive() const
{
    return sockfd!=-1;
}


void Socket::setSocketFD(int _sockfd)
{
    if (sockfd != -1)
    {
        throw std::runtime_error("Assiging a file descriptor to an initialized Socket.");
    }
    sockfd = _sockfd;
}

int Socket::adquireSocketFD()
{
    int sockret = sockfd;
    sockfd = -1;
    return sockret;
}

void Socket::getRemotePair(char * address) const
{
    strncpy(address, remotePair, INET6_ADDRSTRLEN);
}

void Socket::setRemotePair(const char * address)
{
    remotePair[INET6_ADDRSTRLEN] = 0;
    strncpy(remotePair, address, INET6_ADDRSTRLEN);
}

int Socket::shutdownSocket(int mode)
{
    if (!isActive()) return -1;

    int i;
    if ((i=iShutdown(mode))!=0) return i;

    if ( mode == SHUT_RDWR && (shutdown_rd && shutdown_wr) )
    {
        // Double shutdown handling...
        return -1;
    }
    else if ( mode == SHUT_RDWR && (shutdown_rd && !shutdown_wr) )
    {
        // Double shutdown handling...
        mode = SHUT_WR;
        shutdown_rd = true;
    }
    else if ( mode == SHUT_RDWR && (!shutdown_rd && shutdown_wr) )
    {
        // Double shutdown handling...
        mode = SHUT_RD;
        shutdown_wr = true;
    }
    else if ( mode == SHUT_WR && (shutdown_wr) )
    {
        // Double shutdown handling...
        return -1;
    }
    else if ( mode == SHUT_WR && (!shutdown_wr) )
    {
        shutdown_wr = true;
    }
    else if ( mode == SHUT_RD && (shutdown_rd) )
    {
        // Double shutdown handling...
        return -1;
    }
    else if ( mode == SHUT_RD && (!shutdown_rd) )
    {
        shutdown_rd = true;
    }

    return shutdown(sockfd, mode);

    // Double shutdown?
    //    throw std::runtime_error("Double shutdown on Socket");

    //  return -1;
}

bool Socket::setBlockingMode(bool blocking)
{
#ifdef _WIN32
    int iResult;
    unsigned long int iMode = (!blocking)?1:0;
    iResult = ioctlsocket(sockfd, FIONBIO, &iMode);
    return (iResult == NO_ERROR);
#else
    long arg;
    // Set to blocking mode again...
    if( (arg = fcntl(sockfd, F_GETFL, nullptr)) < 0)
    {
        lastError = "Error getting blocking mode... ";
        return false;
    }
    if (blocking)
        arg &= (~O_NONBLOCK);
    else
        arg |= (O_NONBLOCK);

    if( fcntl(sockfd, F_SETFL, arg) < 0)
    {
        lastError = "Error setting blocking...";
        return false;
    }
    return true;
#endif
}

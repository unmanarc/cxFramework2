#include "virtualnetworkinterface.h"

#include <sys/types.h>
#include <sys/stat.h>

#ifndef WIN32
#include <sys/ioctl.h>
#include <linux/if_tun.h>
#include <pwd.h>
#include <grp.h>
#include "netifconfig.h"
#else
#include "tap-windows.h"
#include "iphlpapi.h"
#endif

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

using namespace CX2::Network::Interfaces;

VirtualNetworkInterface::VirtualNetworkInterface()
{
#ifdef WIN32
    fd = INVALID_HANDLE_VALUE;
#else
    fd = -1;
#endif
}

VirtualNetworkInterface::~VirtualNetworkInterface()
{
    stop();
}

bool VirtualNetworkInterface::start(NetIfConfig * netcfg, const std::string &netIfaceName)
{
    interfaceName = netIfaceName;

#ifdef WIN32
    this->NETCLSID = netIfaceName;
    DWORD open_flags = 0;
    devicePath = std::string(USERMODEDEVICEDIR) + netIfaceName + TAP_WIN_SUFFIX;
    fd = CreateFileA(devicePath.c_str(), GENERIC_WRITE | GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | open_flags, 0);
    if (fd != INVALID_HANDLE_VALUE)
        interfaceRealName=netIfaceName;
    // We have the interface handler now...
    if (fd != INVALID_HANDLE_VALUE && netcfg)
    {
        netcfg->openTAPW32Interface(fd, getWinTapAdapterIndex());
        netcfg->setUP(true);
        if (!netcfg->apply())
        {
            lastError = "Failed to configure the interface.";
        }
    }
    return fd!=INVALID_HANDLE_VALUE;
#else
    // Open the TUN/TAP device...
    if((fd = open("/dev/net/tun", O_RDWR)) < 0)
    {
        lastError = "/dev/net/tun error";
        return false;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    // Create the tun/tap interface.
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    if (interfaceName.c_str()[interfaceName.size()-1]>='0' && interfaceName.c_str()[interfaceName.size()-1]<='9')
    {
        snprintf(ifr.ifr_name, IFNAMSIZ, "%s",interfaceName.c_str() );
    }
    else
    {
        snprintf(ifr.ifr_name, IFNAMSIZ, "%s%%d",interfaceName.c_str() );
    }

    if(ioctl(fd, TUNSETIFF, (void*) &ifr) < 0)
    {
        lastError = "TUNSETIFF error";
        stop();
        return false;
    }

    interfaceRealName = ifr.ifr_name;
    if (netcfg)
    {
        if (netcfg->openInterface(interfaceRealName))
        {
            netcfg->setUP(true);
            if (netcfg->apply())
                return true;
            else
            {
                lastError = "Failed to configure the interface.";
                return false;
            }
        }
        else
        {
            lastError = "Failed to open the interface during the network configuration.";
            stop();
            return false;
        }
    }

    return true;
#endif
}

void VirtualNetworkInterface::stop()
{
#ifdef WIN32
    if (fd == INVALID_HANDLE_VALUE)
        return;
    if (CloseHandle(fd) == 0)
        lastError = "Error closing the device.";
    fd = INVALID_HANDLE_VALUE;
#else
    if (fd>=0)
        close(fd);
    fd = -1;
#endif
}


#ifndef WIN32
ssize_t VirtualNetworkInterface::writePacket(const void *packet, unsigned int len)
{
    std::unique_lock<std::mutex> lock(mutexWrite);
    return write(fd,packet,len);
}

ssize_t VirtualNetworkInterface::readPacket(void *packet, unsigned int len)
{
    return read(fd,packet,len);
}

int VirtualNetworkInterface::getInterfaceHandler()
{
    return fd;
}

bool VirtualNetworkInterface::setPersistentMode(bool mode)
{
    if (fd<0) return false;
    int iPersistent = mode?1:0;
    if (ioctl(fd, TUNSETPERSIST, iPersistent) < 0)
        return false;
    return true;
}

bool VirtualNetworkInterface::setOwner(const char *userName)
{
    if (fd<0) return false;
    char pwd_buf[4096];
    struct passwd pwd, *p_pwd;

    getpwnam_r(userName,&pwd,pwd_buf,sizeof(pwd_buf), &p_pwd);
    if (p_pwd)
    {
        if (ioctl(fd, TUNSETOWNER, p_pwd->pw_uid) < 0)
            return false;
        return true;
    }
    return false;
}

bool VirtualNetworkInterface::setGroup(const char *groupName)
{
    if (fd<0) return false;
    char grp_buf[4096];
    struct group grp, *p_grp;

    getgrnam_r(groupName,&grp,grp_buf,sizeof(grp_buf), &p_grp);
    if (p_grp)
    {
        if (ioctl(fd, TUNSETGROUP, p_grp->gr_gid) < 0)
            return false;
        return true;
    }
    return false;
}

#else
HANDLE VirtualNetworkInterface::getWinTapHandler()
{
    return fd;
}

WINTAP_VERSION VirtualNetworkInterface::getWinTapVersion()
{
    WINTAP_VERSION r;
    // Versión del controlador.
    unsigned long info[3] = { 0, 0, 0 };
    DWORD len;
    DeviceIoControl(fd, TAP_WIN_IOCTL_GET_VERSION, &info, sizeof(info),&info, sizeof(info), &len,NULL);
    r.major = info[0];
    r.minor = info[1];
    r.subminor = info[2];
    return r;
}

std::string VirtualNetworkInterface::getWinTapDeviceInfo()
{
    char devInfo[256];
    devInfo[0]=0;
    DWORD len;
    DeviceIoControl(fd, TAP_WIN_IOCTL_GET_INFO, &devInfo, sizeof(devInfo),&devInfo, sizeof(devInfo), &len,NULL);
    return devInfo;
}

std::string VirtualNetworkInterface::getWinTapLogLine()
{
    char devInfo[1024];
    devInfo[0]=0;
    DWORD len;
    DeviceIoControl(fd, TAP_WIN_IOCTL_GET_LOG_LINE, &devInfo, sizeof(devInfo),&devInfo, sizeof(devInfo), &len,NULL);
    return devInfo;
}

DWORD VirtualNetworkInterface::writePacket(const void *packet, DWORD len)
{
    std::unique_lock<std::mutex> lock(mutexWrite);

    DWORD wlen = 0;
    if (WriteFile(fd, packet, len, &wlen, NULL) == 0)
        return 0;
    return wlen;
}

int VirtualNetworkInterface::readPacket(void *packet, DWORD len)
{
    return ReadFile(fd, packet, sizeof packet, &len, NULL);
}

std::string VirtualNetworkInterface::getWinTapDevicePath() const
{
    return devicePath;
}

// Useful for network configuration:
ULONG VirtualNetworkInterface::getWinTapAdapterIndex()
{
    std::wstring wNetCLSID(NETCLSID.begin(), NETCLSID.end());

    ULONG interfaceIndex;
    wchar_t wDevPath[256] = L"\\DEVICE\\TCPIP_";
    lstrcatW(wDevPath,wNetCLSID.c_str());

    if (GetAdapterIndex(wDevPath, &interfaceIndex) != NO_ERROR)
        return ((DWORD)-1);
    else
        return (DWORD)interfaceIndex;
}

#endif

std::string VirtualNetworkInterface::getLastError() const
{
    return lastError;
}

std::string VirtualNetworkInterface::getInterfaceRealName() const
{
    return interfaceRealName;
}


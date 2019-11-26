#include "Network.h"
#include <MswSock.h>
#include <iostream>
using namespace std;

bool Network::init()
{
    WSADATA wsaData = { 0 };
    return ::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

bool Network::unInit()
{
    ::WSACleanup();
    return true;
}

SOCKET Network::socket()
{
    return ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 
		NULL, 0, WSA_FLAG_OVERLAPPED);
}

int Network::bind(SOCKET s, const LPSOCKADDR_IN pAddr)
{
    return ::bind(s, (LPSOCKADDR)pAddr, sizeof(SOCKADDR_IN));
}

int Network::listen(SOCKET s, int backlog)
{
    return ::listen(s, backlog);
}

SOCKADDR_IN Network::getsockname(SOCKET s)
{
    SOCKADDR_IN addr[2];
    int addrLen = sizeof(addr);
    SecureZeroMemory(&addr, addrLen);
    int ret = ::getsockname(s, (PSOCKADDR)&addr, &addrLen);
    if (SOCKET_ERROR == ret)
    {
        cout << "getsockname failed with error: " << WSAGetLastError() << endl;
    }
    return addr[0];
}

SOCKADDR_IN Network::getpeername(SOCKET s)
{
    /*
     * 这里用32个字节接收（sizeof(SOCKADDR_IN) + 16）, 
	 *	否则可能会出现错误WSAEFAULT（code 10014）
     * WSAEFAULT: The name or the namelen parameter is not in a valid part of 
		the user address space, or the namelen parameter is too small.
     * 实践发现如果用16字节，我的手提电脑不会出错，台式电脑出错了
     * 这里应该是跟acceptEx接收地址的缓冲区长度保持一致
    */
    SOCKADDR_IN addr[2];
    int addrLen = sizeof(addr);
    SecureZeroMemory(&addr, addrLen);
    int ret = ::getpeername(s, (PSOCKADDR)&addr, &addrLen);
    if (SOCKET_ERROR == ret)
    {
        cout << "getpeername failed with error: " << WSAGetLastError() << endl;
    }
    return addr[0];
}

bool Network::setKeepAlive(SOCKET s, bool on)
{
    DWORD opt = on;
    int ret = ::setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt, 
		sizeof(DWORD)); //保持激活，长连接；发送“保持活动”包。
    if (SOCKET_ERROR == ret)
    {
        cout << "setsockopt failed with error: " << WSAGetLastError() << endl;
        return false;
    }
    return true;
}

bool Network::setLinger(SOCKET s, bool on, int timeoutSecs)
{
    LINGER linger;
    linger.l_onoff = on;
    linger.l_linger = timeoutSecs;
    int ret = ::setsockopt(s, SOL_SOCKET, SO_LINGER, (char*)&linger,
		sizeof(LINGER)); //关闭时有未发送数据，则逗留。
    if (SOCKET_ERROR == ret)
    {
        cout << "setsockopt failed with error: " << WSAGetLastError() << endl;
        return false;
    }
    return true;
}

bool Network::updateAcceptContext(SOCKET listenSocket, SOCKET acceptSocket)
{
    int ret = ::setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&listenSocket, sizeof(SOCKET)); //让accept的socket继承listen的属性
    if (SOCKET_ERROR == ret)
    {
        cout << "setsockopt failed with error: " << WSAGetLastError() << endl;
        return false;
    }
    return true;
}

#ifndef PCH_H
#define PCH_H
//±ÜÃâstd::max³öÏÖ´íÎóexpected an identifier
#define NOMINMAX
#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>     //for inet_ntop
#pragma comment(lib,"ws2_32.lib")

#endif //PCH_H

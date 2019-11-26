#include <ws2tcpip.h>
#include "Addr.h"
#include <sstream>
using namespace std;

Addr::Addr(const SOCKADDR_IN& addr) : m_addr(addr)
{
}

std::string Addr::toString() const
{
	ostringstream os;
    char peerAddrBuf[32] = { 0 }; //255.255.255.255:36660
    inet_ntop(AF_INET, &m_addr.sin_addr, peerAddrBuf, sizeof(peerAddrBuf));
    os << peerAddrBuf << ":" << ntohs(m_addr.sin_port);
    return os.str();
}

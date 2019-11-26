#ifndef __CLIENT_CONTEXT_H__
#define __CLIENT_CONTEXT_H__
#include "Addr.h"
#include "Buffer.h"
#include "PerIoContext.h"
#include <algorithm>
#include <string>
#include <queue>
#include <map>

struct ListenContext
{
	ListenContext(short port, const std::string& ip = "0.0.0.0");

	SOCKET m_socket; //监听socket
	SOCKADDR_IN m_addr; //监听地址
};

struct IoContext;
struct RecvIoContext;

struct ClientContext
{
	ClientContext(const SOCKET& socket = INVALID_SOCKET);
	//socket由IocpServer释放
	~ClientContext();
	void reset();

	void appendToBuffer(PBYTE pInBuf, size_t len);
	void appendToBuffer(const std::string& inBuf);

	SOCKET m_socket; //客户端socket
	Addr m_addr; //客户端地址
	//Avoids Access Violation，该值为0时才能释放ClientContext
	ULONG m_nPendingIoCnt; 
	RecvIoContext* m_recvIoCtx;
	IoContext* m_sendIoCtx;
	Buffer m_inBuf;
	Buffer m_outBuf;
	std::queue<Buffer> m_outBufQueue;
	CRITICAL_SECTION m_csLock; //保护ClientContext

	DWORD m_nLastHeartbeatTime;
};

#endif // !__CLIENT_CONTEXT_H__


#ifndef __IO_CONTEXT_H__
#define __IO_CONTEXT_H__
#include <WinSock2.h>
#include <Windows.h>

//设置成4K的倍数
constexpr int MAX_BUFFER_LEN = (1024 * 8);

constexpr int ACCEPT_ADDRS_SIZE = sizeof(SOCKADDR_IN) + 16;
constexpr int DOUBLE_ACCEPT_ADDRS_SIZE = (sizeof(SOCKADDR_IN) + 16) * 2;

enum PostType
{
	UNKNOWN, // 用于初始化，无意义
	ACCEPT, // 标志投递的Accept操作
	SEND, // 标志投递的是发送操作
	RECV, // 标志投递的是接收操作
};

enum PostResult
{
	PostResultSucc, //成功
	PostResultFailed, //失败
	PostResultInvalidSocket, //套接字无效
};

//为了保证以下结构体第一个变量成员是OVERLAPPED，
//不能使用虚函数（具有虚函数的类第一个变量是虚表指针）
struct IoContext
{
	//每一个重叠io操作都要有一个OVERLAPPED结构
	OVERLAPPED m_Overlapped;
	PostType m_PostType;
	WSABUF m_wsaBuf;

	IoContext(PostType type);
	~IoContext();
	void resetBuffer();
};

struct AcceptIoContext : public IoContext
{
	AcceptIoContext(SOCKET acceptSocket = INVALID_SOCKET);
	~AcceptIoContext();
	void resetBuffer();
	SOCKET m_acceptSocket; //接受连接的socket
	BYTE m_accpetBuf[MAX_BUFFER_LEN]; //用于acceptEx接收数据
};

struct RecvIoContext : public IoContext
{
	RecvIoContext();
	~RecvIoContext();
	void resetBuffer();
	BYTE m_recvBuf[MAX_BUFFER_LEN];
};

#endif // !__IO_CONTEXT_H__

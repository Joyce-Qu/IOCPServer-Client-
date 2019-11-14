/*==========================================================================
* �����CIocpBase�Ǳ�����ĺ����࣬����˵��WinSock�������˱��ģ���е�
	��ɶ˿�(IOCP)��ʹ�÷���, ���е�IoContext���Ƿ�װ������ÿһ���ص������Ĳ�����
	����˵���˷������˽�����ɶ˿ڡ������������̡߳�Ͷ��Recv����Ͷ��Accept����ķ�����
	���еĿͻ��������Socket����Ҫ�󶨵�IOCP�ϣ����дӿͻ��˷��������ݣ�������ûص�������
*�÷�������һ�����࣬���ػص�����
Created by TTGuoying at 2018/02
Revised by GaoJS at 2019/11
==========================================================================*/
#pragma once
#include <WinSock2.h>
#include <MSWSock.h>
#include <Windows.h>
#include <vector>
#include <list>
#include <string>

using std::list;
using std::vector;
using std::wstring;

#define BUFF_SIZE (1024*4) // I/O ����Ļ�������С
#define WORKER_THREADS_PER_PROCESSOR (1) // ÿ���������ϵ��߳���
#define INIT_IOCONTEXT_NUM (3) // IOContextPool�еĳ�ʼ����
#define MAX_POST_ACCEPT (2) // ͬʱͶ�ݵ�Accept����
#define EXIT_CODE	(-1) // ���ݸ�Worker�̵߳��˳��ź�
#define DEFAULT_PORT	(10240) // Ĭ�϶˿�

// �ͷ�ָ��ĺ�
#define RELEASE_POINTER(x) {if(x != NULL) {delete x; x = NULL;}}
// �ͷž���ĺ�
#define RELEASE_HANDLE(x)	{if(x != NULL && x != INVALID_HANDLE_VALUE)\
	 { CloseHandle(x); x = INVALID_HANDLE_VALUE; }}
// �ͷ�Socket�ĺ�
#define RELEASE_SOCKET(x)	{if(x != INVALID_SOCKET)\
	 { closesocket(x); x = INVALID_SOCKET; }}

#ifndef TRACE
#define TRACE wprintf
//#include <atltrace.h>
//#define TRACE AtlTrace
#endif

enum class IOTYPE
{
	UNKNOWN, // ���ڳ�ʼ����������
	ACCEPT, // Ͷ��Accept����
	SEND, // Ͷ��Send����
	RECV, // Ͷ��Recv����
};

//ÿͶ������IO���󣬶�Ҫ���ṩһ��WSABuf��WSAOVERLAPPED�Ĳ�����
//���ԣ������Զ���һ��IOContext�࣬ÿ��Ͷ�ݸ��������ı�����
//��Ҫע������������������ڣ���ֹ�ڴ�й©��
class IoContext
{
public:
	// ÿ��socket��ÿһ��IO��������Ҫһ���ص��ṹ
	WSAOVERLAPPED overLapped;
	SOCKET hSocket; // ��IO������Ӧ��socket
	WSABUF wsaBuf; // ���ݻ���
	IOTYPE ioType; // IO��������

	IoContext()
	{
		TRACE(L"IoContext()\n");
		hSocket = INVALID_SOCKET;
		ZeroMemory(&overLapped, sizeof(overLapped));
		wsaBuf.buf = (char*)HeapAlloc(GetProcessHeap(),
			HEAP_ZERO_MEMORY, BUFF_SIZE);
		wsaBuf.len = BUFF_SIZE;
		ioType = IOTYPE::UNKNOWN;
	}

	~IoContext()
	{
		TRACE(L"~IoContext()\n");
		RELEASE_SOCKET(hSocket);
		if (wsaBuf.buf != NULL)
		{
			HeapFree(GetProcessHeap(), 0, wsaBuf.buf);
		}
	}

	void Reset()
	{
		TRACE(L"Reset()\n");
		if (wsaBuf.buf != NULL)
		{
			ZeroMemory(wsaBuf.buf, BUFF_SIZE);
		}
		else
		{
			wsaBuf.buf = (char*)HeapAlloc(GetProcessHeap(),
				HEAP_ZERO_MEMORY, BUFF_SIZE);
		}
		ZeroMemory(&overLapped, sizeof(overLapped));
		ioType = IOTYPE::UNKNOWN;
	}
};

// ����ÿһ��socketҲ������һ��IOContextPool�Ļ������
// ���е�IoContext������(IoContext��)
class IoContextPool
{
private:
	list<IoContext*> contextList;
	CRITICAL_SECTION csLock;

public:
	IoContextPool()
	{
		TRACE(L"IoContextPool()\n");
		InitializeCriticalSection(&csLock);
		EnterCriticalSection(&csLock);
		contextList.clear();
		for (size_t i = 0; i < INIT_IOCONTEXT_NUM; i++)
		{
			IoContext* context = new IoContext;
			contextList.push_back(context);
		}
		LeaveCriticalSection(&csLock);
	}

	~IoContextPool()
	{
		TRACE(L"~IoContextPool()\n");
		EnterCriticalSection(&csLock);
		for (list<IoContext*>::iterator it = contextList.begin();
			it != contextList.end(); it++)
		{
			delete (*it);
		}
		contextList.clear();
		LeaveCriticalSection(&csLock);
		DeleteCriticalSection(&csLock);
	}

	// ����һ��IOContxt
	IoContext* AllocateIoContext()
	{
		TRACE(L"AllocateIoContext()\n");
		IoContext* context = NULL;
		EnterCriticalSection(&csLock);
		if (contextList.size() > 0)
		{//list��Ϊ�գ���list��ȡһ��
			context = contextList.back();
			contextList.pop_back();
		}
		else
		{//listΪ�գ��½�һ��
			context = new IoContext;
		}
		LeaveCriticalSection(&csLock);
		return context;
	}

	// ����һ��IOContxt
	void ReleaseIoContext(IoContext* pContext)
	{
		TRACE(L"ReleaseIoContext()\n");
		pContext->Reset();
		EnterCriticalSection(&csLock);
		contextList.push_front(pContext);
		LeaveCriticalSection(&csLock);
	}
};

// ����ÿһ��socketҲ������һ��SocketContext����
class SocketContext
{
public:
	SOCKET connSocket; // ���ӵ�socket
	SOCKADDR_IN clientAddr; // ���ӵ�Զ�̵�ַ

private:
	static IoContextPool ioContextPool; // ���е�IOContext��
	vector<IoContext*> arrIoContext; // ͬһ��socket�ϵĶ��IO����
	CRITICAL_SECTION csLock;

public:
	SocketContext()
	{
		TRACE(L"SocketContext()\n");
		InitializeCriticalSection(&csLock);
		EnterCriticalSection(&csLock);
		arrIoContext.clear(); //û��
		LeaveCriticalSection(&csLock);
		ZeroMemory(&clientAddr, sizeof(clientAddr));
		connSocket = INVALID_SOCKET;
	}

	~SocketContext()
	{
		TRACE(L"~SocketContext()\n");
		RELEASE_SOCKET(connSocket);
		// �������е�IOContext
		EnterCriticalSection(&csLock);
		if (arrIoContext.size() > 0)
		{
			for (vector<IoContext*>::iterator it = arrIoContext.begin();
				it != arrIoContext.end(); it++)
			{
				ioContextPool.ReleaseIoContext(*it);
			}
			arrIoContext.clear();
		}
		LeaveCriticalSection(&csLock);
		DeleteCriticalSection(&csLock);
	}

	// ��ȡһ���µ�IoContext
	IoContext* GetNewIoContext()
	{
		TRACE(L"GetNewIoContext()\n");
		IoContext* context = ioContextPool.AllocateIoContext();
		if (context != NULL)
		{
			EnterCriticalSection(&csLock);
			arrIoContext.push_back(context);
			LeaveCriticalSection(&csLock);
		}
		return context;
	}

	// ���������Ƴ�һ��ָ����IoContext
	void RemoveContext(IoContext* pContext)
	{
		TRACE(L"RemoveContext()\n");
		for (vector<IoContext*>::iterator it = arrIoContext.begin();
			it != arrIoContext.end(); it++)
		{
			if (pContext == *it)
			{
				ioContextPool.ReleaseIoContext(*it);
				EnterCriticalSection(&csLock);
				arrIoContext.erase(it);
				LeaveCriticalSection(&csLock);
				break;
			}
		}
	}
};

// IOCP����
class CIocpBase
{
public:
	CIocpBase();
	~CIocpBase();

	// ��ʼ����
	BOOL Start(int port = DEFAULT_PORT);

	// ֹͣ����
	void Stop();

	// ��ָ���ͻ��˷�������
	BOOL SendData(SocketContext* socketContext, char* data, int size);

	// ��ȡ��ǰ������
	ULONG GetConnectCount() { return connectCount; }

	// ��ȡ��ǰ������
	UINT GetPort() { return port; }

	// �¼�֪ͨ����(���������ش��庯��)
	// ������
	virtual void OnConnectionAccepted(SocketContext* sockContext) = 0;
	// ���ӹر�
	virtual void OnConnectionClosed(SocketContext* sockContext) = 0;
	// �����Ϸ�������
	virtual void OnConnectionError(SocketContext* sockContext, int error) = 0;
	// ���������
	virtual void OnRecvCompleted(SocketContext* sockContext, IoContext* ioContext) = 0;
	// д�������
	virtual void OnSendCompleted(SocketContext* sockContext, IoContext* ioContext) = 0;

protected:
	HANDLE stopEvent; // ֪ͨ�߳��˳���ʱ��
	HANDLE completionPort; // ��ɶ˿�
	HANDLE* workerThreads; // �������̵߳ľ��ָ��
	int workerThreadNum; // �������̵߳�����
	int port; // �����˿�
	SocketContext* listenSockContext; // ����socket��Context
	LONG connectCount; // ��ǰ����������
	LONG acceptPostCount; // ��ǰͶ�ݵĵ�Accept����

	//AcceptEx����ָ��,win8.1�Ժ��֧��
	LPFN_ACCEPTEX fnAcceptEx; 
	//GetAcceptExSockAddrs����ָ��,win8.1�Ժ��֧��
	LPFN_GETACCEPTEXSOCKADDRS fnGetAcceptExSockAddrs;

private:
	// �����̺߳���
	static DWORD WINAPI WorkerThreadProc(LPVOID pThiz);

	// ��ʼ��IOCP
	BOOL InitializeIocp();
	// ��ʼ��Socket
	BOOL InitializeListenSocket();
	// �ͷ���Դ
	void DeInitialize();
	// socket�Ƿ���
	BOOL IsSocketAlive(SOCKET sock);
	// ��ȡ����CPU������
	int GetNumOfProcessors();
	// �����(Socket)�󶨵���ɶ˿���
	BOOL AssociateWithIocp(SocketContext* sockContext);
	// Ͷ��IO����
	BOOL PostAccept(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL PostRecv(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL PostSend(SocketContext*& sockContext, IoContext*& ioContext);
	// IO��������
	BOOL DoAccept(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL DoRecv(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL DoSend(SocketContext*& sockContext, IoContext*& ioContext);
	BOOL DoClose(SocketContext*& sockContext);
};
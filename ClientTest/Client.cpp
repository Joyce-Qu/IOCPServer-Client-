#include "StdAfx.h"
#include "Client.h"
#include "MainDlg.h"
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")

#define RELEASE_HANDLE(x) {if(x != NULL && x!=INVALID_HANDLE_VALUE)\
		{ CloseHandle(x);x = NULL;}}
#define RELEASE_POINTER(x) {if(x != NULL ){delete x;x=NULL;}}
#define RELEASE_ARRAY(x) {if(x != NULL ){delete[] x;x=NULL;}}

CClient::CClient(void) :
	m_strServerIP(DEFAULT_IP),
	m_strLocalIP(DEFAULT_IP),
	m_nThreads(DEFAULT_THREADS),
	m_pMain(NULL),
	m_nPort(DEFAULT_PORT),
	m_strMessage(DEFAULT_MESSAGE),
	//m_phWorkerThreads(NULL),
	//m_pWorkerThreadIds(NULL),
	m_hConnectionThread(NULL),
	m_hShutdownEvent(NULL),
	m_hIOCompletionPort(0)	
{
	//m_LogFunc = NULL;
}

CClient::~CClient(void)
{
	this->Stop();
}

//////////////////////////////////////////////////////////////////////////////////
//	建立连接的线程
DWORD WINAPI CClient::_ConnectionThread(LPVOID lpParam)
{
	ConnectionThreadParam* pParams = (ConnectionThreadParam*)lpParam;
	CClient* pClient = (CClient*)pParams->pClient;
	pClient->ShowMessage("_AcceptThread启动，系统监听中...\n");
	pClient->EstablishConnections();
	pClient->ShowMessage("_ConnectionThread线程结束.\n");
	RELEASE_POINTER(pParams);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////
// 创建IOCP，等待监听事件
// 

#include "..\IOCP3Server\IOCP\PerSocketContext.h"
DWORD WINAPI CClient::_StartIOCP(LPVOID lpParam){
	IOCPThreadParam* pParams = (IOCPThreadParam*)lpParam;
	CClient* pClient = (CClient*)pParams->pClient;
	//while(WAIT_OBJECT_0 != WaitForSingleObject(m_hShutdownEvent, 0)){
	while(true){
		DWORD dwBytesTransfered = 0;
		OVERLAPPED* pOverlapped = nullptr;
		SocketContext* pSoContext = nullptr;
		const BOOL bRet = GetQueuedCompletionStatus(pParams->ioSocket,
													&dwBytesTransfered, (PULONG_PTR)&pSoContext, &pOverlapped, INFINITE);
		IoContext* pIoContext = CONTAINING_RECORD(pOverlapped, IoContext, m_Overlapped);
		if(nullptr == pSoContext)		{
			break;
		}
		if(!bRet) {	//返回值为false，表示出错
			const DWORD dwErr = GetLastError();
			// 显示一下提示信息
			continue;
		} else{
			if((0 == dwBytesTransfered)
			   && (OPERATION_TYPE::RECV == pIoContext->m_OpType
				   || OPERATION_TYPE::SEND == pIoContext->m_OpType))			{
				// Close
				//pIocpModel->_ShowMessage("客户端 %s:%d 断开连接",
				//	inet_ntoa(pSoContext->m_ClientAddr.sin_addr),
				//	ntohs(pSoContext->m_ClientAddr.sin_port));
				// 释放掉对应的资源
				//pIocpModel->_DoClose(pSoContext);
				continue;
			} else if(pIoContext->m_OpType == OPERATION_TYPE::CONNECT){
				BOOL bRet = SetEvent(pClient->m_hConnectedEvent);
				pClient->ShowMessage("Detect connect IO event...!!!!\n");
			}
		}
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////
// 用于发送信息的线程
DWORD WINAPI CClient::_WorkerThread(LPVOID lpParam)
{
	WorkerThreadParam* pParams = (WorkerThreadParam*)lpParam;
	CClient* pClient = (CClient*)pParams->pClient;
	char* pTemp = new char[MAX_BUFFER_LEN];
	int nBytesSent = 0, nBytesRecv = 0;

	ASSERT(pTemp != NULL); //认为内存足够
	InterlockedIncrement(&pClient->m_nRunningWorkerThreads);
	for (int i = 1; i <= pParams->nSendTimes; i++)
	{
		// 监听用户的停止事件
		int nRet = WaitForSingleObject(pClient->m_hShutdownEvent, 0);
		if (WAIT_OBJECT_0 == nRet)
		{
			break; /// return true;
		}
		memset(pTemp, 0, MAX_BUFFER_LEN);
		// 向服务器发送信息
		snprintf(pTemp, MAX_BUFFER_LEN,
			("Msg:[%d] Thread:[%d], Data:[%s]"),
			i, pParams->nThreadNo, pParams->szSendBuffer);
		nBytesSent = send(pParams->sock, pTemp, strlen(pTemp), 0);
		if (SOCKET_ERROR == nBytesSent)
		{
			pClient->ShowMessage("send ERROR: ErrCode=[%ld]\n", WSAGetLastError());
			break; /// return 1;
		}
		pClient->ShowMessage("SENT: %s", pTemp);

		memset(pTemp, 0, MAX_BUFFER_LEN);
		memset(pParams->szRecvBuffer, 0, MAX_BUFFER_LEN);
		nBytesRecv = recv(pParams->sock, pParams->szRecvBuffer,
			MAX_BUFFER_LEN - 1, 0); //这里-1就不会内存越界了
		if (SOCKET_ERROR == nBytesRecv)
		{
			pClient->ShowMessage("recv ERROR: ErrCode=[%ld]\n", WSAGetLastError());
			break; /// return 1;
		}
		//nBytesRecv = 4096 //这句可能导致内存越界
		//pParams->szRecvBuffer[nBytesRecv] = 0;
		snprintf(pTemp, MAX_BUFFER_LEN,
			("RECV: Msg:[%d] Thread[%d], Data[%s]"),
			i, pParams->nThreadNo, pParams->szRecvBuffer);
		pClient->ShowMessage(pTemp);
		Sleep(100);
	}

	if (pParams->nThreadNo == pClient->m_nThreads)
	{
		pClient->ShowMessage(_T("测试并发 %d 个线程完毕."),
			pClient->m_nThreads);
	}
	/*DWORD dwThreadId = GetCurrentThreadId();
	for (int i = 0; i < pClient->m_nThreads; i++)
	{
		if (dwThreadId == pClient->m_pWorkerThreadIds[i])
		{
			pClient->m_pWorkerThreadIds[i] = 0;
			break;
		}
	}*/
	InterlockedDecrement(&pClient->m_nRunningWorkerThreads);
	delete[]pTemp;
	return 0;
}

void NTAPI CClient::poolThreadWork(
	_Inout_ PTP_CALLBACK_INSTANCE Instance,
	_Inout_opt_ PVOID lpParam, _Inout_ PTP_WORK Work)
{
	_WorkerThread(lpParam);
}

///////////////////////////////////////////////////////////////////////////////////
// 建立连接
bool CClient::EstablishConnections()
{
	DWORD nThreadID = 0;
	PCSTR pData = m_strMessage.GetString();
	//m_phWorkerThreads = new HANDLE[m_nThreads];
	//m_pWorkerThreadIds = new DWORD[m_nThreads];
	//memset(m_phWorkerThreads, 0, sizeof(HANDLE) * m_nThreads);
	m_pWorkerParams = new WorkerThreadParam[m_nThreads];
	ASSERT(m_pWorkerParams != 0); //bad_alloc
	memset(m_pWorkerParams, 0, sizeof(WorkerThreadParam) * m_nThreads);

	// 初始化线程池
	InitializeThreadpoolEnvironment(&te);
	threadPool = CreateThreadpool(NULL);
	BOOL bRet = SetThreadpoolThreadMinimum(threadPool, 2);
	SetThreadpoolThreadMaximum(threadPool, m_nThreads);
	SetThreadpoolCallbackPool(&te, threadPool);
	cleanupGroup = CreateThreadpoolCleanupGroup();
	SetThreadpoolCallbackCleanupGroup(&te, cleanupGroup, NULL);
	pWorks = new PTP_WORK[m_nThreads];
	ASSERT(pWorks != 0); //bad_alloc

	// 根据用户设置的线程数量，生成每一个线程连接至服务器，并生成线程发送数据
	for (int i = 0; i < m_nThreads; i++)
	{
		// 监听用户的停止事件
		if (WAIT_OBJECT_0 == WaitForSingleObject(m_hShutdownEvent, 0))
		{
			ShowMessage("接收到用户停止命令.\n");
			return true;
		}
		// 向服务器进行连接
		int ret = this->ConnectToServer(m_pWorkerParams[i].sock,
										m_strServerIP, m_nPort);
		if (ret == -1)
		{
			ShowMessage(_T("连接服务器失败！"));
			//CleanUp(); //这里清除后，线程还在用，就崩溃了
			continue;//return false;
		} else if (ret == 1){
			//WAIT_OBJECT_0 == WaitForSingleObject(m_hShutdownEvent, 0)
			if (WAIT_OBJECT_0 == WaitForSingleObject(m_hConnectedEvent, INFINITE)){
				ShowMessage(_T("连接服务器事件已经收到！"));
				continue;
			} else{
				ShowMessage(_T("连接服务器事件失败！"));
				break;
			}
		}
		m_pWorkerParams[i].nThreadNo = i + 1;
		m_pWorkerParams[i].nSendTimes = m_nTimes;
		snprintf(m_pWorkerParams[i].szSendBuffer,
			MAX_BUFFER_LEN, "%s", pData);
		// 如果连接服务器成功，就开始建立工作者线程，向服务器发送指定数据
		m_pWorkerParams[i].pClient = this;
		/*m_phWorkerThreads[i] = CreateThread(0, 0, _WorkerThread,
			(void*)(&m_pWorkerParams[i]), 0, &nThreadID);
		m_pWorkerThreadIds[i] = nThreadID;*/

		pWorks[i] = CreateThreadpoolWork(poolThreadWork,
			(PVOID)&m_pWorkerParams[i], &te);
		if (pWorks[i] != NULL)
		{
			SubmitThreadpoolWork(pWorks[i]);
		}
		Sleep(10);
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////////
//	向服务器进行Socket连接
int CClient::ConnectToServer(SOCKET& pSocket, CString strServer, int nPort)
{
	struct sockaddr_in ServerAddress;
	struct hostent* Server;
	// 生成SOCKET
	//pSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	pSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	SocketContext* pNewSocketContext = new SocketContext;
	pNewSocketContext->m_Socket = pSocket;
	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pSocket,
										  m_hIOCompletionPort, (DWORD)pNewSocketContext, 0);
	if(nullptr == hTemp) // ERROR_INVALID_PARAMETER=87L
	{
		ShowMessage("Bind IOCP failed, err=%d", GetLastError());
		return -1;
	}
	if (INVALID_SOCKET == pSocket)
	{
		ShowMessage("初始化Socket失败，err=%d\n",
			WSAGetLastError());
		pSocket = NULL;
		return -1;
	}
	// 生成地址信息
	Server = gethostbyname(strServer.GetString());
	if (Server == NULL)
	{
		ShowMessage("无效的服务器地址.\n");
		closesocket(pSocket);
		pSocket = NULL;
		return -1;
	}
	ZeroMemory((char*)&ServerAddress, sizeof(ServerAddress));
	ServerAddress.sin_family = AF_INET;
	CopyMemory((char*)&ServerAddress.sin_addr.s_addr,
		(char*)Server->h_addr, Server->h_length);
	ServerAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

	ServerAddress.sin_port = htons(m_nPort);
	// 开始连接服务
	LPFN_CONNECTEX fn_ConnectEx = nullptr;
	GUID guid = WSAID_CONNECTEX;
	DWORD dwBytes = 0;
	int res = WSAIoctl(pSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
					   &guid, sizeof(guid),
					   &fn_ConnectEx, sizeof(fn_ConnectEx),
					   &dwBytes, NULL, NULL);
	if(!fn_ConnectEx){
		ShowMessage("连接至服务器失败！err=%d\n",
					WSAGetLastError());
		closesocket(pSocket);
		pSocket = NULL;
		return -1;
	} else{
		struct sockaddr_in addr;
		ZeroMemory(&addr, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = 0;
		res = bind(pSocket, (SOCKADDR*)&addr, sizeof(addr));
		if(res != 0){
			printf("bind failed: %d\n", WSAGetLastError());
			return 1;
		}

		// life cycle
		IoContext* pIoContext = new IoContext;
		pIoContext->m_OpType = OPERATION_TYPE::CONNECT;

		if(fn_ConnectEx(pSocket, reinterpret_cast<const struct sockaddr*>(&ServerAddress),
						sizeof(ServerAddress), nullptr, 0, nullptr, &pIoContext->m_Overlapped))	{
			ShowMessage("连接至服务器成功");
			return 0;
		} else{
			int error = WSAGetLastError();
			if(error == WSA_IO_PENDING)	{
				ShowMessage("连接至服务器进入队列");
				return 1;
			} else	{
				ShowMessage("连接至服务器失败,err:%d", errno);
				// MessageId: WSAEINVAL
				//
				// MessageText:
				//
				// An invalid argument was supplied.
				//
				//#define WSAEINVAL                        10022L
				return -1;
			}
		}

		/*int nRet = connect(pSocket, reinterpret_cast<const struct sockaddr*>(&ServerAddress),
						   sizeof(ServerAddress));
		if(nRet==SOCKET_ERROR)	{
			ShowMessage("连接至服务器失败！err=%d\n",
						WSAGetLastError());
			closesocket(pSocket);
			pSocket = NULL;
			return false;
		}*/
	}
	return 0;
}

////////////////////////////////////////////////////////////////////
// 初始化WinSock 2.2
bool CClient::LoadSocketLib()
{
	WSADATA wsaData;
	int nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (NO_ERROR != nResult)
	{
		ShowMessage(_T("初始化WinSock 2.2失败！\n"));
		return false; // 错误
	}
	return true;
}

///////////////////////////////////////////////////////////////////
// 开始监听
bool CClient::Start()
{
	// 建立系统退出的事件通知 bManualReset bInitialState
	m_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hConnectedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_nRunningWorkerThreads = 0;
	// 启动连接线程
	DWORD nThreadID = 0;
	m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
												 nullptr, 0, 0); 
	ConnectionThreadParam* pThreadParams = new ConnectionThreadParam;
	pThreadParams->pClient = this;
	DWORD nIOCPThreadID = 0;
	
	IOCPThreadParam* pIOThreadParams = new IOCPThreadParam;
	pIOThreadParams->pClient = this;
	pIOThreadParams->ioSocket = m_hIOCompletionPort;
	m_hIOCPThread = ::CreateThread(0, 0, _StartIOCP,(void*)pIOThreadParams, 0, &nIOCPThreadID);
	if(nullptr == m_hIOCompletionPort){
		return false;
	}
	m_hConnectionThread = ::CreateThread(0, 0, _ConnectionThread,
		(void*)pThreadParams, 0, &nThreadID);
	return true;
}

///////////////////////////////////////////////////////////////////////
//	停止监听
void CClient::Stop()
{
	if (m_hShutdownEvent == NULL) return;
	BOOL bRet = SetEvent(m_hShutdownEvent);
	//ShowMessage("SetEvent() bRet=%d", bRet);
	// 等待Connection线程退出 INFINITE，超时1000避免卡死
	int nRet = WaitForSingleObject(m_hConnectionThread, 1000);
	//ShowMessage("WaitForSingleObject() nRet=%d", nRet);
	// 关闭所有的Socket
	if (m_pWorkerParams) // && m_phWorkerThreads)
	{
		for (int i = 0; i < m_nThreads; i++)
		{
			if (m_pWorkerParams[i].sock)
			{
				int nRet = closesocket(m_pWorkerParams[i].sock);
				//ShowMessage("closesocket() nRet=%d", nRet);
			}
		}
		while (m_nRunningWorkerThreads > 0)
		{//等待所有工作线程全部退出
			Sleep(100);
		}
	}
	// 取消所有线程池中的线程
	CloseThreadpoolCleanupGroupMembers(cleanupGroup, TRUE, NULL);
	DestroyThreadpoolEnvironment(&te);
	CloseThreadpool(threadPool);
	delete[]pWorks;
	pWorks = NULL;
	CleanUp(); // 清空资源
}

//////////////////////////////////////////////////////////////////////
//	清空资源
void CClient::CleanUp()
{
	if (m_hShutdownEvent == NULL) return;
	//RELEASE_ARRAY(m_phWorkerThreads);
	//RELEASE_ARRAY(m_pWorkerThreadIds);
	RELEASE_HANDLE(m_hConnectionThread);
	RELEASE_ARRAY(m_pWorkerParams);
	RELEASE_HANDLE(m_hShutdownEvent);
}

////////////////////////////////////////////////////////////////////
// 获得本机的IP地址
CString CClient::GetLocalIP()
{
	char hostname[MAX_PATH];
	gethostname(hostname, MAX_PATH); // 获得本机主机名
	struct hostent FAR* lpHostEnt = gethostbyname(hostname);
	if (lpHostEnt == NULL)
	{
		return DEFAULT_IP;
	}
	// 取得IP地址列表中的第一个为返回的IP
	LPSTR lpAddr = lpHostEnt->h_addr_list[0];
	struct in_addr inAddr;
	memmove(&inAddr, lpAddr, 4);
	// 转化成标准的IP地址形式
	m_strLocalIP = CString("192.168.50.85");
	return m_strLocalIP;
}

/////////////////////////////////////////////////////////////////////
// 在主界面中显示信息
void CClient::ShowMessage(const char* szFormat, ...)
{
	//if (this->m_LogFunc)
	{
		const int BUFF_LEN = 256;
		char* pBuff = new char[BUFF_LEN];
		ASSERT(pBuff != NULL);
		memset(pBuff, 0, BUFF_LEN);
		va_list arglist;
		// 处理变长参数
		va_start(arglist, szFormat);
		vsnprintf(pBuff, BUFF_LEN - 1, szFormat, arglist);
		va_end(arglist);

		//this->m_LogFunc(string(pBuff));
		CMainDlg::AddInformation(string(pBuff));
		delete []pBuff;
	}
}

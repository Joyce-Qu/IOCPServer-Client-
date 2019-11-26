#include "IocpModel.h"

#pragma comment(lib, "WS2_32.lib")

CIocpModel::CIocpModel(void) :
	m_nThreads(0),
	m_hShutdownEvent(nullptr),
	m_hIOCompletionPort(nullptr),
	m_phWorkerThreads(nullptr),
	m_strIP(DEFAULT_IP),
	m_nPort(DEFAULT_PORT),
	m_lpfnAcceptEx(nullptr),
	m_lpfnGetAcceptExSockAddrs(nullptr),
	m_pListenContext(nullptr),
	acceptPostCount(0),
	connectCount(0)
{
	errorCount = 0;
	m_LogFunc = nullptr;
	this->LoadSocketLib();
}

CIocpModel::~CIocpModel(void)
{
	// 确保资源彻底释放
	this->Stop();
	this->UnloadSocketLib();
}

///////////////////////////////////////////////////////////////////
// 工作者线程： 为IOCP请求服务的工作者线程
// 也就是每当完成端口上出现了完成数据包，就将之取出来进行处理的线程
///////////////////////////////////////////////////////////////////
/*********************************************************************
*函数功能：线程函数，根据GetQueuedCompletionStatus返回情况进行处理；
*函数参数：lpParam是THREADPARAMS_WORKER类型指针；
*函数说明：GetQueuedCompletionStatus正确返回时表示某操作已经完成，
	第二个参数lpNumberOfBytes表示本次套接字传输的字节数，
参数lpCompletionKey和lpOverlapped包含重要的信息，请查询MSDN文档；
*********************************************************************/
DWORD WINAPI CIocpModel::_WorkerThread(LPVOID lpParam)
{
	WorkerThreadParam* pParam = (WorkerThreadParam*)lpParam;
	CIocpModel* pIocpModel = (CIocpModel*)pParam->pIocpModel;
	const int nThreadNo = pParam->nThreadNo;
	const int nThreadId = pParam->nThreadId;

	pIocpModel->_ShowMessage("工作者线程，No:%d, ID:%d", nThreadNo, nThreadId);
	//循环处理请求，直到接收到Shutdown信息为止
	while (WAIT_OBJECT_0 != WaitForSingleObject(pIocpModel->m_hShutdownEvent, 0))
	{
		DWORD dwBytesTransfered = 0;
		OVERLAPPED* pOverlapped = nullptr;
		SocketContext* pSoContext = nullptr;
		const BOOL bRet = GetQueuedCompletionStatus(pIocpModel->m_hIOCompletionPort,
			&dwBytesTransfered, (PULONG_PTR)&pSoContext, &pOverlapped, INFINITE);		
		IoContext* pIoContext = CONTAINING_RECORD(pOverlapped, 
			IoContext, m_Overlapped); // 读取传入的参数
		//接收EXIT_CODE退出标志，则直接退出
		if (EXIT_CODE == (DWORD)pSoContext)
		{
			break;
		}
		if (!bRet)
		{	//返回值为false，表示出错
			const DWORD dwErr = GetLastError();
			// 显示一下提示信息
			if (!pIocpModel->HandleError(pSoContext, dwErr))
			{
				break;
			}
			continue;
		}
		else
		{
			// 判断是否有客户端断开了
			if ((0 == dwBytesTransfered)
				&& (PostType::RECV == pIoContext->m_PostType
					|| PostType::SEND == pIoContext->m_PostType))
			{
				pIocpModel->OnConnectionClosed(pSoContext);
				//pIocpModel->_ShowMessage("客户端 %s:%d 断开连接",
				//	inet_ntoa(pSoContext->m_ClientAddr.sin_addr),
				//	ntohs(pSoContext->m_ClientAddr.sin_port));
				// 释放掉对应的资源
				pIocpModel->_DoClose(pSoContext);
				continue;
			}
			else
			{
				switch (pIoContext->m_PostType)
				{
				case PostType::ACCEPT:
				{
					// 为了增加代码可读性，这里用专门的_DoAccept函数进行处理连入请求
					pIoContext->m_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoAccept(pSoContext, pIoContext);
				}
				break;

				case PostType::RECV:
				{
					// 为了增加代码可读性，这里用专门的_DoRecv函数进行处理接收请求
					pIoContext->m_nTotalBytes = dwBytesTransfered;
					pIocpModel->_DoRecv(pSoContext, pIoContext);
				}
				break;

				// 这里略过不写了，要不代码太多了，不容易理解，Send操作相对来讲简单一些
				case PostType::SEND:
				{
					pIoContext->m_nSentBytes += dwBytesTransfered;
					pIocpModel->_DoSend(pSoContext, pIoContext);
				}
				break;
				default:
					// 不应该执行到这里
					pIocpModel->_ShowMessage("_WorkThread中的m_OpType 参数异常");
					break;
				} //switch
			}//if
		}//if
	}//while
	pIocpModel->_ShowMessage("工作者线程 %d 号退出", nThreadNo);
	// 释放线程参数
	RELEASE_POINTER(lpParam);
	return 0;
}

//================================================================================
//				 系统初始化和终止
//================================================================================
////////////////////////////////////////////////////////////////////
// 初始化WinSock 2.2
//函数功能：初始化套接字
bool CIocpModel::LoadSocketLib()
{
	WSADATA wsaData = { 0 };
	const int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
	// 错误(一般都不可能出现)
	if (NO_ERROR != nRet)
	{
		this->_ShowMessage("初始化WinSock 2.2失败！");
		return false;
	}
	return true;
}

//函数功能：启动服务器
bool CIocpModel::Start(int port)
{
	m_nPort = port;
	// 初始化线程互斥量
	InitializeCriticalSection(&m_csContextList);
	// 建立系统退出的事件通知
	m_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	// 初始化IOCP
	if (!_InitializeIOCP())
	{
		this->_ShowMessage("初始化IOCP失败！");
		return false;
	}
	else
	{
		this->_ShowMessage("初始化IOCP完毕！");
	}
	// 初始化Socket
	if (!_InitializeListenSocket())
	{
		this->_ShowMessage("监听Socket初始化失败！");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("监听Socket初始化完毕");
	}
	this->_ShowMessage("系统准备就绪，等候连接...");
	return true;
}

////////////////////////////////////////////////////////////////////
//	开始发送系统退出消息，退出完成端口和线程资源
void CIocpModel::Stop()
{
	if (m_pListenContext != nullptr
		&& m_pListenContext->m_Socket != INVALID_SOCKET)
	{
		// 激活关闭消息通知
		SetEvent(m_hShutdownEvent);
		for (int i = 0; i < m_nThreads; i++)
		{
			// 通知所有的完成端口操作退出
			PostQueuedCompletionStatus(m_hIOCompletionPort,
				0, (DWORD)EXIT_CODE, NULL);
		}
		// 等待所有的客户端资源退出
		WaitForMultipleObjects(m_nThreads, m_phWorkerThreads,
			TRUE, INFINITE);
		// 清除客户端列表信息
		this->_ClearContextList();
		// 释放其他资源
		this->_DeInitialize();
		this->_ShowMessage("停止监听");
	}
	else
	{
		m_pListenContext = nullptr;
	}
}

bool CIocpModel::SendData(SocketContext* pSoContext, char* data, int size)
{
	this->_ShowMessage("SendData(): s=%p d=%p", pSoContext, data);
	if (!pSoContext || !data || size <= 0 || size > MAX_BUFFER_LEN)
	{
		this->_ShowMessage("SendData()，参数有误");
		return false;
	}
	//投递WSASend请求，发送数据
	IoContext* pNewIoContext = pSoContext->GetNewIoContext();
	pNewIoContext->m_acceptSocket = pSoContext->m_Socket;
	pNewIoContext->m_PostType = PostType::SEND;
	pNewIoContext->m_nTotalBytes = size;
	pNewIoContext->m_wsaBuf.len = size;
	memcpy(pNewIoContext->m_wsaBuf.buf, data, size);
	if (!this->_PostSend(pSoContext, pNewIoContext))
	{// 无需RELEASE_POINTER，失败时，已经release了
		// RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	return true;
}

bool CIocpModel::SendData(SocketContext* pSoContext, IoContext* pIoContext)
{
	return this->_PostSend(pSoContext, pIoContext);
}

bool CIocpModel::RecvData(SocketContext* pSoContext, IoContext* pIoContext)
{
	return this->_PostRecv(pSoContext, pIoContext);
}

////////////////////////////////
// 初始化完成端口
bool CIocpModel::_InitializeIOCP()
{
	this->_ShowMessage("初始化IOCP-InitializeIOCP()");
	//If this parameter is zero, the system allows as many 
	//concurrently running threads as there are processors in the system.
	//如果此参数为零，则系统允许的并发运行线程数量与系统中的处理器数量相同。
	m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
		nullptr, 0, 0); //NumberOfConcurrentThreads
	if (nullptr == m_hIOCompletionPort)
	{
		this->_ShowMessage("建立完成端口失败！错误代码: %d!", WSAGetLastError());
		return false;
	}
	// 根据本机中的处理器数量，建立对应的线程数
	m_nThreads = WORKER_THREADS_PER_PROCESSOR * _GetNumOfProcessors();
	// 为工作者线程初始化句柄
	m_phWorkerThreads = new HANDLE[m_nThreads];
	// 根据计算出来的数量建立工作者线程
	DWORD nThreadID = 0;
	for (int i = 0; i < m_nThreads; i++)
	{
		WorkerThreadParam* pThreadParams = new WorkerThreadParam;
		pThreadParams->pIocpModel = this;
		pThreadParams->nThreadNo = i + 1;
		m_phWorkerThreads[i] = ::CreateThread(0, 0, _WorkerThread,
			(void*)pThreadParams, 0, &nThreadID);
		pThreadParams->nThreadId = nThreadID;
	}
	this->_ShowMessage("建立WorkerThread %d 个", m_nThreads);
	return true;
}

/////////////////////////////////////////////////////////////////
// 初始化Socket
bool CIocpModel::_InitializeListenSocket()
{
	this->_ShowMessage("初始化Socket-InitializeListenSocket()");
	// 生成用于监听的Socket的信息
	m_pListenContext = new SocketContext;
	// 需要使用重叠IO，必须得使用WSASocket来建立Socket，才可以支持重叠IO操作
	m_pListenContext->m_Socket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_pListenContext->m_Socket)
	{
		this->_ShowMessage("WSASocket() 失败，err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("创建 WSASocket() 完成");
	}

	// 将Listen Socket绑定至完成端口中
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenContext->m_Socket,
		m_hIOCompletionPort, (DWORD)m_pListenContext, 0))
	{
		this->_ShowMessage("绑定失败！err=%d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("绑定完成端口 完成");
	}

	// 填充地址信息
	// 服务器地址信息，用于绑定Socket
	sockaddr_in serverAddress;
	ZeroMemory((char*)&serverAddress, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	// 这里可以绑定任何可用的IP地址，或者绑定一个指定的IP地址 
	// ServerAddress.sin_addr.s_addr = inet_addr(m_strIP.c_str());
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(m_nPort);

	// 绑定地址和端口
	if (SOCKET_ERROR == bind(m_pListenContext->m_Socket,
		(sockaddr*)&serverAddress, sizeof(serverAddress)))
	{
		this->_ShowMessage("bind()函数执行错误");
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("bind() 完成");
	}

	// 开始进行监听
	if (SOCKET_ERROR == listen(m_pListenContext->m_Socket, MAX_LISTEN_SOCKET))
	{
		this->_ShowMessage("listen()出错, err=%d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage("listen() 完成");
	}

	// 使用AcceptEx函数，因为这个是属于WinSock2规范之外的微软另外提供的扩展函数
	// 所以需要额外获取一下函数的指针，获取AcceptEx函数指针
	DWORD dwBytes = 0;
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(m_pListenContext->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
		sizeof(GuidAcceptEx), &m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoctl 未能获取AcceptEx函数指针。错误代码: %d",
			WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	// 获取GetAcceptExSockAddrs函数指针，也是同理
	if (SOCKET_ERROR == WSAIoctl(m_pListenContext->m_Socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs), &m_lpfnGetAcceptExSockAddrs,
		sizeof(m_lpfnGetAcceptExSockAddrs), &dwBytes, NULL, NULL))
	{
		this->_ShowMessage("WSAIoctl 未能获取GuidGetAcceptExSockAddrs函数指针。"
			"错误代码: %d", WSAGetLastError());
		this->_DeInitialize();
		return false;
	}

	// 为AcceptEx 准备参数，然后投递AcceptEx I/O请求
	// 创建10个套接字，投递AcceptEx请求，即共有10个套接字进行accept操作；
	for (size_t i = 0; i < MAX_POST_ACCEPT; i++)
	{
		// 新建一个IO_CONTEXT
		IoContext* pIoContext = m_pListenContext->GetNewIoContext();
		if (pIoContext && !this->_PostAccept(pIoContext))
		{
			m_pListenContext->RemoveContext(pIoContext);
			return false;
		}
	}
	this->_ShowMessage("投递 %d 个AcceptEx请求完毕", MAX_POST_ACCEPT);
	return true;
}

////////////////////////////////////////////////////////////
//	最后释放掉所有资源
void CIocpModel::_DeInitialize()
{
	// 删除客户端列表的互斥量
	DeleteCriticalSection(&m_csContextList);
	// 关闭系统退出事件句柄
	RELEASE_HANDLE(m_hShutdownEvent);
	// 释放工作者线程句柄指针
	for (int i = 0; i < m_nThreads; i++)
	{
		RELEASE_HANDLE(m_phWorkerThreads[i]);
	}

	RELEASE_ARRAY(m_phWorkerThreads);
	// 关闭IOCP句柄
	RELEASE_HANDLE(m_hIOCompletionPort);
	// 关闭监听Socket
	RELEASE_POINTER(m_pListenContext);
	this->_ShowMessage("释放资源完毕");
}

//================================================================================
//				 投递完成端口请求
//================================================================================
//////////////////////////////////////////////////////////////////
// 投递Accept请求
bool CIocpModel::_PostAccept(IoContext* pIoContext)
{
	if (m_pListenContext == NULL || m_pListenContext->m_Socket == INVALID_SOCKET)
	{
		throw "_PostAccept,m_pListenContext or m_Socket INVALID!";
	}
	// 准备参数
	pIoContext->ResetBuffer();
	pIoContext->m_PostType = PostType::ACCEPT;
	// SOCKET hClient = accept(hSocket, NULL, NULL); //传统accept
	// 为以后新连入的客户端先准备好Socket( 这个是与传统accept最大的区别 ) 
	pIoContext->m_acceptSocket = WSASocket(AF_INET, SOCK_STREAM,
		IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == pIoContext->m_acceptSocket)
	{// 投递多少次ACCEPT，就创建多少个socket；
		_ShowMessage("创建用于Accept的Socket失败！err=%d", WSAGetLastError());
		return false;
	}
	//https://docs.microsoft.com/zh-cn/windows/win32/api/mswsock/nf-mswsock-acceptex
	// 投递AcceptEx // 将接收缓冲置为0,令AcceptEx直接返回,防止拒绝服务攻击	
	DWORD dwBytes = 0, dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	WSABUF* pWSAbuf = &pIoContext->m_wsaBuf; //必须+16,参见MSDN
	if (!m_lpfnAcceptEx(m_pListenContext->m_Socket,
		pIoContext->m_acceptSocket, pWSAbuf->buf,
		0, //pWSAbuf->len - (dwAddrLen * 2),
		dwAddrLen, dwAddrLen, &dwBytes,
		&pIoContext->m_Overlapped))
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			_ShowMessage("投递 AcceptEx 失败，err=%d", nErr);
			return false;
		}
	}
	InterlockedIncrement(&acceptPostCount);
	return true;
}

////////////////////////////////////////////////////////////
// 在有客户端连入的时候，进行处理
// 流程有点复杂，你要是看不懂的话，就看配套的文档吧....
// 如果能理解这里的话，完成端口的机制你就消化了一大半了

// 总之你要知道，传入的是ListenSocket的Context，我们需要复制一份出来给新连入的Socket用
// 原来的Context还是要在上面继续投递下一个Accept请求
/********************************************************************
*函数功能：函数进行客户端接入处理；
*参数说明：
SocketContext* pSoContext:	本次accept操作对应的套接字，该套接字所对应的数据结构；
IoContext* pIoContext:			本次accept操作对应的数据结构；
DWORD		dwIOSize:			本次操作数据实际传输的字节数
********************************************************************/
#include <mstcpip.h> //tcp_keepalive
bool CIocpModel::_DoAccept(SocketContext* pSoContext, IoContext* pIoContext)
{//这里的pSoContext是listenSocketContext
	InterlockedIncrement(&connectCount);
	InterlockedDecrement(&acceptPostCount);
#if 1 //无法得到对方的IP地址呢！
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	DWORD dwAddrLen = (sizeof(SOCKADDR_IN) + 16);
	int remoteLen = 0, localLen = 0; //必须+16,参见MSDN
	this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf,
		0, //pIoContext->m_wsaBuf.len - (dwAddrLen * 2),
		dwAddrLen, dwAddrLen, (LPSOCKADDR*)&localAddr,
		&localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);

	// 2. 为新连接建立一个SocketContext 
	SocketContext* pNewSocketContext = new SocketContext;
	//加入到ContextList中去(需要统一管理，方便释放资源)
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->m_Socket = pIoContext->m_acceptSocket;
	memcpy(&(pNewSocketContext->m_ClientAddr), 
		clientAddr, sizeof(SOCKADDR_IN));

	// 3. 将listenSocketContext的IOContext 重置后继续投递AcceptEx
	if (!_PostAccept(pIoContext))
	{
		pSoContext->RemoveContext(pIoContext);
	}

	// 4. 将新socket和完成端口绑定
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{//无需RELEASE_POINTER，失败时，已经release了
		// RELEASE_POINTER(pNewSocketContext);
		return false;
	}

	// 并设置tcp_keepalive
	tcp_keepalive alive_in = { 0 }, alive_out = { 0 };
	// 60s  多长时间（ ms ）没有数据就开始 send 心跳包
	alive_in.keepalivetime = 1000 * 60; //1分钟
	// 10s  每隔多长时间（ ms ） send 一个心跳包
	alive_in.keepaliveinterval = 1000 * 10; //10s
	alive_in.onoff = TRUE;
	DWORD lpcbBytesReturned = 0;
	if (SOCKET_ERROR == WSAIoctl(pNewSocketContext->m_Socket,
		SIO_KEEPALIVE_VALS, &alive_in, sizeof(alive_in), &alive_out,
		sizeof(alive_out), &lpcbBytesReturned, NULL, NULL))
	{
		_ShowMessage("WSAIoctl() failed: %d\n", WSAGetLastError());
	}
	OnConnectionAccepted(pNewSocketContext);

	// 5. 建立recv操作所需的ioContext，在新连接的socket上投递recv请求
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	if (pNewIoContext != NULL)
	{//不成功，会怎么样？
		pNewIoContext->m_PostType = PostType::RECV;
		pNewIoContext->m_acceptSocket = pNewSocketContext->m_Socket;
		// 投递recv请求
		return _PostRecv(pNewSocketContext, pNewIoContext);
	}
	else
	{
		_DoClose(pNewSocketContext);
		return false;
	}
#else //貌似无需区分 是否WithData
	if (pIoContext->m_nTotalBytes > 0)
	{
		//客户接入时，第一次接收dwIOSize字节数据
		_DoFirstRecvWithData(pIoContext);
	}
	else
	{
		//客户端接入时，没有发送数据，则投递WSARecv请求，接收数据
		_DoFirstRecvWithoutData(pIoContext);
	}
	// 5. 使用完毕之后，把Listen Socket的那个IoContext重置，然后准备投递新的AcceptEx
	return this->_PostAccept(pIoContext);
#endif
}

/*************************************************************
*函数功能：AcceptEx接收客户连接成功，接收客户第一次发送的数据，故投递WSASend请求
*函数参数：IoContext* pIoContext:	用于监听套接字上的操作
**************************************************************/
bool CIocpModel::_DoFirstRecvWithData(IoContext* pIoContext)
{
	SOCKADDR_IN* clientAddr = NULL, * localAddr = NULL;
	int remoteLen, localLen, addrLen = sizeof(SOCKADDR_IN);
	///////////////////////////////////////////////////////////////////////////
	// 1. 首先取得连入客户端的地址信息
	// 这个 m_lpfnGetAcceptExSockAddrs 不得了啊~~~~~~
	// 不但可以取得客户端和本地端的地址信息，还能顺便取出第一组数据，老强大了...
	this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf,
		pIoContext->m_wsaBuf.len - ((addrLen + 16) * 2),
		addrLen + 16, addrLen + 16, (LPSOCKADDR*)&localAddr,
		&localLen, (LPSOCKADDR*)&clientAddr, &remoteLen);
	// 显示客户端信息
	this->_ShowMessage("客户端 %s:%d 连入了!", inet_ntoa(clientAddr->sin_addr),
		ntohs(clientAddr->sin_port));
	this->_ShowMessage("收到 %s:%d 信息：%s", inet_ntoa(clientAddr->sin_addr),
		ntohs(clientAddr->sin_port), pIoContext->m_wsaBuf.buf);

	////////////////////////////////////////////////////////////////////////////////
	// 2. 这里需要注意，这里传入的这个是ListenSocket上的Context，
	// 这个Context我们还需要用于监听下一个连接，所以我还得要将ListenSocket
	//	上的Context复制出来一份，为新连入的Socket新建一个SocketContext
	//	为新接入的套接创建SocketContext，并将该套接字绑定到完成端口
	SocketContext* pNewSocketContext = new SocketContext;
	this->_ShowMessage("pNewSocketContext=%p", pNewSocketContext);
	//加入到ContextList中去(需要统一管理，方便释放资源)
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->m_Socket = pIoContext->m_acceptSocket;
	memcpy(&(pNewSocketContext->m_ClientAddr), clientAddr, remoteLen);
	// 3. 将该套接字绑定到完成端口
	// 参数设置完毕，将这个Socket和完成端口绑定(这也是一个关键步骤)
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{//无需RELEASE_POINTER，失败时，已经release了
		// RELEASE_POINTER(pNewSocketContext);
		return false;
	}

	// 4. 如果投递成功，那么就把这个有效的客户端信息，
	this->OnConnectionAccepted(pNewSocketContext);
	// 一定要新建一个IoContext，因为原有的是ListenSocket的
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	pNewIoContext->m_PostType = PostType::RECV;
	pNewIoContext->m_acceptSocket = pNewSocketContext->m_Socket;
	pNewIoContext->m_nTotalBytes = pIoContext->m_nTotalBytes;
	pNewIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes;
	memcpy(pNewIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.buf,
		pIoContext->m_nTotalBytes);	//复制数据到WSASend函数的参数缓冲区
	this->_DoRecv(pNewSocketContext, pNewIoContext);

	//////////////////////////////////////////////////////////////////////////////////
	// 5. 使用完毕之后，把Listen Socket的那个IoContext重置，然后准备投递新的AcceptEx
	// return this->_PostAccept(pIoContext );
	return true;
}

/*************************************************************
*函数功能：AcceptEx接收客户连接成功，此时并未接收到数据，故投递WSARecv请求
*函数参数：IoContext* pIoContext:	用于监听套接字上的操作
**************************************************************/
bool CIocpModel::_DoFirstRecvWithoutData(IoContext* pIoContext)
{
	//为新接入的套接字创建SocketContext结构，并绑定到完成端口
	// 1. 首先取得连入客户端的地址信息
	SOCKADDR_IN clientAddr = { 0 };
	int addrLen = sizeof(clientAddr);
	getpeername(pIoContext->m_acceptSocket, (SOCKADDR*)&clientAddr, &addrLen);
	this->_ShowMessage("客户端 %s:%d 连入了", inet_ntoa(clientAddr.sin_addr),
		ntohs(clientAddr.sin_port));
	// 2. 这里需要注意，这里传入的这个是ListenSocket上的Context，
	SocketContext* pNewSocketContext = new SocketContext;
	this->_ShowMessage("pNewSocketContext=%p", pNewSocketContext);
	//加入到ContextList中去(需要统一管理，方便释放资源)
	this->_AddToContextList(pNewSocketContext);
	pNewSocketContext->m_Socket = pIoContext->m_acceptSocket;
	memcpy(&(pNewSocketContext->m_ClientAddr),
		&clientAddr, sizeof(clientAddr));
	// 3. 将该套接字绑定到完成端口
	if (!this->_AssociateWithIOCP(pNewSocketContext))
	{//无需RELEASE_POINTER，失败时，已经release了
		//RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	// 4.投递WSARecv请求，接收数据
	IoContext* pNewIoContext = pNewSocketContext->GetNewIoContext();
	//此时是AcceptEx未接收到客户端第一次发送的数据，
	//所以这里调用PostRecv，接收来自客户端的数据
	if (!this->_PostRecv(pNewSocketContext, pNewIoContext))
	{//无需RELEASE_POINTER，失败时，已经release了
		//RELEASE_POINTER(pNewSocketContext);
		return false;
	}
	// 5.整个流程就全部成功，继续投递Accept
	// return this->_PostAccept(pIoContext );
	this->OnConnectionAccepted(pNewSocketContext);
	return true;
}

/*************************************************************
*函数功能：投递WSARecv请求；
*函数参数：
IoContext* pIoContext:	用于进行IO的套接字上的结构，主要为WSARecv参数和WSASend参数；
**************************************************************/
bool CIocpModel::_PostRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	pIoContext->ResetBuffer();
	pIoContext->m_PostType = PostType::RECV;
	pIoContext->m_nTotalBytes = 0;
	pIoContext->m_nSentBytes = 0;
	// 初始化变量
	DWORD dwFlags = 0, dwBytes = 0;
	// 初始化完成后，投递WSARecv请求
	const int nBytesRecv = WSARecv(pIoContext->m_acceptSocket,
		&pIoContext->m_wsaBuf, 1, &dwBytes, &dwFlags,
		&pIoContext->m_Overlapped, NULL);
	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if (SOCKET_ERROR == nBytesRecv)
	{
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			this->_ShowMessage("投递WSARecv失败！err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

/////////////////////////////////////////////////////////////////
// 在有接收的数据到达的时候，进行处理
bool CIocpModel::_DoRecv(SocketContext* pSoContext, IoContext* pIoContext)
{
	// 先把上一次的数据显示出现，然后就重置状态，发出下一个Recv请求
	SOCKADDR_IN* clientAddr = &pSoContext->m_ClientAddr;
	//this->_ShowMessage("收到 %s:%d 信息：%s", inet_ntoa(clientAddr->sin_addr),
	//	ntohs(clientAddr->sin_port), pIoContext->m_wsaBuf.buf);
	// 然后开始投递下一个WSARecv请求 //发送数据
	//这里不应该直接PostWrite，发什么应该由应用决定
	this->OnRecvCompleted(pSoContext, pIoContext);
	//return _PostRecv(pSoContext, pIoContext);
	return true; //交给应用层，不继续接收了
}

/*************************************************************
*函数功能：投递WSASend请求
*函数参数：
IoContext* pIoContext:	用于进行IO的套接字上的结构，主要为WSARecv参数和WSASend参数
*函数说明：调用PostWrite之前需要设置pIoContext中m_wsaBuf, m_nTotalBytes, m_nSendBytes；
**************************************************************/
bool CIocpModel::_PostSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	// 初始化变量
	////pIoContext->ResetBuffer(); //外部设置m_wsaBuf
	pIoContext->m_PostType = PostType::SEND;
	pIoContext->m_nTotalBytes = 0;
	pIoContext->m_nSentBytes = 0;
	//投递WSASend请求 -- 需要修改
	const DWORD dwFlags = 0;
	DWORD dwSendNumBytes = 0;
	const int nRet = WSASend(pIoContext->m_acceptSocket,
		&pIoContext->m_wsaBuf, 1, &dwSendNumBytes, dwFlags,
		&pIoContext->m_Overlapped, NULL);
	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if (SOCKET_ERROR == nRet)
	{ //WSAENOTCONN=10057L
		int nErr = WSAGetLastError();
		if (WSA_IO_PENDING != nErr)
		{// Overlapped I/O operation is in progress.
			this->_ShowMessage("投递WSASend失败！err=%d", nErr);
			this->_DoClose(pSoContext);
			return false;
		}
	}
	return true;
}

bool CIocpModel::_DoSend(SocketContext* pSoContext, IoContext* pIoContext)
{
	if (pIoContext->m_nSentBytes < pIoContext->m_nTotalBytes)
	{
		//数据未能发送完，继续发送数据
		pIoContext->m_wsaBuf.buf = pIoContext->m_szBuffer
			+ pIoContext->m_nSentBytes;
		pIoContext->m_wsaBuf.len = pIoContext->m_nTotalBytes
			- pIoContext->m_nSentBytes;
		return this->_PostSend(pSoContext, pIoContext);
	}
	else
	{
		this->OnSendCompleted(pSoContext, pIoContext);
		//return this->_PostRecv(pSoContext, pIoContext);
		return true; //通知应用层，发送完毕，不主动接收
	}
}

bool CIocpModel::_DoClose(SocketContext* pSoContext)
{
	//this->_ShowMessage("_DoClose() pSoContext=%p", pSoContext);
	if (pSoContext != m_pListenContext)
	{// m_pListenContext不在vector中，找不到
		InterlockedDecrement(&connectCount);
		this->_RemoveContext(pSoContext);
		return true;
	}
	InterlockedIncrement(&errorCount);
	return false;
}

/////////////////////////////////////////////////////
// 将句柄(Socket)绑定到完成端口中
bool CIocpModel::_AssociateWithIOCP(SocketContext* pSoContext)
{
	// 将用于和客户端通信的SOCKET绑定到完成端口中
	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pSoContext->m_Socket,
		m_hIOCompletionPort, (DWORD)pSoContext, 0);
	if (nullptr == hTemp) // ERROR_INVALID_PARAMETER=87L
	{
		this->_ShowMessage("绑定IOCP失败。err=%d", GetLastError());
		this->_DoClose(pSoContext);
		return false;
	}
	return true;
}

//=====================================================================
//				 ContextList 相关操作
//=====================================================================
//////////////////////////////////////////////////////////////
// 将客户端的相关信息存储到数组中
void CIocpModel::_AddToContextList(SocketContext* pSoContext)
{
	EnterCriticalSection(&m_csContextList);
	m_arrayClientContext.push_back(pSoContext);
	LeaveCriticalSection(&m_csContextList);
}

////////////////////////////////////////////////////////////////
//	移除某个特定的Context
void CIocpModel::_RemoveContext(SocketContext* pSoContext)
{
	EnterCriticalSection(&m_csContextList);
	vector<SocketContext*>::iterator it;
	it = m_arrayClientContext.begin();
	while (it != m_arrayClientContext.end())
	{
		SocketContext* pContext = *it;
		if (pSoContext == pContext)
		{
			delete pSoContext;
			pSoContext = nullptr;
			it = m_arrayClientContext.erase(it);
			break;
		}
		it++;
	}
	LeaveCriticalSection(&m_csContextList);
}

////////////////////////////////////////////////////////////////
// 清空客户端信息
void CIocpModel::_ClearContextList()
{
	EnterCriticalSection(&m_csContextList);
	for (size_t i = 0; i < m_arrayClientContext.size(); i++)
	{
		delete m_arrayClientContext.at(i);
	}
	m_arrayClientContext.clear();
	LeaveCriticalSection(&m_csContextList);
}

//================================================================================
//				 其他辅助函数定义
//================================================================================
////////////////////////////////////////////////////////////////////
// 获得本机的IP地址
string CIocpModel::GetLocalIP()
{
	// 获得本机主机名
	char hostname[MAX_PATH] = { 0 };
	gethostname(hostname, MAX_PATH);
	struct hostent FAR* lpHostEnt = gethostbyname(hostname);
	if (lpHostEnt == NULL)
	{
		return DEFAULT_IP;
	}
	// 取得IP地址列表中的第一个为返回的IP(因为一台主机可能会绑定多个IP)
	const LPSTR lpAddr = lpHostEnt->h_addr_list[0];
	// 将IP地址转化成字符串形式
	struct in_addr inAddr;
	memmove(&inAddr, lpAddr, 4);
	m_strIP = string(inet_ntoa(inAddr));
	return m_strIP;
}

///////////////////////////////////////////////////////////////////
// 获得本机中处理器的数量
int CIocpModel::_GetNumOfProcessors() noexcept
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

/////////////////////////////////////////////////////////////////////
// 判断客户端Socket是否已经断开，否则在一个无效的Socket上投递WSARecv操作会出现异常
// 使用的方法是尝试向这个socket发送数据，判断这个socket调用的返回值
// 因为如果客户端网络异常断开(例如客户端崩溃或者拔掉网线等)的时候，
// 服务器端是无法收到客户端断开的通知的
bool CIocpModel::_IsSocketAlive(SOCKET s) noexcept
{
	const int nByteSent = send(s, "", 0, 0);
	if (SOCKET_ERROR == nByteSent)
	{
		return false;
	}
	else
	{
		return true;
	}
}

///////////////////////////////////////////////////////////////////
//函数功能：显示并处理完成端口上的错误
bool CIocpModel::HandleError(SocketContext* pSoContext, const DWORD& dwErr)
{
	// 如果是超时了，就再继续等吧 0x102=258L
	if (WAIT_TIMEOUT == dwErr)
	{
		// 确认客户端是否还活着...
		if (!_IsSocketAlive(pSoContext->m_Socket))
		{
			this->_ShowMessage("检测到客户端异常退出！");
			this->OnConnectionClosed(pSoContext);
			this->_DoClose(pSoContext);
			return true;
		}
		else
		{
			this->_ShowMessage("网络操作超时！重试中..");
			return true;
		}
	}
	// 可能是客户端异常退出了; 0x40=64L
	else if (ERROR_NETNAME_DELETED == dwErr)
	{// 出这个错，可能是监听SOCKET挂掉了
		//this->_ShowMessage("检测到客户端异常退出！");
		this->OnConnectionError(pSoContext, dwErr);
		if (!this->_DoClose(pSoContext))
		{
			this->_ShowMessage("检测到异常！");
		}
		return true;
	}
	else
	{//ERROR_OPERATION_ABORTED=995L
		this->_ShowMessage("完成端口操作出错，线程退出。err=%d", dwErr);
		this->OnConnectionError(pSoContext, dwErr);
		this->_DoClose(pSoContext);
		return false;
	}
}

/////////////////////////////////////////////////////////////////////
// 在主界面中显示提示信息
void CIocpModel::_ShowMessage(const char* szFormat, ...)
{
	if (m_LogFunc)
	{
		char buff[256] = { 0 };
		va_list arglist;
		// 处理变长参数
		va_start(arglist, szFormat);
		vsnprintf(buff, sizeof(buff), szFormat, arglist);
		va_end(arglist);

		m_LogFunc(string(buff));
	}
}

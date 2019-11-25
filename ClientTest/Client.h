/*==========================================================================
Purpose:
* 这个类CClient是本代码的核心类，用于产生用于指定的并发线程向指定服务器发送
 信息，测试服务器的响应及资源占用率情况，并使用了MFC对话框程序来进行说明
Notes:
* 客户端使用的是最简单的多线程阻塞式Socket，而且每个线程只发送一次数据
 如果需要可以修改成发送多次数据的情况
==========================================================================*/
#pragma once
#include <string>
using std::string;
// 屏蔽deprecation警告
#pragma warning(disable: 4996)
// 缓冲区长度(8*1024字节)
#define MAX_BUFFER_LEN 512//(4*1024)//(8*1024)
#define DEFAULT_PORT 10240 // 默认端口
#define DEFAULT_IP _T("127.0.0.1") // 默认IP地址
#define DEFAULT_THREADS 10000 // 默认并发线程数
#define DEFAULT_TIMES 10000 // 默认发送次数
#define DEFAULT_MESSAGE _T("Hello!") // 默认的发送信息
typedef void (*LOG_FUNC)(const string& strInfo);

class CClient;

// 用于发送数据的线程参数
struct WorkerThreadParam
{
	CClient* pClient; // 类指针，用于调用类中的函数
	SOCKET sock; // 每个线程使用的Socket
	int nThreadNo; // 线程编号
	int nSendTimes; // 发送次数
	char szSendBuffer[MAX_BUFFER_LEN];
	char szRecvBuffer[MAX_BUFFER_LEN];
};

// 产生Socket连接的线程
struct ConnectionThreadParam
{
	CClient* pClient; // 类指针，用于调用类中的函数
};

class CClient
{
public:
	CClient(void);
	~CClient(void);

public:
	// 加载Socket库
	bool LoadSocketLib();
	// 卸载Socket库，彻底完事
	void UnloadSocketLib() { WSACleanup(); }
	// 开始测试
	bool Start();
	//	停止测试
	void Stop();
	// 获得本机的IP地址
	CString GetLocalIP();
	// 设置连接IP地址
	void SetIP(const CString& strIP) { m_strServerIP = strIP; }
	// 设置监听端口
	void SetPort(const int& nPort) { m_nPort = nPort; }
	// 设置并发线程发送次数
	void SetTimes(const int& n) { m_nTimes = n; }
	// 设置并发线程数量
	void SetThreads(const int& n) { m_nThreads = n; }
	// 设置要按发送的信息
	void SetMessage(const CString& strMessage) { m_strMessage = strMessage; }
	// 设置主界面的指针，用于调用其函数
	void SetMainDlg(CDialog* p) { m_pMain = p; }

private:
	// 建立连接
	bool EstablishConnections();
	// 向服务器进行连接
	bool ConnectToServer(SOCKET& pSocket, CString strServer, int nPort);
	// 用于建立连接的线程
	static DWORD WINAPI _ConnectionThread(LPVOID lpParam);
	// 用于发送信息的线程
	static DWORD WINAPI _WorkerThread(LPVOID lpParam);
	// 释放资源
	void CleanUp();

private:
	//LOG_FUNC m_LogFunc;
	CDialog* m_pMain; // 界面指针
	CString m_strServerIP; // 服务器端的IP地址
	CString m_strLocalIP; // 本机IP地址
	CString m_strMessage; // 发给服务器的信息
	int m_nPort; // 监听端口
	int m_nTimes; // 并发线程发送次数
	int m_nThreads; // 并发线程数量
	//HANDLE* m_phWorkerThreads; //所有工作线程
	//DWORD* m_pWorkerThreadIds; //所有工作线程的ID
	WorkerThreadParam* m_pWorkerParams; // 线程参数
	HANDLE m_hConnectionThread; // 接受连接的线程句柄
	HANDLE m_hShutdownEvent; // 通知线程，为了更好的退出线程
	LONG m_nRunningWorkerThreads; // 正在运行的并发线程数量
	//使用线程池，进行操作
	TP_CALLBACK_ENVIRON te;
	PTP_POOL threadPool;
	PTP_CLEANUP_GROUP cleanupGroup;
	PTP_WORK* pWorks;

	static void NTAPI CClient::poolThreadWork(
		_Inout_ PTP_CALLBACK_INSTANCE Instance,
		_Inout_opt_ PVOID Context,
		_Inout_ PTP_WORK Work);

public:
	// 在主界面中显示信息
	void ShowMessage(const char* szFormat, ...);
	//void SetLogFunc(LOG_FUNC fn) { m_LogFunc = fn; }
};

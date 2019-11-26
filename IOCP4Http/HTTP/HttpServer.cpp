#include <WinSock2.h>
#include <Windows.h>
#include "HttpCodec.h"
#include "HttpServer.h"
#include <iostream>
using namespace std;

HttpServer::HttpServer(short listenPort, int maxConnectionCount)
    : IocpServer(listenPort, maxConnectionCount)
{
}

void HttpServer::notifyPackageReceived(ClientContext* pConnClient)
{
    HttpCodec codec(pConnClient->m_inBuf.getBuffer(),
		pConnClient->m_inBuf.getBufferLen());

    int ret = 1;
    while (ret > 0)
    {
        ret = codec.tryDecode();
        if (ret != 0)
        {
            string resMsg = codec.responseMessage();
            send(pConnClient, (PBYTE)resMsg.c_str(), resMsg.length());
            pConnClient->m_inBuf.remove(pConnClient->m_inBuf.getBufferLen());
        }
        if (ret < 0)
        {
            cout << "tryDecode failed" << endl;
            CloseClient(pConnClient);
            releaseClientContext(pConnClient);
        }
    }
}
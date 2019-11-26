#include "Iocp/IocpServer.h"
#include "Http/HttpServer.h"
#include <iostream>
using namespace std;

int main()
{
    {
        //IocpServer server(8000);
        HttpServer server(8000);
        bool ret = server.start();
        if (!ret)
        {
            cout << "start failed" << endl;
            return 0;
        }
        while (1)
        {
            Sleep(1000);
        }
    }
    system("pause");
    return 0;
}

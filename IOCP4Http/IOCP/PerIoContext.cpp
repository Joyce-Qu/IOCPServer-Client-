#include "PerIoContext.h"
#include <iostream>
using namespace std;

IoContext::IoContext(PostType type) : m_PostType(type)
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
}

IoContext::~IoContext()
{
    cout << "IoContext::~IoContext()" << endl;
}

void IoContext::resetBuffer()
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
}

AcceptIoContext::AcceptIoContext(SOCKET acceptSocket)
    : IoContext(PostType::ACCEPT), m_acceptSocket(acceptSocket)
{
    SecureZeroMemory(m_accpetBuf, MAX_BUFFER_LEN);
    m_wsaBuf.buf = (PCHAR)m_accpetBuf;
    m_wsaBuf.len = MAX_BUFFER_LEN;
}

AcceptIoContext::~AcceptIoContext()
{
}

void AcceptIoContext::resetBuffer()
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
    SecureZeroMemory(&m_accpetBuf, MAX_BUFFER_LEN);
}

RecvIoContext::RecvIoContext()
    : IoContext(PostType::RECV)
{
    SecureZeroMemory(&m_recvBuf, MAX_BUFFER_LEN);
    m_wsaBuf.buf = (PCHAR)m_recvBuf;
    m_wsaBuf.len = MAX_BUFFER_LEN;
}

RecvIoContext::~RecvIoContext()
{
}

void RecvIoContext::resetBuffer()
{
    SecureZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
    SecureZeroMemory(&m_recvBuf, MAX_BUFFER_LEN);
}

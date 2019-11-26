#ifndef __BUFFER_H__
#define __BUFFER_H__
#include "BufferSlice.h"
#include <Windows.h>
#include <string>

class Buffer
{
public:
	Buffer();
	virtual ~Buffer();
	operator Slice();
	void clear();
	//相当于consume，跟consume不同，remove每次都要更换内存
	UINT remove(UINT nSize);
	UINT read(PBYTE pData, UINT nSize);
	BOOL write(PBYTE pData, UINT nSize);
	BOOL write(PCHAR pData, UINT nSize);
	BOOL write(const std::string& s);
	BOOL insert(PBYTE pData, UINT nSize);
	BOOL insert(const std::string& s);
	int scan(PBYTE pScan, UINT nPos);
	void copy(Buffer& buf);
	PBYTE getBuffer(UINT nPos = 0);
	UINT getBufferLen(); //数据大小
	void writeFile(const std::string& fileName);	

protected:
	UINT reallocateBuffer(UINT nSize); //重新分配
	UINT deallocateBuffer(UINT nSize); //貌似没用	
	UINT getMemSize(); //占用内存大小

protected:
	PBYTE m_pBegin; //缓冲区头部位置，固定不移动
	PBYTE m_pEnd; //缓冲区尾部位置
	UINT m_nSize; //Buffer占用内存大小
};

#endif // !__BUFFER_H__



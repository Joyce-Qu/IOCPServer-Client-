#include <iostream>
using namespace std;

void noMemoryToAlloc()
{
	cerr << "unable to satisfy request for memory\n";
	abort();
}

int main()
{
	try 
	{ 
		//set过后，新的异常处理函数会被调用到
		set_new_handler(noMemoryToAlloc);
		size_t size = 0x7FFFFFFFFFFFFFFF;
		int* p = new int[size];
		if (p == 0) // 检查 p 是否空指针
		{
			return -1;
		}
	}
	catch (const bad_alloc & e) 
	{ 
		//否则，会调用到这里
		return -1;
	}
}

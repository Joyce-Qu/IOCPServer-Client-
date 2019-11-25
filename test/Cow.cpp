//Cow.cpp
#include "Cow.h"
#include <cstring>
#include <iostream>
using namespace std;

Cow::Cow()
{
	strcpy(name, "none");
	hobby = new char[4];
	strcpy(hobby, "cow");
	weight = 0.0;

}

Cow::Cow(const char* nm, const char* bo, double wt)
{
	strcpy(name, nm);
	hobby = new char[strlen(bo) + 1];
	strcpy(hobby, bo);
	weight = wt;
}

Cow::Cow(const Cow& c)
{
	//delete[] hobby;
	strcpy(name, c.name);
	//hobby = new char[strlen(c.hobby)];
	hobby = new char[strlen(c.hobby) + 1];
	strcpy(hobby, c.hobby);
	weight = c.weight;

}


Cow::~Cow()
{
	delete[] hobby;
}

Cow& Cow::operator=(const Cow& c)
{
	if (this == &c)
		return *this;
	else
	{
		delete[] hobby;
		std::strcpy(name, c.name);
		hobby = new char[strlen(c.hobby) + 1];
		strcpy(hobby, c.hobby);
		weight = c.weight;
		return *this;
	}

	// TODO: 在此处插入 return 语句
}

void Cow::showCow() const
{
	std::cout << "Name: " << name << std::endl;
	std::cout << "Hobby: " << hobby << std::endl;
	std::cout << "Weight: " << weight << std::endl;

}

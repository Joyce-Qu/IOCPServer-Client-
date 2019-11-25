//Cow.h
#ifndef COW_H
#define COW_H
#define _CRT_SECURE_NO_WARNINGS

class Cow
{
	char name[20];
	char* hobby;
	double weight;
public:
	Cow();
	Cow(const char* nm, const char* bo, double wt);
	Cow(const Cow& c);
	~Cow();

	Cow& operator=(const Cow& c);
	void showCow()const;//display all cow data;
};

#endif
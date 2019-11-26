#ifndef __HTTP_MESSAGE_H__
#define __HTTP_MESSAGE_H__
#include "../IOCP/BufferSlice.h"
#include <unordered_map>

struct HttpMessage
{
	std::string m_version;
	//std::string m_body;
	std::unordered_map<std::string, std::string> m_headers;
	std::string getHeaderField(const std::string& strKey);
	void setHeader(std::string key, std::string value);
};

struct HttpRequest : public HttpMessage
{
	enum RequestMethod
	{
		GET,
		POST
	};

	size_t m_nHeaderLength;
	std::string m_method;
	std::string m_url;
	Slice m_body;
};

struct HttpResponse : public HttpMessage
{
	int m_status;
};

#endif // !__HTTP_MESSAGE_H__

#pragma once
#include <string>
#include <vector>
namespace fake {inline std::vector<std::string> httpTrace; inline bool beginOk=true; inline int httpCode=200; inline std::string response="{}";}
namespace freeink {class SecureHttpClient {
 public:
 void setInsecure(){fake::httpTrace.push_back("insecure");}
 bool begin(const std::string& url){fake::httpTrace.push_back("begin:"+url);return fake::beginOk;}
 void addHeader(const std::string& k,const std::string& v){fake::httpTrace.push_back("header:"+k+":"+v);}
 int GET(){fake::httpTrace.push_back("GET");return fake::httpCode;}
 int sendRequest(const char* m,const std::string& body){fake::httpTrace.push_back(std::string(m)+":"+body);return fake::httpCode;}
 std::string getString(){fake::httpTrace.push_back("response:"+fake::response);return fake::response;}
 void end(){fake::httpTrace.push_back("end");}
};}

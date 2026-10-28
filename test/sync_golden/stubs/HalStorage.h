#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace fake {inline size_t fileSize=0; inline int failSeek=-1; inline bool openOk=true; inline size_t readLimit=1024; inline std::vector<std::string> fileTrace;}
class HalFile {
 size_t offset=0;
 public:
 size_t fileSize(){return fake::fileSize;}
 bool seekSet(size_t n){fake::fileTrace.push_back("seek:"+std::to_string(n)); offset=n; return int(n)!=fake::failSeek;}
 size_t read(uint8_t* p,size_t n){n=std::min(n,fake::readLimit); fake::fileTrace.push_back("read:"+std::to_string(n)); for(size_t i=0;i<n;++i)p[i]=uint8_t((offset+i)*17+(offset+i)/256); return n;}
};
struct FakeStorage {bool openFileForRead(const char*,const std::string&,HalFile&){return fake::openOk;}};
inline FakeStorage Storage;

#pragma once
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace oracle {
inline std::vector<std::string> trace;
inline std::map<std::string, std::vector<uint8_t>> files;
inline std::string fault;
inline size_t readCount = 0, writeCount = 0;
inline unsigned hash(const uint8_t* p, size_t n) { unsigned h=2166136261u; while(n--) h=(h^*p++)*16777619u; return h; }
inline void record(const char* fmt, ...) { char b[1024]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof(b),fmt,a); va_end(a); trace.emplace_back(b); }
inline void reset() { trace.clear(); files.clear(); fault.clear(); readCount=writeCount=0; }
}

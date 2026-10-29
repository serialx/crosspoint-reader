#pragma once
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace oracle {
inline std::vector<std::string> trace;
inline std::string fault, manifest, currentVersion="1.2.3";
inline std::vector<uint8_t> image, written;
inline size_t chunk = 4096;
inline int cut = -1, step = 0;
inline bool verified = false, selected = false;
inline std::array<uint8_t, 8192> otadata;
inline void record(const char* fmt, ...) {
  char b[2048]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof(b),fmt,a); va_end(a); trace.emplace_back(b);
}
inline void boundary() {
  if (step++ != cut) return;
  record("power-loss written=%zu verified=%d selected=%d",written.size(),verified,selected);
  for(const auto& line:trace) std::cout << line << '\n';
  std::cout << "slots "; for(size_t off:{size_t(0),size_t(4096)}) { for(size_t i=0;i<32;++i) printf("%02x",otadata[off+i]); std::cout << ' '; } std::cout << std::endl;
  std::exit(0);
}
inline uint32_t hash(const uint8_t* p,size_t n) { uint32_t h=2166136261u; for(size_t i=0;i<n;++i) h=(h^p[i])*16777619u; return h; }
inline void reset() { trace.clear(); fault.clear(); written.clear(); verified=selected=false; step=0; cut=-1; chunk=4096; }
}

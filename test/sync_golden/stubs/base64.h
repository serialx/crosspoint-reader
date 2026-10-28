#pragma once
#include <string>
#include <cstring>
using String=std::string;
namespace base64 {
inline String encode(const uint8_t* p,size_t n) {
 static constexpr char a[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 String s; for(size_t i=0;i<n;i+=3) {uint32_t v=uint32_t(p[i])<<16; if(i+1<n)v|=uint32_t(p[i+1])<<8; if(i+2<n)v|=p[i+2]; s+=a[v>>18]; s+=a[(v>>12)&63]; s+=i+1<n?a[(v>>6)&63]:'='; s+=i+2<n?a[v&63]:'=';} return s;
}
inline String encode(const char* p) {return encode(reinterpret_cast<const uint8_t*>(p),strlen(p));}
}

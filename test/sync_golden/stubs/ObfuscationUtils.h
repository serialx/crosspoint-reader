#pragma once
#include <base64.h>
namespace obfuscation {
inline std::string obfuscateToBase64(const std::string& p) {std::string t=p; const uint8_t mac[]={2,17,34,51,68,85}; for(size_t i=0;i<t.size();++i)t[i]^=mac[i%6]; return base64::encode(reinterpret_cast<const uint8_t*>(t.data()),t.size());}
inline std::string deobfuscateFromBase64(const char* p,bool* ok=nullptr) {std::string out; uint32_t n=0; int bits=0; const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; if(ok)*ok=p&&*p; if(!p)return out; for(;*p && *p!='=';++p){const char* v=strchr(alphabet,*p); if(!v){if(ok)*ok=false; return "";} n=(n<<6)|uint32_t(v-alphabet); bits+=6; if(bits>=8){bits-=8;out+=char((n>>bits)&255);}} const uint8_t mac[]={2,17,34,51,68,85}; for(size_t i=0;i<out.size();++i)out[i]^=mac[i%6]; return out;}
}

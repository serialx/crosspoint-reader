#pragma once
#include <CommonCrypto/CommonDigest.h>
#include <cstring>
#include <string>
class MD5Builder {
 CC_MD5_CTX ctx{}; unsigned char digest[16]{};
 public:
 void begin(){CC_MD5_Init(&ctx);}
 void add(const char* s){CC_MD5_Update(&ctx,s,CC_LONG(strlen(s)));}
 void add(uint8_t* s,size_t n){CC_MD5_Update(&ctx,s,CC_LONG(n));}
 void calculate(){CC_MD5_Final(digest,&ctx);}
 std::string toString(){std::string r; for(auto b:digest){r+="0123456789abcdef"[b>>4];r+="0123456789abcdef"[b&15];}return r;}
};

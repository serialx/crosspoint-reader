#pragma once
#include "WolfsslCrypto.h"
namespace freeink::content {
inline size_t base64Decode(const char* p,size_t n,uint8_t* out,size_t capacity){if(n!=capacity)return 0;memcpy(out,p,n);return n;}
inline bool lcpUnwrapContentKey(WolfsslCrypto&,const uint8_t* priv,const uint8_t*,const uint8_t*,const uint8_t*,const uint8_t*,uint8_t* out){oracle::record("unwrap %u",oracle::hash(priv,32));memcpy(out,priv,32);return true;}
}

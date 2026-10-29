#pragma once
#include "Oracle.h"
struct mbedtls_sha256_context { unsigned hash=0; };
inline void mbedtls_sha256_init(mbedtls_sha256_context* c) { c->hash=0; oracle::record("sha-init"); }
inline void mbedtls_sha256_starts(mbedtls_sha256_context* c,int) { c->hash=2166136261u; }
inline void mbedtls_sha256_update(mbedtls_sha256_context* c,const uint8_t* p,size_t n) { oracle::record("sha-update %zu %u",n,oracle::hash(p,n)); while(n--) c->hash=(c->hash^*p++)*16777619u; }
inline void mbedtls_sha256_finish(mbedtls_sha256_context* c,uint8_t* p) { for(size_t i=0;i<32;++i) p[i]=uint8_t(c->hash>>(8*(i%4))); oracle::record("sha-finish"); }
inline void mbedtls_sha256_free(mbedtls_sha256_context*) { oracle::record("sha-free"); }

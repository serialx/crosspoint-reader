#pragma once
#include "Oracle.h"
namespace freeink::content {
struct WolfsslCrypto {
  bool x25519MakeKey(uint8_t* priv, uint8_t* pub) {
    oracle::record("keygen");
    for (size_t i = 0; i < 32; ++i) {
      priv[i] = uint8_t(i);
      pub[i] = uint8_t(32 + i);
    }
    return oracle::fault != "keygen";
  }
};
}  // namespace freeink::content

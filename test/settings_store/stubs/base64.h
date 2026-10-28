#pragma once
#include <Arduino.h>
#include <mbedtls/base64.h>
namespace base64 {
inline String encode(const uint8_t* p, size_t n) {
  size_t size = 0;
  mbedtls_base64_encode(nullptr, 0, &size, p, n);
  String out(size, '\0');
  mbedtls_base64_encode(reinterpret_cast<uint8_t*>(out.data()), size, &size, p, n);
  out.resize(size);
  return out;
}
}

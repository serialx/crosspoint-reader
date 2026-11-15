#pragma once

#include <Print.h>
#include <xp_ffi.h>

#include <cstddef>
#include <cstdint>

// A short write stops ZipFile streaming even when the ZIP directory lies about size.
class GifLimitedWriter final : public Print {
  Print& output;
  size_t remaining;

 public:
  GifLimitedWriter(Print& output, size_t limit) : output(output), remaining(limit) {}
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    return xp_assets_limited_write(&remaining, data, size, &output, [](void* stream, const uint8_t* bytes, size_t len) {
      return static_cast<Print*>(stream)->write(bytes, len);
    });
  }
};

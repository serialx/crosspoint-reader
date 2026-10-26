#pragma once
#include <Logging.h>
#include <Print.h>
#include <xp_ffi.h>

#include <climits>

class EncryptionManifestProbe : public Print {
 public:
  EncryptionManifestProbe() = default;
  EncryptionManifestProbe(const EncryptionManifestProbe&) = delete;
  EncryptionManifestProbe& operator=(const EncryptionManifestProbe&) = delete;
  ~EncryptionManifestProbe() override { xp_pkg_destroy(parser); }
  bool setup() {
    xp_pkg_destroy(parser);
    parser = nullptr;
    completed = false;
    if (xp_pkg_encryption_create(nullptr, 0, &parser) != 0) {
      LOG_ERR("PKG", "Could not allocate encryption probe");
      return false;
    }
    return true;
  }
  size_t write(uint8_t data) override { return write(&data, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    return size <= INT_MAX && parse(data, size, false) ? size : 0;
  }
  bool finish() { return parse(nullptr, 0, true); }
  bool needsProtectionCheck() const { return aes128; }

 private:
  XpPkg* parser = nullptr;
  bool aes128 = false;
  bool completed = false;
  bool parse(const uint8_t* bytes, size_t size, bool final) {
    if (!parser || completed) return false;
    size_t offset = 0;
    do {
      size_t consumed = 0;
      if (xp_pkg_feed(parser, bytes ? bytes + offset : nullptr, size - offset, final, &consumed) != 0) return false;
      offset += consumed;
      for (;;) {
        XpPkgRecord record{};
        if (xp_pkg_next(parser, &record) != 0 || record.kind == XP_PKG_ERROR) {
          LOG_ERR("PKG", "Encryption manifest parse failed");
          return false;
        }
        if (record.kind == XP_PKG_PROTECTION) aes128 = true;
        if (record.kind == XP_PKG_DONE) {
          completed = true;
          return offset == size;
        }
        if (record.kind == XP_PKG_NEED_INPUT) break;
      }
      if (consumed == 0 && offset < size) return false;
    } while (offset < size);
    return true;
  }
};

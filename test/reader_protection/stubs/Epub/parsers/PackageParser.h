#pragma once
#include <Epub/parsers/PackageDiagnostics.h>
#include <Logging.h>
#include <Print.h>
#include <xp_ffi.h>
class PackageParser final : public Print {
  XpPkgPass* pass = nullptr;

 public:
  PackageParser(uint32_t kind, const char*, size_t, const char*) { assert(kind == XP_PKG_ENCRYPTION); }
  ~PackageParser() { xp_pkg_pass_destroy(pass); }
  bool setup() {
    if (xp_pkg_pass_create(XP_PKG_ENCRYPTION, {nullptr, 0}, 0, false, nullptr, nullptr, nullptr, &pass) != 0) {
      LOG_ERR("PKG", "Could not allocate package pass");
      return false;
    }
    return true;
  }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t length) override {
    const XpPkgDiagnostics d{nullptr, packageDiagnostic};
    XpPkgError error{};
    return xp_pkg_pass_write(pass, {bytes, length}, &d, &error);
  }
  bool finish() {
    const XpPkgDiagnostics d{nullptr, packageDiagnostic};
    XpPkgError error{};
    return xp_pkg_pass_finish(pass, &d, &error);
  }
  bool needsProtectionCheck() const { return xp_pkg_pass_count(pass) != 0; }
};

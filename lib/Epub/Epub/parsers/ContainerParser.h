#pragma once
#include <Print.h>
#include <xp_ffi.h>

#include <string>

class ContainerParser final : public Print {
  size_t remainingSize;
  XpPkg* parser = nullptr;

 public:
  ContainerParser(const ContainerParser&) = delete;
  ContainerParser& operator=(const ContainerParser&) = delete;
  std::string fullPath;

  explicit ContainerParser(const size_t xmlSize) : remainingSize(xmlSize) {}
  ~ContainerParser() override { xp_pkg_destroy(parser); }

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};

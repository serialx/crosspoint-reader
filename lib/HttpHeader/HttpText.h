#pragma once

#include <xp_http_transfer.h>

#include <memory>
#include <string>
#include <utility>

namespace httptransfer {
XpHttpNativeText nativeText(const std::string& value);
XpHttpMutableText mutableText(std::string& value);
extern const XpHttpTextLeaves textLeaves;

template <typename Fill>
std::string textResult(Fill&& fill) {
  XpHttpTextStorageRetireP19 storage;
  XpHttpMutableText output{&storage};
  fill(&output);
  auto* value = static_cast<std::string*>(output.native_string);
  std::string result(std::move(*value));
  std::destroy_at(value);
  return result;
}
}  // namespace httptransfer

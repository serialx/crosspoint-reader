#include "HttpText.h"

#include <new>

namespace httptransfer {
XpHttpNativeText nativeText(const std::string& value) {
  return {&value, {reinterpret_cast<const uint8_t*>(value.data()), value.size()}};
}
XpHttpMutableText mutableText(std::string& value) {
  return {&value, reinterpret_cast<uint8_t*>(value.data()), value.size(), value.capacity()};
}
namespace {
void initializeText(XpHttpTextStorageRetireP19* storage, size_t length, XpHttpMutableText* output) {
  static_assert(sizeof(std::string) <= sizeof(*storage));
  static_assert(alignof(std::string) <= alignof(XpHttpTextStorageRetireP19));
  auto* value = new (storage) std::string(length, '\0');
  *output = mutableText(*value);
}
void resizeText(XpHttpMutableText* value, size_t length) {
  auto& text = *static_cast<std::string*>(value->native_string);
  text.resize(length);
  *value = mutableText(text);
}
void releaseText(XpHttpMutableText* value) {
  std::destroy_at(static_cast<std::string*>(value->native_string));
  *value = {};
}
}  // namespace
const XpHttpTextLeaves textLeaves{initializeText, resizeText, releaseText};
}  // namespace httptransfer

#pragma once
#include_next <ArduinoJson.h>
#ifdef PLUGIN_JSON_FAULTS
#include "JsonFault.h"
namespace plugin_oracle {
class Allocator final : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t n) override { return json_fault::allocate(n); }
  void* reallocate(void* p, size_t n) override { return json_fault::reallocate(p, n); }
  void deallocate(void* p) override { json_fault::deallocate(p); }
};
inline Allocator allocator;
class Document : public ArduinoJson::JsonDocument {
 public:
  Document() : ArduinoJson::JsonDocument(&plugin_oracle::allocator) {}
};
}  // namespace plugin_oracle
#define JsonDocument plugin_oracle::Document
#endif

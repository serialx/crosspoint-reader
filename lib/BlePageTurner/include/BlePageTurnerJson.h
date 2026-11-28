#pragma once

#include <ArduinoJson.h>

#include "BlePageTurner.h"

namespace bleturner {
namespace detail {
// Generic document leaves: Rust owns BLE keys, defaults, limits and filtering.
struct JsonReader {
  void (*root)(void*);
  void (*key)(void*, const char*);
  // Two nested arrays (depth 0/1), borrowed for this read; begin precedes next/item.
  void (*arrayBegin)(void*, uint8_t);
  bool (*arrayNext)(void*, uint8_t);
  void (*arrayItem)(void*, uint8_t);
  bool (*u8Value)(void*, uint8_t*);
  bool (*u32Value)(void*, uint32_t*);
  const char* (*text)(void*);
  bool (*isNull)(void*);
};
struct JsonWriter {
  void (*root)(void*);
  void (*key)(void*, const char*);
  void (*index)(void*, size_t);
  bool (*array)(void*);
  // The stored index, or SIZE_MAX when allocation failed.
  size_t (*appendObject)(void*);
  void (*number)(void*, uint32_t);
  void (*text)(void*, const char*);
  void (*appendNumber)(void*, uint32_t);
};
extern "C" bool xp_ble_read_json(Config*, const JsonReader*, void*);
extern "C" void xp_ble_write_json(const Config*, const JsonWriter*, void*);
struct ReadCursor {
  JsonVariantConst root;
  JsonVariantConst value;
  JsonArrayConstIterator next[2];
  JsonVariantConst item[2];
};
struct WriteCursor {
  JsonDocument& doc;
  JsonVariant value;
};
inline ReadCursor& readCursor(void* ctx) { return *static_cast<ReadCursor*>(ctx); }
inline WriteCursor& writeCursor(void* ctx) { return *static_cast<WriteCursor*>(ctx); }
inline const JsonReader reader{[](void* c) { readCursor(c).value = readCursor(c).root; },
                               [](void* c, const char* key) { readCursor(c).value = readCursor(c).value[key]; },
                               [](void* c, uint8_t depth) {
                                 auto& cursor = readCursor(c);
                                 cursor.next[depth] = cursor.value.as<JsonArrayConst>().begin();
                               },
                               [](void* c, uint8_t depth) {
                                 auto& cursor = readCursor(c);
                                 auto& next = cursor.next[depth];
                                 if (next == JsonArrayConst{}.end()) return false;
                                 cursor.value = cursor.item[depth] = *next;
                                 ++next;
                                 return true;
                               },
                               [](void* c, uint8_t depth) { readCursor(c).value = readCursor(c).item[depth]; },
                               [](void* c, uint8_t* out) {
                                 const auto v = readCursor(c).value;
                                 if (!v.is<uint8_t>()) return false;
                                 *out = v.as<uint8_t>();
                                 return true;
                               },
                               [](void* c, uint32_t* out) {
                                 const auto v = readCursor(c).value;
                                 if (!v.is<uint32_t>()) return false;
                                 *out = v.as<uint32_t>();
                                 return true;
                               },
                               [](void* c) { return readCursor(c).value | ""; },
                               [](void* c) { return readCursor(c).value.isNull(); }};
inline const JsonWriter writer{[](void* c) { writeCursor(c).value = writeCursor(c).doc.as<JsonVariant>(); },
                               [](void* c, const char* key) {
                                 auto& v = writeCursor(c).value;
                                 const JsonString borrowed(key, true);
                                 v = v[borrowed].isNull() ? v[borrowed].to<JsonVariant>()
                                                          : v[borrowed].as<JsonVariant>();
                               },
                               [](void* c, size_t i) {
                                 auto& v = writeCursor(c).value;
                                 v = v[i].as<JsonVariant>();
                               },
                               [](void* c) { return !writeCursor(c).value.to<JsonArray>().isNull(); },
                               [](void* c) {
                                 auto& v = writeCursor(c).value;
                                 const auto array = v.as<JsonArray>();
                                 const auto object = array.add<JsonObject>();
                                 v = object;
                                 return object.isNull() ? SIZE_MAX : array.size() - 1;
                               },
                               [](void* c, uint32_t n) { writeCursor(c).value.set(n); },
                               [](void* c, const char* text) { writeCursor(c).value.set(JsonString(text, true)); },
                               [](void* c, uint32_t n) { writeCursor(c).value.as<JsonArray>().add(n); }};
}  // namespace detail

// The caller owns the document and Config. Borrowed strings keep ArduinoJson's
// existing lifetime contract; the callbacks never retain a Rust-owned pointer.
inline void writeJson(const Config& config, JsonDocument& doc) {
  detail::WriteCursor cursor{doc, doc.as<JsonVariant>()};
  detail::xp_ble_write_json(&config, &detail::writer, &cursor);
}
inline bool readJson(Config& config, JsonVariantConst doc) {
  detail::ReadCursor cursor{doc, doc, {}, {}};
  return detail::xp_ble_read_json(&config, &detail::reader, &cursor);
}
}  // namespace bleturner

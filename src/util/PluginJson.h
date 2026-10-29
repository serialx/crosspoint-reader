#pragma once
#include <ArduinoJson.h>
#include <xp_plugin.h>

// Slots borrow values from the caller's live ArduinoJson document.
struct PluginJson {
  JsonVariantConst slots[4];
  JsonObjectConstIterator cursor;
  size_t cursorIndex = 0;
  uint32_t cursorSlot = UINT32_MAX;
  explicit PluginJson(JsonVariantConst root) : slots{root, {}, {}, {}} {}
  static XpPluginSpan span(const char* p) { return {reinterpret_cast<const uint8_t*>(p), p ? strlen(p) : 0}; }
  XpPluginJson api() {
    static const XpPluginJsonVtable table{
        [](void* p, uint32_t from, uint32_t to, XpPluginSpan key, size_t index) {
          auto& j = *static_cast<PluginJson*>(p);
          if (j.cursorSlot == to) j.cursorSlot = UINT32_MAX;
          const auto* zero = key.data ? static_cast<const uint8_t*>(memchr(key.data, 0, key.len)) : nullptr;
          const auto len = zero ? static_cast<size_t>(zero - key.data) : key.len;
          j.slots[to] =
              key.data ? j.slots[from][JsonString(reinterpret_cast<const char*>(key.data), len)] : j.slots[from][index];
        },
        [](void* p, uint32_t slot) { return span(static_cast<PluginJson*>(p)->slots[slot].as<const char*>()); },
        [](void* p, uint32_t slot) {
          auto v = static_cast<PluginJson*>(p)->slots[slot];
          return v.is<long long>() || v.is<int>();
        },
        [](void* p, uint32_t slot) { return static_cast<PluginJson*>(p)->slots[slot].isNull(); },
        [](void* p, uint32_t from, uint32_t to, size_t index, XpPluginSpan* key) {
          auto& j = *static_cast<PluginJson*>(p);
          const auto obj = j.slots[from].as<JsonObjectConst>();
          if (j.cursorSlot != from || index < j.cursorIndex) {
            j.cursor = obj.begin();
            j.cursorIndex = 0;
            j.cursorSlot = from;
          }
          while (j.cursorIndex < index && j.cursor != obj.end()) {
            ++j.cursor;
            ++j.cursorIndex;
          }
          if (j.cursor == obj.end()) return false;
          *key = span(j.cursor->key().c_str());
          j.slots[to] = j.cursor->value();
          return true;
        },
        [](void*, XpPluginSpan text) {
          const std::string segment(reinterpret_cast<const char*>(text.data), text.len);
          return static_cast<size_t>(atoi(segment.c_str()));
        }};
    return {this, &table};
  }
};

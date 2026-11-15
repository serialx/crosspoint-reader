#include "SyncJson.h"

#include <ObfuscationUtils.h>

#include <cstring>
namespace {
XpSyncValue value(uint32_t kind, int64_t n = 0, double real = 0) { return {kind, n, real, {nullptr, 0}, 1}; }
XpSyncValue string(const char* s) {
  if (!s) s = "";
  return {0, 0, 0, {reinterpret_cast<const uint8_t*>(s), strlen(s)}, 1};
}
XpSyncValue get(void* ctx, uint32_t slot, const char* key, uint32_t mode) {
  auto& j = *static_cast<SyncJson*>(ctx);
  const JsonVariantConst v = j.input[slot][key];
  switch (static_cast<XpSyncJsonRead>(mode)) {
    case XP_SYNC_READ_CSTRING:
      return string(v | "");
    case XP_SYNC_READ_STRING:
      j.text = v.as<std::string>();
      return {0, 0, 0, {reinterpret_cast<const uint8_t*>(j.text.data()), j.text.size()}, 1};
    case XP_SYNC_READ_U8:
      return value(1, v.as<uint8_t>());
    case XP_SYNC_READ_U8_DEFAULT: {
      auto out = value(1, v.as<uint8_t>());
      out.valid = v.is<uint8_t>();
      return out;
    }
    case XP_SYNC_READ_BOOL:
      return value(2, v | false);
    case XP_SYNC_READ_F32:
      return value(3, 0, v.as<float>());
    case XP_SYNC_READ_I64:
      return value(4, v.as<int64_t>());
    case XP_SYNC_READ_U32:
      return value(5, v.as<uint32_t>());
    case XP_SYNC_READ_U16:
      return value(6, v.as<uint16_t>());
    case XP_SYNC_READ_NULL:
      return value(2, v.isNull());
    case XP_SYNC_READ_OBJECT:
      return value(2, v.is<JsonObjectConst>());
    case XP_SYNC_READ_RAW: {
      auto out = value(8, slot);
      out.valid = v.is<const char*>() || v.is<bool>() || v.is<long long>() || v.is<double>();
      return out;
    }
  }
  return value(XP_SYNC_VALUE_STRING);
}
void set(void* ctx, uint32_t slot, const char* key, XpSyncValue v) {
  auto& j = *static_cast<SyncJson*>(ctx);
  // Schema keys are static Rust strings; copied extra keys retain the JSON library lifetime.
  auto target = j.output[slot][JsonString(key, v.kind != XP_SYNC_VALUE_RAW)];
  switch (static_cast<XpSyncValueKind>(v.kind)) {
    case XP_SYNC_VALUE_STRING:
      target = JsonString(reinterpret_cast<const char*>(v.text.data), v.text.len);
      break;
    case XP_SYNC_VALUE_U8:
      target = static_cast<uint8_t>(v.number);
      break;
    case XP_SYNC_VALUE_BOOL:
      target = v.number != 0;
      break;
    case XP_SYNC_VALUE_F32:
      target = static_cast<float>(v.real);
      break;
    case XP_SYNC_VALUE_I64:
      target = v.number;
      break;
    case XP_SYNC_VALUE_U32:
      target = static_cast<uint32_t>(v.number);
      break;
    case XP_SYNC_VALUE_U16:
      target = static_cast<uint16_t>(v.number);
      break;
    case XP_SYNC_VALUE_SECRET:
      target = obfuscation::obfuscateToBase64(std::string(reinterpret_cast<const char*>(v.text.data), v.text.len));
      break;
    case XP_SYNC_VALUE_RAW:
      target = j.input[v.number][key];
      break;
  }
  j.input[slot] = j.output[slot];
}
void select(void* ctx, uint32_t from, const char* key, size_t index, uint32_t to, uint32_t kind) {
  auto& j = *static_cast<SyncJson*>(ctx);
  if (kind) {
    if (key) {
      if (kind == 1)
        j.output[to] = j.output[from][JsonString(key, true)].to<JsonObject>();
      else
        j.output[to] = j.output[from][JsonString(key, true)].to<JsonArray>();
    } else {
      if (kind == 1)
        j.output[to] = j.output[from].as<JsonArray>().add<JsonObject>();
      else
        j.output[to] = j.output[from].as<JsonArray>().add<JsonArray>();
    }
    j.input[to] = j.output[to];
  } else
    j.input[to] = key ? j.input[from][key] : j.input[from][index];
}
size_t count(void* ctx, uint32_t slot) { return static_cast<SyncJson*>(ctx)->input[slot].as<JsonArrayConst>().size(); }
const char* key(void* ctx, uint32_t slot, size_t index) {
  const auto obj = static_cast<SyncJson*>(ctx)->input[slot].as<JsonObjectConst>();
  auto it = obj.begin();
  for (size_t i = 0; i < index && it != obj.end(); ++i) ++it;
  return it == obj.end() ? nullptr : it->key().c_str();
}
}  // namespace
XpSyncJson SyncJson::api() { return {this, get, set, select, count, key}; }

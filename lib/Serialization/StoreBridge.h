#pragma once
#include <ArduinoJson.h>
#include <ObfuscationUtils.h>
#include <xp_settings.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

namespace store_bridge {
inline XpStoreBytes bytes(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}

class JsonInput {
  JsonVariantConst input;

  JsonVariantConst get(const XpStoreJsonPath& path) const {
    JsonVariantConst value = input[path.key];
    if (path.index != SIZE_MAX) value = value[path.index];
    if (path.field) value = value[path.field];
    if (path.subindex != SIZE_MAX) value = value[path.subindex];
    if (path.subfield) value = value[path.subfield];
    return value;
  }

 public:
  explicit JsonInput(JsonVariantConst input) : input(input) {}

  XpStoreJsonOps ops() {
    return {this,
            [](void* ctx, const XpStoreJsonPath* path, uint32_t kind, uint64_t fallback) -> uint64_t {
              const auto value = static_cast<JsonInput*>(ctx)->get(*path);
              switch (kind) {
                case XP_JSONQUERY_NULL:
                  return value.isNull();
                case XP_JSONQUERY_ARRAYSIZE:
                  return value.as<JsonArrayConst>().size();
                case XP_JSONQUERY_U8:
                  return value | static_cast<uint8_t>(fallback);
                case XP_JSONQUERY_U16:
                  return value | static_cast<uint16_t>(fallback);
                case XP_JSONQUERY_U32:
                  return value | static_cast<uint32_t>(fallback);
                case XP_JSONQUERY_USIZE:
                  return value.as<size_t>();
                case XP_JSONQUERY_ISUSIZE:
                  return value.is<size_t>();
                case XP_JSONQUERY_ISU32:
                  return value.is<uint32_t>();
                case XP_JSONQUERY_ISINT:
                  return value.is<int>();
                case XP_JSONQUERY_INT:
                  return static_cast<uint64_t>(value.as<int>());
                case XP_JSONQUERY_ISU8:
                  return value.is<uint8_t>();
                case XP_JSONQUERY_BOOL:
                  return value | (fallback != 0);
                case XP_JSONQUERY_ISSTRING:
                  return value.is<const char*>();
                case XP_JSONQUERY_ISU16:
                  return value.is<uint16_t>();
                default:
                  return 0;
              }
            },
            [](void* ctx, const XpStoreJsonPath* path) -> XpStoreBytes {
              const char* value = static_cast<JsonInput*>(ctx)->get(*path) | "";
              return {reinterpret_cast<const uint8_t*>(value), strlen(value)};
            },
            [](void*, const XpStoreJsonPath*, uint32_t, uint64_t, XpStoreBytes) {}};
  }
};

class Json : public JsonInput {
  JsonDocument* output;
  JsonArray array, subarray;
  JsonVariant row, subrow;
  size_t rowIndex = SIZE_MAX, subrowIndex = SIZE_MAX;

  JsonVariant getOutput(const XpStoreJsonPath& path) {
    JsonVariant value;
    if (path.index == SIZE_MAX) {
      auto root = (*output)[JsonString(path.key, path.static_keys)];
      if (root.isNull()) root.set(nullptr);
      value = root;
    } else {
      // Cache even a failed append: retrying it for each field changes OOM behavior.
      if (rowIndex != path.index) {
        row = array.add<JsonVariant>();
        rowIndex = path.index;
        subrowIndex = SIZE_MAX;
      }
      value = row;
    }
    if (path.field && path.subindex == SIZE_MAX) {
      if (value[JsonString(path.field, true)].isNull()) value[JsonString(path.field, true)].set(nullptr);
      value = value[JsonString(path.field, true)];
    }
    if (path.subindex != SIZE_MAX) {
      if (subrowIndex != path.subindex) {
        subrow = subarray.add<JsonVariant>();
        subrowIndex = path.subindex;
      }
      value = subrow;
    }
    if (path.subfield) {
      if (value[JsonString(path.subfield, true)].isNull()) value[JsonString(path.subfield, true)].set(nullptr);
      value = value[JsonString(path.subfield, true)];
    }
    return value;
  }

 public:
  explicit Json(JsonDocument& output) : JsonInput(output.as<JsonVariantConst>()), output(&output) {}

  XpStoreJsonOps ops() {
    auto result = JsonInput::ops();
    result.put = [](void* ctx, const XpStoreJsonPath* path, uint32_t kind, uint64_t number, XpStoreBytes text) {
      auto& self = *static_cast<Json*>(static_cast<JsonInput*>(ctx));
      JsonVariant value = self.getOutput(*path);
      switch (static_cast<XpStoreJsonWrite>(kind)) {
        case XP_JSONWRITE_BORROWEDSTRING:
          value.set(JsonString(reinterpret_cast<const char*>(text.data), text.len, true));
          break;
        case XP_JSONWRITE_ARRAY:
          if (path->index == SIZE_MAX) {
            self.array = value.to<JsonArray>();
            self.rowIndex = SIZE_MAX;
          } else {
            self.subarray = value.to<JsonArray>();
            self.subrowIndex = SIZE_MAX;
          }
          break;
        case XP_JSONWRITE_NUMBER:
          value.set(number);
          break;
        case XP_JSONWRITE_BOOL:
          value.set(number != 0);
          break;
        case XP_JSONWRITE_STRING:
          value.set(std::string_view(reinterpret_cast<const char*>(text.data), text.len));
          break;
      }
    };
    return result;
  }
};

struct RowStrings {
  std::string& (*lookup)(RowStrings*, size_t, uint32_t);
  std::string& at(size_t row, uint32_t id) { return lookup(this, row, id); }

  XpStoreRows strings() {
    return {this,
            nullptr,
            nullptr,
            [](void* ctx, size_t row, uint32_t id) { return bytes(static_cast<RowStrings*>(ctx)->at(row, id)); },
            [](void* ctx, size_t row, uint32_t id, XpStoreBytes value) {
              static_cast<RowStrings*>(ctx)->at(row, id).assign(reinterpret_cast<const char*>(value.data), value.len);
            },
            [](void* ctx, uint32_t id, XpStoreBytes value, size_t limit) -> uint32_t {
              bool ok = false, tooLong = false;
              static_cast<RowStrings*>(ctx)->at(SIZE_MAX, id) =
                  obfuscation::deobfuscateFromBase64(reinterpret_cast<const char*>(value.data), limit, &ok, &tooLong);
              return static_cast<uint32_t>(ok) | (static_cast<uint32_t>(tooLong) << 1);
            },
            [](void* ctx, size_t row, uint32_t id, const XpStoreJsonOps* json, const XpStoreJsonPath* path,
               bool obfuscated) {
              const auto& value = static_cast<RowStrings*>(ctx)->at(row, id);
              if (obfuscated) {
                const String encoded = obfuscation::obfuscateToBase64(value);
                json->put(json->ctx, path, XP_JSONWRITE_STRING, 0,
                          {reinterpret_cast<const uint8_t*>(encoded.c_str()), encoded.length()});
              } else {
                json->put(json->ctx, path, XP_JSONWRITE_STRING, 0, bytes(value));
              }
            },
            nullptr};
  }
};

// C++ containers remain owned by the public facade. Rust borrows their primitive operations.
template <typename T, XpStoreStoreKind Kind>
struct Rows : RowStrings {
  std::vector<T>& values;
  T* scratch;
  std::string& (*field)(T&, uint32_t);
  std::string* extra = nullptr;

  Rows(std::vector<T>& values, T* scratch, std::string& (*field)(T&, uint32_t), std::string* extra = nullptr)
      : RowStrings{[](RowStrings* ctx, size_t row, uint32_t id) -> std::string& {
          return static_cast<Rows*>(ctx)->string(row, id);
        }},
        values(values),
        scratch(scratch),
        field(field),
        extra(extra) {}

  std::string& string(size_t row, uint32_t id) {
    if (id == 8) return *extra;
    return field(row == SIZE_MAX ? *scratch : values[row], id);
  }

  XpStoreRows ops(void (*diagnostic)(void*, uint32_t, size_t, size_t, size_t)) {
    return {static_cast<RowStrings*>(this),
            [](void* ctx) { return static_cast<Rows*>(static_cast<RowStrings*>(ctx))->values.size(); },
            [](void* ctx, uint32_t op, size_t index) {
              auto& self = *static_cast<Rows*>(static_cast<RowStrings*>(ctx));
              switch (op) {
                case XP_ROWEDIT_CLEAR:
                  self.values.clear();
                  break;
                case XP_ROWEDIT_RESERVE:
                  self.values.reserve(index);
                  break;
                case XP_ROWEDIT_RESETSCRATCH:
                  std::destroy_at(self.scratch);
                  std::construct_at(self.scratch);
                  break;
                case XP_ROWEDIT_APPENDSCRATCH:
                  if constexpr (Kind == XP_STOREKIND_OPDS || Kind == XP_STOREKIND_TOKENS)
                    self.values.push_back(std::move(*self.scratch));
                  else
                    self.values.push_back(*self.scratch);
                  break;
                case XP_ROWEDIT_ERASE:
                  if constexpr (Kind != XP_STOREKIND_OPDS)
                    self.values.erase(self.values.begin() + static_cast<ptrdiff_t>(index));
                  break;
                case XP_ROWEDIT_MOVETOFRONT:
                  if constexpr (Kind == XP_STOREKIND_RECENT)
                    std::rotate(self.values.begin(), self.values.begin() + index, self.values.begin() + index + 1);
                  break;
                case XP_ROWEDIT_INSERTFRONT:
                  if constexpr (Kind == XP_STOREKIND_RECENT)
                    self.values.insert(self.values.begin(), std::move(*self.scratch));
                  break;
                case XP_ROWEDIT_RESIZE:
                  if constexpr (Kind == XP_STOREKIND_RECENT) self.values.resize(index);
                  break;
                case XP_ROWEDIT_APPENDMOVE:
                  if constexpr (Kind == XP_STOREKIND_WIFI || Kind == XP_STOREKIND_TOKENS)
                    self.values.push_back(std::move(*self.scratch));
                  break;
              }
            },
            RowStrings::strings().get,
            RowStrings::strings().set,
            RowStrings::strings().decode,
            RowStrings::strings().emit,
            diagnostic};
  }
};
template <typename T, XpStoreStoreKind Kind>
bool load(uint32_t kind, JsonVariantConst doc, Rows<T, Kind>& rows,
          void (*diagnostic)(void*, uint32_t, size_t, size_t, size_t)) {
  JsonInput json(doc);
  const auto jsonOps = json.ops();
  const auto rowOps = rows.ops(diagnostic);
  return xp_store_rows_load(kind, &jsonOps, &rowOps);
}
template <typename T, XpStoreStoreKind Kind>
void save(uint32_t kind, JsonDocument& doc, Rows<T, Kind>& rows) {
  Json json(doc);
  const auto jsonOps = json.ops();
  const auto rowOps = rows.ops(nullptr);
  xp_store_rows_save(kind, &jsonOps, &rowOps);
}
}  // namespace store_bridge

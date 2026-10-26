#pragma once

#include <cstring>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "Epub/blocks/BlockStyle.h"

namespace ChapterTrace {
inline std::ofstream stream;
inline void open(const std::string& path) {
  stream.open(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open chapter trace: " + path);
}
inline void close() {
  stream.close();
  if (!stream) throw std::runtime_error("Cannot write chapter trace");
}
struct Hex { std::string text; };
inline Hex hex(std::string_view text) {
  static constexpr char DIGITS[] = "0123456789abcdef";
  std::string out;
  out.reserve(text.size() * 2);
  for (const unsigned char byte : text) {
    out += DIGITS[byte >> 4];
    out += DIGITS[byte & 15];
  }
  return {std::move(out)};
}
inline uint32_t bits(float value) {
  uint32_t result;
  memcpy(&result, &value, sizeof(result));
  return result;
}
template <typename T>
void field(const T& value) {
  if constexpr (std::is_enum_v<T> || std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
    stream << static_cast<int>(value);
  else if constexpr (std::is_same_v<T, float>)
    stream << bits(value);
  else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>)
    stream << hex(value).text;
  else if constexpr (std::is_convertible_v<T, const char*>)
    stream << (value ? hex(value).text : "null");
  else if constexpr (std::is_same_v<T, Hex>)
    stream << value.text;
  else
    stream << value;
}
inline void field(const BlockStyle& s) {
  stream << int(s.alignment) << ',' << int(s.characterSpacing) << ',' << s.marginTop << ',' << s.marginBottom << ','
         << s.marginLeft << ',' << s.marginRight << ',' << s.paddingTop << ',' << s.paddingBottom << ','
         << s.paddingLeft << ',' << s.paddingRight << ',' << s.textIndent << ',' << s.textIndentDefined << ','
         << s.textAlignDefined << ',' << s.isRtl << ',' << s.directionDefined << ',' << bits(s.fontScale) << ','
         << s.fontScaleDefined << ',' << s.fontId << ',' << s.pageBreakBefore << ',' << s.pageBreakAfter << ','
         << s.keepWithNext;
}
inline void field(const CssBorderSide& side) { stream << int(side.width) << ',' << int(side.style); }
inline void field(const CssLength& length) { stream << bits(length.value) << ',' << int(length.unit); }
template <typename... Args>
void record(const char* op, const Args&... args) {
  stream << op;
  ((stream << '\t', field(args)), ...);
  stream << '\n';
}
template <typename Fn, typename... Args>
decltype(auto) call(const char* op, Fn&& fn, const Args&... args) {
  record(op, args...);
  if constexpr (std::is_void_v<decltype(fn())>) {
    fn();
  } else {
    decltype(auto) reply = fn();
    record("Reply", reply);
    return reply;
  }
}
}  // namespace ChapterTrace

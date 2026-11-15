#pragma once

#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

// Host-only instrumentation. Each section owns one deterministic trace stream.
namespace LayoutTrace {
inline std::ofstream stream;
inline void open(const std::string& path) {
  stream.open(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open layout trace: " + path);
}
inline void close() {
  stream.close();
  if (!stream) throw std::runtime_error("Cannot write layout trace");
}
inline std::string hex(std::string_view text) {
  static constexpr char DIGITS[] = "0123456789abcdef";
  std::string out;
  out.reserve(text.size() * 2);
  for (unsigned char c : text) {
    out += DIGITS[c >> 4];
    out += DIGITS[c & 15];
  }
  return out;
}
template <typename... Args>
void record(const char* event, const Args&... args) {
  stream << event;
  ((stream << '\t' << args), ...);
  stream << '\n';
}
}  // namespace LayoutTrace

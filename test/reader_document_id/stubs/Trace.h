#pragma once
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace document_test {
inline std::vector<std::string> trace;
inline uint32_t size = 0, offset = 0;
inline int seekFail = -1, readFail = -1, readShort = -1, readZero = -1, sample = -1;
inline bool openFail = false, logs = true;
inline uint8_t byte(uint32_t index) { return uint8_t((uint64_t(index) * 37 + (index >> 9)) & 255); }
inline void event(std::string s) { trace.push_back(std::move(s)); }
inline void log(const char* tag, const char* format, ...) {
  if (!logs) return;
  // Match the production logger's fixed-width prefix and 256-byte record.
  char line[256];
  int prefix = snprintf(line, sizeof(line), "[0] [DBG] [%s] ", tag);
  va_list args;
  va_start(args, format);
  vsnprintf(line + prefix, sizeof(line) - prefix, format, args);
  va_end(args);
  event(line);
}
}  // namespace document_test

#pragma once
#include <Arduino.h>

#include <cstdarg>
inline int esp_rom_printf(const char* format, ...) {
  if (!displayTraceDiagnostics) return 0;
  char line[512];
  va_list args;
  va_start(args, format);
  const int result = std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  std::string text(line);
  if (!text.empty() && text.back() == '\n') text.pop_back();
  trace::event("diagnostic " + text);
  return result;
}

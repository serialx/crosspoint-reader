#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace trace {
void event(const std::string& text);
void finish();
void configure(bool full, int polarity, std::vector<uint8_t> probe);
void busyDuration(uint64_t us);
void step(const std::string& name);
void boardHook(const char* name, bool value);
struct Wait {
  int previousPolarity;
  uint64_t previousStart;
  std::string name;
  Wait(const char* kind, int polarity, const char* tag);
  ~Wait();
};
}  // namespace trace

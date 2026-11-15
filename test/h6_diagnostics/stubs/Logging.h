#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
inline std::set<std::string> h6ReferenceFormats;
inline std::vector<std::string> h6CandidateLogs, h6ReferenceLogs;
template <class... Args>
void h6Log(const char* file, const char* level, const char* origin, const char* format, Args... args) {
  char text[1024];
  if constexpr (sizeof...(args))
    std::snprintf(text, sizeof(text), format, args...);
  else
    std::snprintf(text, sizeof(text), "%s", format);
  const std::string line = std::string(level) + " " + origin + " " + text;
#ifdef H6_SD_LOGS
  // This oracle exercises load-time parsing, not the separately migrated resident pool.
  if (std::strcmp(level, "ERR") == 0 &&
      (std::strstr(format, "Invalid magic") || std::strstr(format, "Unsupported version") ||
       std::strstr(format, "Invalid style") || std::strstr(format, "Duplicate style") ||
       std::strstr(format, "Invalid .cpfont") || std::strstr(format, "unreasonable counts") ||
       std::strstr(format, "invalid interval")))
    std::printf("LOG %s\n", line.c_str());
#else
  if (std::strstr(file, "/reference/")) h6ReferenceFormats.insert(format);
  (std::strstr(file, "/reference/") ? h6ReferenceLogs : h6CandidateLogs).push_back(line);
#endif
}
#define LOG_ERR(tag, ...) h6Log(__FILE__, "ERR", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) h6Log(__FILE__, "INF", tag, __VA_ARGS__)
#define LOG_DBG(tag, ...) h6Log(__FILE__, "DBG", tag, __VA_ARGS__)
struct H6LogCase {
  const std::string& name;
  explicit H6LogCase(const std::string& context) : name(context) {
    h6CandidateLogs.clear();
    h6ReferenceLogs.clear();
  }
  ~H6LogCase() {
    if (h6CandidateLogs == h6ReferenceLogs) return;
    std::fprintf(stderr, "%s: diagnostic mismatch\n", name.c_str());
    for (const auto& s : h6ReferenceLogs) std::fprintf(stderr, "old: %s\n", s.c_str());
    for (const auto& s : h6CandidateLogs) std::fprintf(stderr, "new: %s\n", s.c_str());
    std::exit(1);
  }
};

inline void h6AssertXtcCoverage() {
  for (const char* prefix :
       {"Invalid magic:", "Unsupported version:", "Header:", "Page table offset is 0",
        "Page table exceeds file bounds:", "Failed to seek to page table at", "Failed to read first page table entry",
        "Failed to read page table entry", "Failed to read page header for page", "Invalid page magic for page",
        "Page %u payload size mismatch:"}) {
    bool found = false;
    for (const auto& format : h6ReferenceFormats) found |= format.starts_with(prefix);
    if (!found) {
      std::fprintf(stderr, "Uncovered XTC event: %s\n", prefix);
      std::exit(1);
    }
  }
}

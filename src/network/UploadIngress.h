#pragma once

#include <WString.h>
#include <strings.h>

#include <cstdlib>
#include <new>

namespace upload_ingress {
constexpr size_t MAX_METADATA_BYTES = 8192;
constexpr size_t MAX_ARGUMENTS = 32;

enum class LineKind { Request, Upload, Other };

template <typename Read>
bool readHeaderLine(String& out, Read read, LineKind kind, bool allowNul = false, bool* endedAtEof = nullptr) {
  out.remove(0);
  for (;;) {
    int c = read();
    if (c < 0 && endedAtEof) *endedAtEof = true;
    if (c < 0 || (!allowNul && c == 0) || c == '\n') return false;
    if (c == '\r') return read() == '\n';
    if (kind == LineKind::Request && out.length() == 12) {
      kind = out == "POST /upload" && (c == '?' || c == ' ') ? LineKind::Upload : LineKind::Other;
    }
    if (kind == LineKind::Upload && out.length() == MAX_METADATA_BYTES) return false;
    char byte = static_cast<char>(c);
    if (!out.concat(&byte, 1)) return false;
  }
}

// The upload endpoint accepts quoted form-data parameters in either order.
inline bool parseDisposition(const char* value, String& name, String& filename, bool& isFile) {
  auto skipSpace = [&]() {
    while (*value == ' ' || *value == '\t') ++value;
  };
  skipSpace();
  if (strncasecmp(value, "form-data", 9) != 0) return false;
  value += 9;
  skipSpace();
  bool hasName = false;
  while (*value) {
    if (*value++ != ';') return false;
    skipSpace();
    String* target;
    if (!hasName && strncasecmp(value, "name=\"", 6) == 0) {
      hasName = true;
      target = &name;
      value += 6;
    } else if (!isFile && strncasecmp(value, "filename=\"", 10) == 0) {
      isFile = true;
      target = &filename;
      value += 10;
    } else {
      return false;
    }
    const char* end = strchr(value, '"');
    if (!end || !target->concat(value, end - value)) return false;
    value = end + 1;
    skipSpace();
  }
  return hasName;
}

inline bool decodeArgument(const String& input, unsigned int first, unsigned int end, String& out) {
  if (!out.reserve(end - first)) return false;
  for (unsigned int i = first; i < end;) {
    char c = input[i++];
    if (c == '%' && end - i >= 2) {
      char hex[] = {'0', 'x', input[i], input[i + 1], 0};
      c = static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
    } else if (c == '+') {
      c = ' ';
    }
    if (!out.concat(&c, 1)) return false;
  }
  return true;
}

template <typename RequestArgument>
bool parseUploadArguments(const String& query, RequestArgument*& args, int& count) {
  delete[] args;
  args = nullptr;
  count = 0;
  if (query.length() > MAX_METADATA_BYTES) return false;
  size_t capacity = 1;
  for (unsigned int i = 0; i < query.length(); ++i) {
    if (query[i] == '&') ++capacity;
  }
  if (capacity > MAX_ARGUMENTS) return false;
  args = new (std::nothrow) RequestArgument[capacity];
  if (!args) return false;
  unsigned int first = 0;
  while (first < query.length()) {
    int separator = query.indexOf('&', first);
    unsigned int end = separator < 0 ? query.length() : (unsigned int)separator;
    int equal = query.indexOf('=', first);
    if (equal >= 0 && (unsigned int)equal < end) {
      RequestArgument& arg = args[count];
      if (!decodeArgument(query, first, equal, arg.key) || !decodeArgument(query, equal + 1, end, arg.value))
        return false;
      ++count;
    }
    first = end + 1;
  }
  return true;
}
}  // namespace upload_ingress

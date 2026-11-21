#pragma once

#include <Memory.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace plugincatalog {

// Keep catalog request expansion within the ESP32-C3 internal heap budget.
inline constexpr size_t MAX_REQUEST_BYTES = 16 * 1024;

inline bool needsUrlEscape(unsigned char c) { return c <= 0x20 || c >= 0x7f; }

// Record the raw origin before wire escaping, so a selected rooted list can substitute it again.
struct Origin {
  size_t size = 0;
  unsigned marker = 0;
  bool ended = false;
  void count(char c) {
    if (ended) return;
    if (marker == 3 && (c == '/' || c == '?' || c == '#')) {
      ended = true;
      return;
    }
    ++size;
    if (marker < 3) marker = c == "://"[marker] ? marker + 1 : static_cast<unsigned>(c == ':');
  }
  size_t length() const { return marker == 3 ? size : 0; }
};

class Template {
 public:
  explicit Template(size_t capacity) : stages(capacity ? makeUniqueNoThrow<Stage[]>(capacity) : nullptr) {}
  bool allocated() const { return stages != nullptr; }
  void add(std::string_view key, std::string_view value, bool encode = false, bool config = false) {
    auto& stage = stages[count++];
    stage.key = key;
    stage.value = value;
    stage.encode = encode;
    stage.config = config;
  }
  void removeLast() { --count; }

  // Each stage sees the previous substitution's output, including matches across its boundaries.
  // Matched prefixes borrow the rule key. No intermediate expanded string is stored.
  bool expand(std::string_view prefix, std::string_view input, bool escapeUrl, size_t& remaining,
              std::string* output = nullptr, Origin* origin = nullptr, std::string* rawOrigin = nullptr,
              bool onlyOrigin = false, bool* rooted = nullptr) {
    size_t leading = 0;
    for (size_t i = 0; i < count; ++i) stages[i].reset();
    size_t source = 0;
    size_t next = count;
    while (true) {
      int byte;
      if (next == 0) {
        if (source < prefix.size()) {
          byte = static_cast<unsigned char>(prefix[source++]);
        } else if (source - prefix.size() < input.size()) {
          byte = static_cast<unsigned char>(input[source++ - prefix.size()]);
        } else {
          byte = End;
        }
      } else {
        byte = stages[next - 1].read();
        if (byte == NeedInput) {
          --next;
          continue;
        }
      }
      if (next < count) {
        stages[next++].feed(byte);
        continue;
      }
      if (byte == End) return true;
      if (rooted && leading < 2) {
        if (!leading)
          *rooted = byte == '/';
        else if (byte == '/')
          *rooted = false;
        ++leading;
      }
      if (onlyOrigin) {
        origin->count(byte);
        if (origin->ended) return true;
      } else if (origin) {
        if (!output)
          origin->count(byte);
        else if (rawOrigin && rawOrigin->size() < origin->length())
          rawOrigin->push_back(static_cast<char>(byte));
      }
      const bool escaped = escapeUrl && needsUrlEscape(byte);
      const size_t bytes = escaped ? 3 : 1;
      if (bytes > remaining) return false;
      remaining -= bytes;
      if (output) {
        if (escaped) {
          output->push_back('%');
          output->push_back(HEX_DIGITS[byte >> 4]);
          output->push_back(HEX_DIGITS[byte & 15]);
        } else {
          output->push_back(static_cast<char>(byte));
        }
      }
    }
  }

 private:
  static constexpr int End = -1, NeedInput = -2;
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  struct Stage {
    std::string_view key;
    std::string_view value;
    bool encode, config;
    size_t keySize() const { return key.size() + (config ? 6 : 0); }
    unsigned char keyByte(size_t i) const {
      if (!config) return key[i];
      if (i < 5) return "{cfg."[i];
      return i == key.size() + 5 ? '}' : key[i - 5];
    }
    size_t matched = 0, literal = 0, literalEnd = 0, replacement = 0;
    int pending = NeedInput;
    unsigned escapeDigit = 0;
    void reset() {
      matched = literal = literalEnd = escapeDigit = 0;
      pending = NeedInput;
      replacement = value.size();
    }
    int read() {
      if (literal < literalEnd) return keyByte(literal++);
      if (replacement != value.size()) {
        const unsigned char c = value[replacement];
        if (encode && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '.' || c == '_' || c == '~')) {
          if (++escapeDigit == 1) return '%';
          if (escapeDigit == 2) return HEX_DIGITS[c >> 4];
          escapeDigit = 0;
          ++replacement;
          return HEX_DIGITS[c & 15];
        }
        ++replacement;
        return c;
      }
      return std::exchange(pending, NeedInput);
    }
    void feed(int byte) {
      if (byte != End && byte == keyByte(matched)) {
        if (++matched == keySize()) {
          matched = 0;
          replacement = 0;
        }
        return;
      }
      literal = 0;
      literalEnd = matched;
      pending = byte;
      // Preserve a partial match beginning inside an unusual config key, or at this byte.
      if (config && byte != End && key.find('{') != std::string_view::npos) {
        for (size_t skip = 1; skip < literalEnd; ++skip) {
          const size_t length = literalEnd - skip;
          if (keyByte(skip) != '{' || keyByte(length) != byte) continue;
          size_t i = 1;
          while (i < length && keyByte(i) == keyByte(skip + i)) ++i;
          if (i == length) {
            literalEnd = skip;
            matched = length + 1;
            pending = NeedInput;
            return;
          }
        }
      }
      matched = byte == '{' ? 1 : 0;
      if (matched) pending = NeedInput;
    }
  };
  std::unique_ptr<Stage[]> stages;
  size_t count = 0;
};
}  // namespace plugincatalog

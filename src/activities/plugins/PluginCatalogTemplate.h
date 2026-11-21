#pragma once

#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plugincatalog {

// Keep catalog request expansion within the ESP32-C3 internal heap budget.
inline constexpr size_t MAX_REQUEST_BYTES = 16 * 1024;

inline bool needsUrlEscape(unsigned char c) { return c <= 0x20 || c >= 0x7f; }

// Stop after the raw authority when a rooted details URL needs the browse origin.
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

struct Rule {
  std::string_view key, value;
  bool encode = false, config = false;

  size_t keySize() const { return key.size() + (config ? 6 : 0); }
  char keyByte(size_t i) const {
    if (!config) return key[i];
    if (i < 5) return "{cfg."[i];
    return i == key.size() + 5 ? '}' : key[i - 5];
  }
  bool suffixIsPrefix(size_t start, size_t end) const {
    for (size_t i = start; i < end; ++i)
      if (keyByte(i) != keyByte(i - start)) return false;
    return true;
  }
};

// A stage holds just a matched prefix length and a one-based output cursor.
// A mismatch stays at its producer until the pending prefix has been drained.
template <typename T>
struct Cursor {
  T matched{}, emitting{};
};

class Template {
 public:
  using Config = std::vector<std::pair<std::string, std::string>>;
  Template() = default;

  bool expand(std::string_view prefix, std::string_view input, bool escape, size_t& remaining,
              std::string* output = nullptr, Origin* origin = nullptr, bool* rooted = nullptr) const {
    Sink sink{remaining, output, origin, rooted, escape};
    if (!config || (prefix.find('{') == prefix.npos && input.find('{') == input.npos)) {
      for (auto part : {prefix, input})
        for (unsigned char c : part)
          if (!sink.put(c)) return sink.done;
      return true;
    }
    return stream(prefix, input, sink);
  }

 protected:
  struct Sources {
    std::string_view token, family, version, capabilities, api, page, limit, offset, listId, query;
    std::string_view id, title, author, url;
  };
  Template(const Config& fields, const Sources& values, bool hasItem)
      : config(&fields), values(&values), count(hasItem ? std::size(bindings) : 12) {}

 public:
  void add(std::string_view key, std::string_view value) { extra[extraCount++] = {key, value}; }
  void removeLast() { --extraCount; }

 private:
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  const Config* config = nullptr;
  struct Binding {
    std::string_view key;
    std::string_view Sources::* value;
    bool encode;
  };
  static constexpr Binding bindings[20] = {{"{token}", &Sources::token, false},
                                           {"{firmware_family}", &Sources::family, false},
                                           {"{firmware_version}", &Sources::version, false},
                                           {"{plugin_capabilities}", &Sources::capabilities, false},
                                           {"{plugin_api}", &Sources::api, false},
                                           {"{page}", &Sources::page, false},
                                           {"{limit}", &Sources::limit, false},
                                           {"{offset}", &Sources::offset, false},
                                           {"{list_id}", &Sources::listId, false},
                                           {"{list_id_enc}", &Sources::listId, true},
                                           {"{query}", &Sources::query, true},
                                           {"{query_raw}", &Sources::query, false},
                                           {"{id}", &Sources::id, false},
                                           {"{title}", &Sources::title, false},
                                           {"{author}", &Sources::author, false},
                                           {"{url}", &Sources::url, false},
                                           {"{id_enc}", &Sources::id, true},
                                           {"{title_enc}", &Sources::title, true},
                                           {"{author_enc}", &Sources::author, true},
                                           {"{url_enc}", &Sources::url, true}};
  const Sources* values = nullptr;
  size_t count = 0, extraCount = 0;
  Rule extra[2];

  struct Sink {
    size_t& remaining;
    std::string* output;
    Origin* origin;
    bool* rooted;
    bool escape;
    unsigned leading = 0;
    bool done = false;
    bool put(unsigned char c) {
      if (rooted && leading < 2) {
        if (!leading)
          *rooted = c == '/';
        else if (c == '/')
          *rooted = false;
        ++leading;
      }
      if (origin) {
        origin->count(c);
        if (origin->ended) {
          done = true;
          return false;
        }
      }
      const bool encoded = escape && needsUrlEscape(c);
      const size_t bytes = encoded ? 3 : 1;
      if (bytes > remaining) return false;
      remaining -= bytes;
      if (output) {
        if (encoded) {
          output->push_back('%');
          output->push_back(HEX_DIGITS[c >> 4]);
          output->push_back(HEX_DIGITS[c & 15]);
        } else {
          output->push_back(c);
        }
      }
      return true;
    }
  };

  static bool encodedByte(unsigned char c) {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
             c == '_' || c == '~');
  }

  // Config JSON occupies at most 2048 bytes. Even an unquoted member needs
  // key, colon, value and delimiter. At most eight fields exceed 240 bytes.
  struct Cursors {
    Cursor<uint8_t> small[512]{};
    Cursor<uint16_t> large[8]{};
    Cursor<size_t> builtin[22]{};

    static bool wide(const Rule& rule) { return rule.key.size() > 240 || rule.value.size() > 240; }
    template <typename T>
    static Cursor<size_t> access(Cursor<T>& cursor, const Cursor<size_t>* update) {
      if (update) cursor = {static_cast<T>(update->matched), static_cast<T>(update->emitting)};
      return {cursor.matched, cursor.emitting};
    }
    Cursor<size_t> at(size_t stage, size_t fields, const Rule& rule, const Cursor<size_t>* update = nullptr) {
      if (!rule.config) return access(builtin[stage ? stage - fields : 0], update);
      auto& slot = small[stage - 1];
      if (wide(rule)) return access(large[slot.matched], update);
      return access(slot, update);
    }
  };

  Rule ruleAt(size_t stage, size_t fields) const {
    if (stage && stage <= fields) {
      const auto& pair = (*config)[stage - 1];
      return {pair.first, pair.second, false, true};
    }
    const size_t i = stage ? stage - fields : 0;
    if (i >= count) return extra[i - count];
    const auto& binding = bindings[i];
    return {binding.key, values->*binding.value, binding.encode};
  }

  // This frame exists only during expansion, outside transport and storage calls.
  __attribute__((noinline)) bool stream(std::string_view prefix, std::string_view input, Sink& sink) const {
    Cursors cursors;
    const size_t fields = config->size();
    if (fields > std::size(cursors.small)) return false;
    size_t wideCount = 0;
    for (size_t i = 1; i <= fields; ++i) {
      if (Cursors::wide(ruleAt(i, fields))) {
        if (wideCount == std::size(cursors.large)) return false;
        cursors.small[i - 1].matched = wideCount++;
      }
    }
    size_t source = 0;
    // Producer zero is the input; producer n is the output of rule n-1.
    size_t producer = 0;
    const size_t stages = fields + count + extraCount;
    for (;;) {
      int byte = -1;
      Cursor<size_t> after{};
      Rule emittingRule{};
      if (!producer) {
        if (source < prefix.size())
          byte = static_cast<unsigned char>(prefix[source]);
        else if (source - prefix.size() < input.size())
          byte = static_cast<unsigned char>(input[source - prefix.size()]);
      } else {
        emittingRule = ruleAt(producer - 1, fields);
        after = cursors.at(producer - 1, fields, emittingRule);
        const size_t pos = after.emitting - 1;
        if (after.matched) {
          byte = static_cast<unsigned char>(emittingRule.keyByte(pos));
          if (emittingRule.suffixIsPrefix(pos + 1, after.matched)) {
            after.matched -= pos + 1;
            after.emitting = 0;
          } else {
            ++after.emitting;
          }
        } else {
          const size_t index = emittingRule.encode ? pos / 3 : pos;
          byte = static_cast<unsigned char>(emittingRule.value[index]);
          if (emittingRule.encode) {
            const unsigned digit = pos % 3;
            if (encodedByte(byte)) {
              byte = digit == 0 ? '%' : HEX_DIGITS[digit == 1 ? byte >> 4 : byte & 15];
              ++after.emitting;
            } else {
              after.emitting += 3;
            }
          } else {
            ++after.emitting;
          }
          if ((emittingRule.encode ? (after.emitting - 1) / 3 : after.emitting - 1) == emittingRule.value.size())
            after.emitting = 0;
        }
      }

      bool consumed = true;
      size_t next = producer;
      size_t i = producer;
      for (; i < stages; ++i) {
        const Rule rule = ruleAt(i, fields);
        auto current = cursors.at(i, fields, rule);
        if (byte >= 0 && byte == static_cast<unsigned char>(rule.keyByte(current.matched))) {
          if (++current.matched == rule.keySize()) {
            current.matched = 0;
            if (!rule.value.empty()) current.emitting = 1;
          }
        } else if (current.matched) {
          current.emitting = 1;
          consumed = false;
        } else {
          continue;
        }
        cursors.at(i, fields, rule, &current);
        if (current.emitting) next = i + 1;
        break;
      }
      if (i == stages) {
        if (byte < 0) return true;
        if (!sink.put(byte)) return sink.done;
      }
      if (consumed) {
        if (producer)
          cursors.at(producer - 1, fields, emittingRule, &after);
        else
          ++source;
      }
      producer = next;
      while (producer && !cursors.at(producer - 1, fields, ruleAt(producer - 1, fields)).emitting) --producer;
    }
  }
};
}  // namespace plugincatalog

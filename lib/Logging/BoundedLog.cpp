#include "BoundedLog.h"

#include <climits>
#include <cstring>
#include <type_traits>

namespace loglite {
namespace {
enum class Length { Int, Long, Size };
struct Conversion {
  Length length = Length::Int;
  int width = 0;
  int precision = -1;
  char kind = 0;
  bool left = false;
  bool zero = false;
  bool argumentPrecision = false;
};

int decimal(const char*& cursor) {
  int value = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    const int digit = *cursor++ - '0';
    value = value > (INT_MAX - digit) / 10 ? INT_MAX : value * 10 + digit;
  }
  return value;
}

bool conversion(const char*& cursor, Conversion& c) {
  while (*cursor == '-' || *cursor == '0') {
    if (*cursor++ == '-')
      c.left = true;
    else
      c.zero = true;
  }
  c.width = decimal(cursor);
  if (*cursor == '.') {
    ++cursor;
    c.argumentPrecision = *cursor == '*';
    c.precision = c.argumentPrecision ? (++cursor, 0) : decimal(cursor);
  }
  if (*cursor == 'l') {
    c.length = Length::Long;
    ++cursor;
  } else if (*cursor == 'z') {
    c.length = Length::Size;
    ++cursor;
  }
  c.kind = *cursor;
  if (!*cursor) return false;
  ++cursor;
  switch (c.kind) {
    case 'd':
    case 'u':
    case 'x':
    case 'X':
      return !c.argumentPrecision;
    case 's':
      return c.length == Length::Int && !c.zero && c.width <= 255;
    case 'p':
      return c.length == Length::Int && c.precision == -1;
    case '%':
      return c.length == Length::Int && !c.width && c.precision == -1 && !c.left && !c.zero;
    default:
      return false;  // An open printf grammar: all other conversions use the legacy logger.
  }
}

class Writer {
 public:
  Writer(char* destination, size_t capacity) : destination(destination), capacity(capacity) {}
  size_t remaining() const { return capacity ? capacity - used - 1 : 0; }
  // Keep character writes inside the formatter stack budget.
  __attribute__((always_inline)) void byte(char c) {
    if (remaining()) destination[used++] = c;
  }
  __attribute__((always_inline)) void repeat(char c, size_t count) {
    if (count > remaining()) count = remaining();
    while (count--) destination[used++] = c;
  }
  void text(const char* text, const Conversion& c) {
    if (!text) text = "(null)";
    size_t count = 0;
    const size_t width = static_cast<size_t>(c.width);
    const size_t needed = width > remaining() ? width : remaining();
    const size_t scan =
        c.precision < 0 || static_cast<size_t>(c.precision) > needed ? needed : static_cast<size_t>(c.precision);
    while (count < scan && text[count]) ++count;
    const size_t pad = c.width > 0 && static_cast<size_t>(c.width) > count ? c.width - count : 0;
    if (!c.left) repeat(' ', pad);
    for (size_t i = 0; i < count; ++i) byte(text[i]);
    if (c.left) repeat(' ', pad);
  }
  void number(uintptr_t value, bool negative, const Conversion& c) {
    const unsigned base = c.kind == 'd' || c.kind == 'u' ? 10 : 16;
    size_t digits = value || c.precision != 0 ? 1 : 0;
    uintptr_t divisor = 1;
    for (uintptr_t rest = value; rest >= base; rest /= base) {
      ++digits;
      divisor *= base;
    }
    const size_t prefix = negative ? 1 : c.kind == 'p' ? 2 : 0;
    const size_t zeros = c.precision > 0 && static_cast<size_t>(c.precision) > digits ? c.precision - digits : 0;
    const size_t total = prefix + zeros + digits;
    const size_t padding = c.width > 0 && static_cast<size_t>(c.width) > total ? c.width - total : 0;
    const bool zeroPad = c.zero && !c.left && c.precision < 0;
    if (!c.left && !zeroPad) repeat(' ', padding);
    if (negative)
      byte('-');
    else if (c.kind == 'p') {
      byte('0');
      byte('x');
    }
    if (zeroPad) repeat('0', padding);
    repeat('0', zeros);
    for (size_t i = 0; i < digits && remaining(); ++i) {
      const unsigned digit = value / divisor;
      byte(digit < 10 ? '0' + digit : (c.kind == 'X' ? 'A' : 'a') + digit - 10);
      value %= divisor;
      if (divisor > 1) divisor /= base;
    }
    if (c.left) repeat(' ', padding);
  }
  void finish() {
    if (capacity) destination[used] = '\0';
  }

 private:
  char* destination;
  size_t capacity;
  size_t used = 0;
};

// Keep prefix scratch off the body-formatting continuation.
__attribute__((noinline)) void prefix(Writer& writer, uint32_t milliseconds, const char* level, const char* origin) {
  writer.byte('[');
  Conversion integer;
  integer.kind = 'u';
  writer.number(milliseconds, false, integer);
  writer.text("] [", {});
  writer.text(level, {});
  writer.text("] [", {});
  writer.text(origin, {});
  writer.text("] ", {});
}

// Keep va_list decoding in this frame; va_list is a pointer on C3 and an array on some host ABIs.
void write(Writer& writer, const char* cursor, va_list args) {
  while (*cursor && writer.remaining()) {
    if (*cursor != '%') {
      writer.byte(*cursor++);
      continue;
    }
    ++cursor;
    Conversion c;
    conversion(cursor, c);
    if (c.argumentPrecision) c.precision = va_arg(args, int);
    if (c.kind == 's') {
      writer.text(va_arg(args, const char*), c);
      continue;
    }
    if (c.kind == '%') {
      writer.byte('%');
      continue;
    }
    uintptr_t value = 0;
    bool negative = false;
    if (c.kind == 'p')
      value = reinterpret_cast<uintptr_t>(va_arg(args, void*));
    else if (c.kind == 'd') {
      intptr_t signedValue = 0;
      switch (c.length) {
        case Length::Int:
          signedValue = va_arg(args, int);
          break;
        case Length::Long:
          signedValue = va_arg(args, long);
          break;
        case Length::Size:
          signedValue = va_arg(args, std::make_signed_t<size_t>);
          break;
      }
      negative = signedValue < 0;
      value = static_cast<uintptr_t>(signedValue);
      if (negative) value = uintptr_t{0} - value;
    } else {
      switch (c.length) {
        case Length::Int:
          value = va_arg(args, unsigned);
          break;
        case Length::Long:
          value = va_arg(args, unsigned long);
          break;
        case Length::Size:
          value = va_arg(args, size_t);
          break;
      }
    }
    writer.number(value, negative, c);
  }
}
}  // namespace

bool supports(const char* cursor) noexcept {
  if (!cursor) return false;
  while (*cursor) {
    if (*cursor++ != '%') continue;
    Conversion c;
    if (!conversion(cursor, c)) return false;
  }
  return true;
}

bool format(char* destination, size_t capacity, const char* format, va_list args) noexcept {
  if (!supports(format)) return false;
  Writer writer(destination, capacity);
  write(writer, format, args);
  writer.finish();
  return true;
}
bool formatLine(char (&destination)[LineSize], uint32_t milliseconds, const char* level, const char* origin,
                const char* text, va_list args) noexcept {
  if (!supports(text)) return false;
  Writer writer(destination, LineSize);
  prefix(writer, milliseconds, level, origin);
  write(writer, text, args);
  writer.finish();
  return true;
}

void formatCounter(char (&destination)[LineSize], uint32_t count, const char* event) noexcept {
  Writer writer(destination, LineSize);
  writer.text("[LOG] ", {});
  Conversion integer;
  integer.kind = 'u';
  writer.number(count, false, integer);
  writer.text(event, {});
  writer.finish();
}

void Journal::clearPending() noexcept {
  pending = snapshot = 0;
  dropped = waits = timeouts = 0;
}

void Journal::commit(char (&lines)[LineCount][LineSize], const char* line, size_t length, size_t& head, uint32_t& magic,
                     bool defer) noexcept {
  if (magic != RtcMagic || head >= LineCount) {
    for (auto& entry : lines) entry[0] = '\0';
    head = 0;
    magic = RtcMagic;
    clearPending();
  }
  if (length > LineSize - 1) length = LineSize - 1;
  std::memcpy(lines[head], line, length);
  lines[head][length] = '\0';
  const auto bit = static_cast<uint16_t>(1u << head);
  if ((pending & bit) && dropped != UINT32_MAX) ++dropped;
  snapshot &= static_cast<uint16_t>(~bit);
  if (defer)
    pending |= bit;
  else
    pending &= static_cast<uint16_t>(~bit);
  head = (head + 1) % LineCount;
}
DrainClaim Journal::beginDrain(uintptr_t task) noexcept {
  if (owner) return owner == task ? DrainClaim::Reentrant : DrainClaim::Busy;
  owner = task;
  snapshot = pending;
  return DrainClaim::Acquired;
}

bool Journal::copyNext(char (&lines)[LineCount][LineSize], char (&destination)[LineSize], size_t head) noexcept {
  if (!owner) return false;
  for (size_t i = 0; i < LineCount; ++i) {
    const size_t index = (head + i) % LineCount;
    const auto bit = static_cast<uint16_t>(1u << index);
    if (!(snapshot & pending & bit)) continue;
    std::memcpy(destination, lines[index], LineSize);
    snapshot &= static_cast<uint16_t>(~bit);
    pending &= static_cast<uint16_t>(~bit);
    return true;
  }
  return false;
}

void Journal::endDrain() noexcept {
  snapshot = 0;
  owner = 0;
}

void Journal::takeContention(uint32_t& waitCount, uint32_t& timeoutCount) noexcept {
  waitCount = waits;
  timeoutCount = timeouts;
  waits = timeouts = 0;
}

uint32_t Journal::takeDropped() noexcept {
  const uint32_t result = dropped;
  dropped = 0;
  return result;
}
}  // namespace loglite

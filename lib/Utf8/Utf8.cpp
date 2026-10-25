#include "Utf8.h"

#include <xp_ffi.h>

#include <cstring>

uint32_t utf8NextCodepoint(const unsigned char** string) {
  const unsigned char lead = **string;
  if (lead < 0x80) {
    if (lead != 0) ++*string;
    return lead;
  }
  size_t expected = 1;
  if (lead >= 0xC0 && lead < 0xF8) {
    if (lead < 0xE0) {
      expected = 2;
    } else if (lead < 0xF0) {
      expected = 3;
    } else {
      expected = 4;
    }
  }
  size_t available = 1;
  uint32_t decoded = lead & 0x0F;
  // Callers may supply unterminated characters. Stop at the first invalid continuation.
  while (available < expected) {
    const unsigned char next = (*string)[available++];
    if ((next & 0xC0) != 0x80) break;
    decoded = (decoded << 6) | (next & 0x3F);
  }
  // Reaching 0x0800 requires two valid continuation bytes; exclude UTF-16 surrogates.
  if (expected == 3 && decoded >= 0x0800 && (decoded < 0xD800 || decoded > 0xDFFF)) {
    *string += 3;
    return decoded;
  }
  size_t consumed;
  const auto cp = xp_utf8_next(*string, available, &consumed);
  *string += consumed;
  return cp;
}
char* utf8AppendCodepoint(char* output, const uint32_t cp) {
  return output + xp_utf8_encode(cp, reinterpret_cast<uint8_t*>(output));
}
void utf8AppendCodepoint(const uint32_t cp, std::string& out) {
  char bytes[4];
  const auto* end = utf8AppendCodepoint(bytes, cp);
  out.append(bytes, static_cast<size_t>(end - bytes));
}
size_t utf8RemoveLastChar(std::string& str) {
  const auto size = xp_utf8_remove_last(reinterpret_cast<const uint8_t*>(str.data()), str.size());
  str.resize(size);
  return size;
}
void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) utf8RemoveLastChar(str);
}
int utf8SafeTruncateBuffer(const char* buf, const int len) {
  return len <= 0 ? 0 : static_cast<int>(xp_utf8_safe_truncate(reinterpret_cast<const uint8_t*>(buf), len));
}
std::string utf8ComposeNfc(const std::string& in, bool (*onAbsorbed)(uint32_t, void*), void* context) {
  // Keep the copy constructor's allocation policy for the common unchanged case.
  if (!xp_utf8_may_compose(reinterpret_cast<const uint8_t*>(in.data()), in.size())) return in;
  std::string out;
  out.reserve(in.size());
  struct Sink {
    std::string& output;
    bool (*absorbed)(uint32_t, void*);
    void* context;
  } sink{out, onAbsorbed, context};
  const auto emit = [](void* opaque, const uint8_t* bytes, size_t len) {
    static_cast<Sink*>(opaque)->output.append(reinterpret_cast<const char*>(bytes), len);
  };
  const auto absorbed = [](void* opaque, uint32_t offset) -> uint8_t {
    const auto& state = *static_cast<Sink*>(opaque);
    return !state.absorbed || state.absorbed(offset, state.context);
  };
  if (!xp_utf8_compose(reinterpret_cast<const uint8_t*>(in.data()), in.size(), emit, absorbed, &sink)) return {};
  return out;
}
void utf8ComposeNfcInPlace(char* buffer) {
  const auto len = xp_utf8_compose_in_place(reinterpret_cast<uint8_t*>(buffer), std::strlen(buffer));
  buffer[len] = '\0';
}
uint32_t utf8DecomposedBase(const uint32_t cp) { return xp_text_decomposed_base(cp); }
bool utf8IsHangul(const uint32_t cp) { return xp_text_hangul(cp); }
uint32_t utf8SmallCapsUpper(const uint32_t cp) { return xp_text_upper(cp); }

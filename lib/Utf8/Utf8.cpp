#include "Utf8.h"

#include <xp_ffi.h>

#include <cstring>

uint32_t utf8NextCodepoint(const unsigned char** string) {
  size_t consumed;
  const auto cp = xp_book_utf8_next(*string, &consumed);
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

#pragma once

#include <xp_ffi.h>

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);
// Appends a Unicode codepoint to a std::string in UTF-8 encoding.
void utf8AppendCodepoint(uint32_t cp, std::string& out);
// Remove the last UTF-8 codepoint from a std::string and return the new size.
size_t utf8RemoveLastChar(std::string& str);
// Truncate string by removing N UTF-8 codepoints from the end.
void utf8TruncateChars(std::string& str, size_t numChars);

// Canonical composition (NFC) for the Latin / Vietnamese range and Hangul:
// precomposes a base letter followed by combining diacritical mark(s), and
// conjoining Hangul jamo sequences (L+V[+T]), into single codepoints. Needed
// because the device fonts have no combining-mark positioning and carry only
// precomposed Hangul syllables, so NFD text (some EPUB chapter titles; every
// filename written by macOS) otherwise renders broken or blank.
// Report absorbed input codepoint positions when requested; return empty if the callback fails.
std::string utf8ComposeNfc(const std::string& in, bool (*onAbsorbed)(uint32_t, void*) = nullptr,
                           void* context = nullptr);

// Compose a null-terminated display buffer without allocating or growing it.
// Uncomposed bytes (including malformed UTF-8) are preserved unchanged.
void utf8ComposeNfcInPlace(char* buffer);

// The base letter in the shared Latin/Vietnamese composition table, or zero.
uint32_t utf8DecomposedBase(uint32_t cp);

// Truncate a raw char buffer to the last complete UTF-8 codepoint boundary.
// Returns the new length (<= len). If the buffer ends mid-sequence, the
// incomplete trailing bytes are excluded.
int utf8SafeTruncateBuffer(const char* buf, int len);

// Reject common non-CJK codepoints before crossing FFI for Unicode range checks.
inline bool utf8IsCjkBreakable(const uint32_t cp) { return cp >= 0x1100 && xp_text_cjk(cp); }
bool utf8IsHangul(uint32_t cp);
// Writes 1-4 bytes without a terminator; retains the permissive encoder policy.
char* utf8AppendCodepoint(char* output, uint32_t codepoint);
uint32_t utf8SmallCapsUpper(uint32_t cp);
inline bool utf8IsCombiningMark(const uint32_t cp) { return cp >= 0x0300 && xp_text_combining(cp); }

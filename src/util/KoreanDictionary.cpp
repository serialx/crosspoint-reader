#include "KoreanDictionary.h"

#include <cstdint>

namespace {

constexpr uint32_t HANGUL_FIRST = 0xAC00;
constexpr uint32_t HANGUL_LAST = 0xD7A3;
constexpr uint32_t JONGSEONG_COUNT = 28;
constexpr uint32_t RIEUL_JONGSEONG = 8;

// Codepoint of a three-byte precomposed Hangul syllable, or 0 for anything else.
uint32_t hangulSyllable(const std::string_view bytes) {
  const auto* b = reinterpret_cast<const unsigned char*>(bytes.data());
  if ((b[0] & 0xF0) != 0xE0 || (b[1] & 0xC0) != 0x80 || (b[2] & 0xC0) != 0x80) return 0;
  const uint32_t cp = ((b[0] & 0x0Fu) << 12) | ((b[1] & 0x3Fu) << 6) | (b[2] & 0x3Fu);
  return (cp >= HANGUL_FIRST && cp <= HANGUL_LAST) ? cp : 0;
}

// Stem-final syllables whose past tense may come from a ㅅ-irregular verb
// (지었다 -> 짓다, 나았다 -> 낫다, 부었다 -> 붓다, 이었다 -> 잇다, ...).
bool mayHideSiot(const uint32_t syllable) {
  constexpr uint32_t SYLLABLES[] = {0xC9C0, 0xB098, 0xBD80, 0xC774, 0xC800, 0xADF8};  // 지 나 부 이 저 그
  for (const uint32_t s : SYLLABLES) {
    if (s == syllable) return true;
  }
  return false;
}

}  // namespace

namespace KoreanDictionary {

std::string baseForm(const std::string_view word) {
  constexpr size_t SYLLABLE_BYTES = 3;
  if (word.empty() || word.size() % SYLLABLE_BYTES != 0) return {};
  for (size_t i = 0; i < word.size(); i += SYLLABLE_BYTES) {
    if (hangulSyllable(word.substr(i, SYLLABLE_BYTES)) == 0) return {};
  }

  // Every rule replaces the last two syllables.
  if (word.size() < 2 * SYLLABLE_BYTES) return {};
  const size_t stemSize = word.size() - 2 * SYLLABLE_BYTES;
  const std::string_view ending = word.substr(stemSize);

  std::string_view baseEnding;
  if (ending == "했다") {
    baseEnding = "하다";
  } else if ((ending == "었다" || ending == "았다") && stemSize > 0) {
    const uint32_t last = hangulSyllable(word.substr(stemSize - SYLLABLE_BYTES, SYLLABLE_BYTES));
    if ((last - HANGUL_FIRST) % JONGSEONG_COUNT == RIEUL_JONGSEONG || mayHideSiot(last)) return {};
    baseEnding = "다";
  } else {
    return {};
  }

  std::string base(word.substr(0, stemSize));
  base.append(baseEnding);
  return base;
}

}  // namespace KoreanDictionary

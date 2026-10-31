#include "Hyphenator.h"

#include <Logging.h>
#include <Memory.h>
#include <xp_ffi.h>

int32_t Hyphenator::cachedLanguage = -1;

void Hyphenator::setPreferredLanguage(const std::string& lang) {
  cachedLanguage = xp_hyphen_language(reinterpret_cast<const uint8_t*>(lang.data()), lang.size());
}

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string& word, const bool includeFallback) {
  // A 160-byte word has at most 160 codepoints; 2/2 minima allow 157 breaks.
  // Separator breaks replace boundaries and cannot increase this bound.
  constexpr size_t LOCAL_BREAKS = 157;
  XpBreak local[LOCAL_BREAKS];
  XpBreak* output = local;
  size_t needed = 0;
  const auto* bytes = reinterpret_cast<const uint8_t*>(word.data());
  int32_t status = xp_hyphen_breaks(cachedLanguage, bytes, word.size(), includeFallback, output, LOCAL_BREAKS, &needed);
  std::unique_ptr<XpBreak[]> overflow;
  if (status == 1) {
    // Long fallback words exceed the Liang limit but must preserve every break.
    overflow = makeUniqueNoThrow<XpBreak[]>(needed);
    if (!overflow) {
      LOG_ERR("HYP", "Cannot allocate %zu break bytes", needed * sizeof(XpBreak));
      return {};
    }
    output = overflow.get();
    status = xp_hyphen_breaks(cachedLanguage, bytes, word.size(), includeFallback, output, needed, &needed);
  }
  if (status != 0) {
    LOG_ERR("HYP", "Invalid hyphenation input (%zu bytes)", word.size());
    return {};
  }
  std::vector<BreakInfo> result(needed);
  for (size_t i = 0; i < needed; ++i) {
    result[i] = {output[i].byte_offset, output[i].insert_hyphen != 0};
  }
  return result;
}

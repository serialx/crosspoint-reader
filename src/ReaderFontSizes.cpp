#include "ReaderFontSizes.h"

#include <cstring>
#include <iterator>

std::vector<uint8_t> readerFontPointSizes(const ReaderFontCatalog* registry, const char* sdFamilyName) {
#ifdef XP_SD_FONT_CATALOG
  XpSdCatalogView view{};
  const XpSdCatalogQuery q{XP_SD_SELECTABLE_SIZES, 0, 0, reinterpret_cast<const uint8_t*>(sdFamilyName),
                           sdFamilyName ? strlen(sdFamilyName) : 0};
  XpSdChoices choices{};
  choices.reserve = xp_sd_catalog_query(registry ? registry->raw() : nullptr, &q, &view);
  std::copy(std::begin(view.sizes), std::end(view.sizes), choices.sizes);
  return readerFontPointSizes(choices);
#else
  if (registry && sdFamilyName && sdFamilyName[0] != '\0') {
    if (const auto* family = registry->findFamily(sdFamilyName)) {
      if (family->vector) {
        return {std::begin(VECTOR_READER_POINT_SIZES), std::end(VECTOR_READER_POINT_SIZES)};
      }
      auto sizes = family->availableSizes();
      if (!sizes.empty()) return sizes;
    }
  }
  return {std::begin(BUILTIN_READER_POINT_SIZES), std::end(BUILTIN_READER_POINT_SIZES)};
#endif
}

#ifdef XP_SD_FONT_CATALOG
std::vector<uint8_t> readerFontPointSizes(const XpSdChoices& choices) {
  std::vector<uint8_t> result;
  if (choices.reserve) result.reserve(choices.reserve);
  for (unsigned n = 0; n < 256; ++n)
    if (choices.sizes[n / 32] & (1u << (n % 32))) result.push_back(n);
  return result;
}
#endif

uint8_t snapToNearestPointSize(const uint8_t* sizes, const size_t count, const uint8_t pt) {
  if (!sizes || count == 0) return pt;

  uint8_t best = sizes[0];
  uint8_t bestDelta = best > pt ? best - pt : pt - best;
  for (size_t i = 1; i < count; i++) {
    const uint8_t delta = sizes[i] > pt ? sizes[i] - pt : pt - sizes[i];
    // Strictly-less keeps the smaller size on a tie, since `sizes` is ascending.
    if (delta < bestDelta) {
      best = sizes[i];
      bestDelta = delta;
    }
  }
  return best;
}

#pragma once
#include <cstddef>

// Historical fixture indices include the catalog-off font_sizes slot at 29.
#ifdef XP_SD_FONT_CATALOG
constexpr std::size_t xpTestEpubLeafCount = 167;
constexpr std::size_t xpTestEpubLeafSlot(std::size_t legacy) { return legacy < 29 ? legacy : legacy - 1; }
#else
constexpr std::size_t xpTestEpubLeafCount = 168;
constexpr std::size_t xpTestEpubLeafSlot(std::size_t legacy) { return legacy; }
#endif

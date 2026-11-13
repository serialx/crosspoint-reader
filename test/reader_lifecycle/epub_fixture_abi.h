#pragma once
#include <cstddef>

// Historical fixture indices include the catalog-off font_sizes slot at 27.
#ifdef XP_SD_FONT_CATALOG
constexpr std::size_t xpTestEpubLeafCount = 162;
constexpr std::size_t xpTestEpubLeafSlot(std::size_t legacy) { return legacy < 27 ? legacy : legacy - 1; }
#else
constexpr std::size_t xpTestEpubLeafCount = 163;
constexpr std::size_t xpTestEpubLeafSlot(std::size_t legacy) { return legacy; }
#endif

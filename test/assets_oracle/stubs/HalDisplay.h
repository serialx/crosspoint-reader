#pragma once
#include <Print.h>
class HalDisplay { public: enum class RefreshMode { HALF_REFRESH }; };
struct AssetDisplay { int getDisplayWidth() const { return 792; } int getDisplayHeight() const { return 528; } };
inline AssetDisplay display;

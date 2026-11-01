#pragma once

#include <cstdint>

// Persisted indices: append actions without reordering existing values.
enum class HomeButtonAction : uint8_t {
  Home,
  Ignore,
  NextPage,
  Refresh,
  Footnotes,
  Confirm,
  Sync,
  Bookmark,
  Dictionary,
  ReaderMenu,
  ToggleFrontlight,
  CreateClipping,
  Count
};

#pragma once
namespace BoardConfig {
struct ViewableInsets {
  int top, right, bottom, left;
};
inline constexpr struct {
  ViewableInsets viewableInsets;
  int headerOffsetY = 0;
  int cornerRadius = 0;
} ACTIVE{{0, 0, 0, 0}};
}  // namespace BoardConfig

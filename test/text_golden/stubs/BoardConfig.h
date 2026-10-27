#pragma once
namespace BoardConfig {
struct ViewableInsets {
  int top, right, bottom, left;
};
inline constexpr struct {
  ViewableInsets viewableInsets;
} ACTIVE{{0, 0, 0, 0}};
}  // namespace BoardConfig

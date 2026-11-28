#pragma once
namespace BoardConfig {
struct ViewableInsets {
  int top, right, bottom, left;
};
inline struct {
  ViewableInsets viewableInsets{};
  int headerOffsetY = 0, cornerRadius = 0, bottomCornerRadius = 0, statusBarOffsetY = 0;
} ACTIVE;
}  // namespace BoardConfig

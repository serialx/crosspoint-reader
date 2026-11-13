#pragma once
extern "C" void xp_assets_image_test_observer(void (*)(bool));
namespace imageFixture {
inline void (*clear)() = nullptr;
inline void (*release)() = nullptr;
inline void observe(bool releasing) {
  if (auto callback = releasing ? release : clear) callback();
}
inline void onClear(void (*callback)()) {
  clear = callback;
  xp_assets_image_test_observer(observe);
}
inline void onRelease(void (*callback)()) {
  release = callback;
  xp_assets_image_test_observer(observe);
}
}  // namespace imageFixture

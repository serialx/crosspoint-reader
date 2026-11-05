#pragma once

#include <cstdio>
#include <cstdlib>

// Single-threaded ownership oracle: recursive acquisition would deadlock in firmware.
class RenderLock {
  inline static bool held = false;

 public:
  inline static void (*afterUnlock)() = nullptr;
  RenderLock() {
    require(!held, "recursive RenderLock");
    held = true;
  }
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock() {
    held = false;
    if (afterUnlock) {
      auto callback = afterUnlock;
      afterUnlock = nullptr;
      callback();
    }
  }
  static void require(bool condition, const char* message) {
    if (!condition) {
      std::fprintf(stderr, "%s\n", message);
      std::exit(1);
    }
  }
  static void requireHeld() { require(held, "access without RenderLock"); }
  static void requireUnlocked() { require(!held, "navigation while holding RenderLock"); }
};

inline int& checkedSelection(int& selected) {
  RenderLock::requireHeld();
  return selected;
}

#pragma once

// Host fakes of HalFile must reject what production asserts on.
//
// lib/hal/HalStorage.cpp asserts `impl != nullptr` in close/read/write/seek/
// size/isDirectory/... , so a handle that was never opened (or was moved from)
// resets the firmware on device. A fake that answers those calls politely hides
// that bug from every host test, which is how the web font upload close and the
// PT-VEC vector slot reached the emulator.
//
// `impl == nullptr` covers a default handle and a moved-from handle only: every
// open attempt (failed opens and empty paths included) and every openNextFile
// result installs an impl, and close() keeps it, so those stay legal. The fakes
// check the impl before arguments, injected faults and early returns, and keep
// isOpen()/operator bool() keyed on the separate open state.

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace hal_fake {

// Stands in for HalFile::impl in a fake that can be moved. Holding one of these instead
// of a bool clears impl presence on move and prevents copies. The containing fake must
// still implement moves if it has a destructor and keep its open queries consistent. Assign from bool to match the
// plain-flag spellings the fakes already use.
class Impl {
 public:
  Impl() = default;
  Impl(bool held) : held_(held) {}
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&& other) noexcept : held_(std::exchange(other.held_, false)) {}
  Impl& operator=(Impl&& other) noexcept {
    if (this != &other) held_ = std::exchange(other.held_, false);
    return *this;
  }
  Impl& operator=(bool held) {
    held_ = held;
    return *this;
  }
  void install() { held_ = true; }
  void clear() { held_ = false; }
  operator bool() const { return held_; }

 private:
  bool held_ = false;
};

[[noreturn]] inline void unopenedHandle(const char* operation, const char* path) {
  std::fprintf(stderr, "HalFile fake: %s() on a handle with no impl (never opened or moved from)", operation);
  if (path && *path) std::fprintf(stderr, ", path %s", path);
  std::fprintf(stderr, "; production asserts here\n");
  std::fflush(stderr);
  std::abort();
}

inline void requireImpl(const bool hasImpl, const char* operation, const char* path = "") {
  if (!hasImpl) unopenedHandle(operation, path);
}

}  // namespace hal_fake

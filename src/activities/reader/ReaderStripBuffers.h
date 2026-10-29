#pragma once

#include <Memory.h>
#include <xp_ffi.h>

// Retained for this activity; panel callbacks do not allocate these buffers.
struct ReaderStripBuffers {
  static constexpr size_t COMMAND_BYTES = 16 * 1024;
  static constexpr size_t MAX_ELEMENTS = 512;
  static constexpr int ROWS = 80;
  static constexpr size_t SCRATCH_BYTES = 100 * ROWS;
  std::unique_ptr<uint8_t[]> scratch;
  std::unique_ptr<uint8_t[]> imageWindow;
  std::unique_ptr<uint64_t[]> commands;
  std::unique_ptr<XpPageRowBounds[]> bounds;

  struct Allocate {
    template <typename T>
    auto operator()(size_t count) const {
      return makeUniqueNoThrow<T[]>(count);
    }
  };

  template <typename Allocator = Allocate>
  bool prepare(size_t stride, bool images, Allocator allocate = {}) {
    if (stride != 99 && stride != 100) return false;
    if (!scratch) scratch = allocate.template operator()<uint8_t>(SCRATCH_BYTES);
    if (!commands) commands = allocate.template operator()<uint64_t>(COMMAND_BYTES / sizeof(uint64_t));
    if (!bounds) bounds = allocate.template operator()<XpPageRowBounds>(MAX_ELEMENTS);
    if (images && !imageWindow) imageWindow = allocate.template operator()<uint8_t>(4096);
    if (scratch && commands && bounds && (!images || imageWindow)) return true;
    // Release partial reservations so the existing framebuffer path has its heap back.
    scratch.reset();
    commands.reset();
    bounds.reset();
    imageWindow.reset();
    return false;
  }
};

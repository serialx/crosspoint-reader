#pragma once
#include <xp_display.h>

// Record every controller command and byte, including later old-frame reseeds.
struct StripDisplayReplay {
  std::vector<uint8_t> busBytes;
  uint32_t clock = 0;
  static void emit(void* raw, uint8_t tag, const uint8_t* data = nullptr, size_t size = 0) {
    auto& self = *static_cast<StripDisplayReplay*>(raw);
    self.busBytes.push_back(tag);
    for (unsigned shift = 0; shift < 32; shift += 8) self.busBytes.push_back(size >> shift);
    if (size) self.busBytes.insert(self.busBytes.end(), data, data + size);
  }
  XpDisplayBus bus() {
    XpDisplayBus b{};
    b.abi_version = XP_DISPLAY_ABI_VERSION;
    b.struct_size = sizeof(b);
    b.ctx = this;
    b.cmd = [](void* c, uint8_t v) { emit(c, 0, &v, 1); };
    b.data8 = [](void* c, uint8_t v) { emit(c, 1, &v, 1); };
    b.data_span = [](void* c, const uint8_t* p, uint16_t n) { emit(c, 2, p, n); };
    b.cmd_data = [](void* c, uint8_t v, const uint8_t* p, uint16_t n) {
      emit(c, 0, &v, 1);
      emit(c, 2, p, n);
    };
    b.txn_begin = [](void* c) { emit(c, 3); };
    b.txn_cmd = b.cmd;
    b.txn_data = b.data8;
    b.txn_write = b.data_span;
    b.txn_end = [](void* c) { emit(c, 4); };
    b.reset = [](void* c, uint16_t v) { emit(c, 5, reinterpret_cast<uint8_t*>(&v), 2); };
    b.reset_level = [](void* c, uint8_t v) { emit(c, 6, &v, 1); };
    b.busy_level = [](void*) -> uint8_t { return 0; };
    b.busy_wait = [](void*, uint32_t, uint32_t) {};
    b.refresh_wait = [](void*, uint32_t) {};
    b.delay_ms = [](void* c, uint32_t n) { static_cast<StripDisplayReplay*>(c)->clock += n; };
    b.delay_us = [](void*, uint32_t) {};
    b.clock_ms = [](void* c) { return static_cast<StripDisplayReplay*>(c)->clock++; };
    b.probe = [](void*, uint32_t, uint32_t) -> uint32_t { return 0; };
    return b;
  }

  std::vector<uint8_t> run(uint32_t controller, ReaderStripPainter& painter, uint8_t* scratch, bool failLate) {
    busBytes.clear();
    clock = 0;
    alignas(8) uint8_t state[XP_DISPLAY_STATE_BYTES]{};
    XpDisplayConfig config{XP_DISPLAY_ABI_VERSION,
                           sizeof(config),
                           controller,
                           0,
                           static_cast<uint16_t>(painter.renderer.getDisplayWidth()),
                           static_cast<uint16_t>(painter.renderer.getDisplayHeight()),
                           0,
                           nullptr,
                           0};
    require(xp_display_init(state, sizeof(state), &config) == XP_DISPLAY_OK, "initialize controller");
    auto table = bus();
    XpDisplayRequest request{};
    request.struct_size = sizeof(request);
    request.data = painter.renderer.getFrameBuffer();
    request.data_len = painter.renderer.getDisplayWidthBytes() * painter.renderer.getDisplayHeight();
    require(xp_display_execute(state, &table, &request, nullptr) == XP_DISPLAY_OK, "begin controller");
    struct Context {
      ReaderStripPainter* painter;
      bool failLate;
    } context{&painter, failLate};
    XpDisplaySource source{sizeof(source),
                           &context,
                           scratch,
                           8000,
                           80,
                           config.width,
                           [](void* raw, uint8_t* out, uint16_t y, uint16_t rows) -> uint32_t {
                             auto& c = *static_cast<Context*>(raw);
                             if (c.failLate && c.painter->committedRows == c.painter->renderer.getDisplayHeight())
                               HalFileTest::failWatchedReads = true;
                             return ReaderStripPainter::paint(c.painter, out, y, rows);
                           }};
    request.operation = XP_DISPLAY_OP_DISPLAY;
    require(xp_display_execute_source(state, &table, &request, &source, nullptr) == XP_DISPLAY_OK, "display source");
    request.mode = 2;
    require(xp_display_execute_source(state, &table, &request, &source, nullptr) == XP_DISPLAY_OK, "following FAST");
    xp_display_drop(state);
    HalFileTest::failWatchedReads = false;
    return busBytes;
  }
};

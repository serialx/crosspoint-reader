#include <array>
#include <cassert>
#include <iostream>
#include <sstream>

#include "Trace.h"
// Exercise the exact callback table passed to Rust, including rejected descriptors.
#include "src/driver/RustPanelDriver.cpp"

int main() {
  trace::configure(true, 2, {});
  freeink::EpdBus bus;
  bus.begin({1, 2, 3, 4, 5, 6}, 20000000, freeink::BusyPolarity::ActiveHigh);
  trace::finish();
  freeink::BusContext context{&bus, {}};
  auto table = freeink::busTable(context);
  assert(table.plane != nullptr);
  const uint8_t data[] = {0x01, 0x80, 0x13, 0x79, 0xfe, 0x45};
  auto capture = [](auto operation) {
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    operation();
    trace::finish();
    std::cout.rdbuf(previous);
    return output.str();
  };
  for (uint8_t direction : {XP_DISPLAY_ROWS_FORWARD, XP_DISPLAY_ROWS_REVERSE}) {
    auto expected = capture([&] {
      bus.cmd(0x10);
      auto transaction = bus.beginTxn();
      if (direction == XP_DISPLAY_ROWS_REVERSE) {
        transaction.writeBytes(data + 4, 2);
        transaction.writeBytes(data + 2, 2);
        transaction.writeBytes(data, 2);
      } else {
        transaction.writeBytes(data, 2);
        transaction.writeBytes(data + 2, 2);
        transaction.writeBytes(data + 4, 2);
      }
    });
    auto actual = capture([&] {
      assert(table.plane(table.ctx, 0x10, data, sizeof(data), 2, direction, XP_DISPLAY_BYTES_IDENTITY) ==
             XP_DISPLAY_OK);
    });
    assert(actual == expected);
  }
  const auto rejected = [&](const uint8_t* plane, uint32_t length, uint16_t stride, uint8_t direction,
                            uint8_t transform, uint32_t status) {
    assert(capture([&] {
             assert(table.plane(table.ctx, 0x10, plane, length, stride, direction, transform) == status);
           }).empty());
  };
  rejected(nullptr, 6, 2, 0, 0, XP_DISPLAY_INVALID);
  rejected(data, 0, 2, 0, 0, XP_DISPLAY_INVALID);
  rejected(data, 6, 0, 0, 0, XP_DISPLAY_INVALID);
  rejected(data, 5, 2, 0, 0, XP_DISPLAY_INVALID);
  rejected(data, 6, 7, 0, 0, XP_DISPLAY_INVALID);
  rejected(data, 6, 2, 2, 0, XP_DISPLAY_UNSUPPORTED);
  rejected(data, 6, 2, 255, 0, XP_DISPLAY_UNSUPPORTED);
  rejected(data, 6, 2, 0, 1, XP_DISPLAY_UNSUPPORTED);
  rejected(data, 6, 2, 0, 255, XP_DISPLAY_UNSUPPORTED);

  alignas(XP_DISPLAY_STATE_ALIGN) std::array<uint8_t, XP_DISPLAY_STATE_BYTES> storage{};
  XpDisplayConfig config{XP_DISPLAY_ABI_VERSION, sizeof(config), XP_DISPLAY_UC8253_X3, 0, 16, 3, 0, nullptr, 0};
  assert(xp_display_init(storage.data(), storage.size(), &config) == XP_DISPLAY_OK);
  XpDisplayRequest request{};
  request.struct_size = sizeof(request);
  request.operation = XP_DISPLAY_OP_GRAY_LSB;
  const uint8_t withTail[] = {0x01, 0x80, 0x13, 0x79, 0xfe, 0x45, 0xaa};
  request.data = withTail;
  request.data_len = sizeof(withTail);
  static auto planeCallback = table.plane;
  static unsigned planeCalls = 0;
  table.plane = [](void* ctx, uint8_t command, const uint8_t* plane, uint32_t length, uint16_t stride,
                   uint8_t direction, uint8_t transform) -> uint32_t {
    ++planeCalls;
    assert(length == 6 && stride == 2 && direction == XP_DISPLAY_ROWS_REVERSE &&
           transform == XP_DISPLAY_BYTES_IDENTITY);
    return planeCallback(ctx, command, plane, length, stride, direction, transform);
  };
  const auto expected = capture([&] {
    bus.sendPlaneFlipped(0x10, data, 3, 2);
    bus.cmd(0x11);
  });
  const auto actual =
      capture([&] { assert(xp_display_execute(storage.data(), &table, &request, nullptr) == XP_DISPLAY_OK); });
  assert(actual == expected && planeCalls == 1);
  table.plane = [](void*, uint8_t, const uint8_t*, uint32_t, uint16_t, uint8_t, uint8_t) -> uint32_t {
    return XP_DISPLAY_UNSUPPORTED;
  };
  assert(capture([&] {
           assert(xp_display_execute(storage.data(), &table, &request, nullptr) == XP_DISPLAY_UNSUPPORTED);
         }).empty());
  table.plane = nullptr;
  assert(capture([&] {
           assert(xp_display_execute(storage.data(), &table, &request, nullptr) == XP_DISPLAY_INVALID);
         }).empty());
  // A legacy-sized table must be rejected before reading its missing tail.
  alignas(XpDisplayBus) const uint32_t oldHeader[] = {1, 8};
  assert(capture([&] {
           assert(xp_display_execute(storage.data(), reinterpret_cast<const XpDisplayBus*>(oldHeader), &request,
                                     nullptr) == XP_DISPLAY_INVALID);
         }).empty());
  alignas(XpDisplayBus) const uint32_t shortHeader[] = {XP_DISPLAY_BUS_VERSION, 8};
  assert(capture([&] {
           assert(xp_display_execute(storage.data(), reinterpret_cast<const XpDisplayBus*>(shortHeader), &request,
                                     nullptr) == XP_DISPLAY_INVALID);
         }).empty());
  xp_display_drop(storage.data());
}

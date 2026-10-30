#include <xp_ui_reader.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
struct State {
  std::array<uint32_t, 22> fields{};
  uint32_t progress = 0, calls = 0, clockDepth = 0;
  int lock = 0;
};
static uint32_t read(void* ptr, uint8_t f) {
  auto& s = *static_cast<State*>(ptr);
  ++s.calls;
  return s.fields.at(f);
}
static void write(void* ptr, uint8_t f, uint32_t v) {
  auto& s = *static_cast<State*>(ptr);
  ++s.calls;
  s.fields.at(f) = v;
}
static uint32_t command(void* ptr, uint8_t c, int32_t, int32_t) {
  auto& s = *static_cast<State*>(ptr);
  ++s.calls;
  switch (c) {
    case XP_UI_READER_PROJECTION_COMMAND_PROGRESS:
      return s.progress;
    case XP_UI_READER_PROJECTION_COMMAND_BOOK_SIZE:
    case XP_UI_READER_PROJECTION_COMMAND_SPINE_COUNT:
    case XP_UI_READER_PROJECTION_COMMAND_ESTIMATE:
      return 4;
    case XP_UI_READER_PROJECTION_COMMAND_LOCK:
      assert(s.lock++ == 0);
      break;
    case XP_UI_READER_PROJECTION_COMMAND_UNLOCK:
      assert(--s.lock == 0);
      break;
    case XP_UI_READER_PROJECTION_COMMAND_CLOCK:
      s.clockDepth = s.lock;
      return 1234;
    default:
      break;
  }
  return 0;
}
int main() {
  State s;
  XpUiReaderProjectionServices svc{&s, read, write, command};
  assert(xp_ui_reader_projection(nullptr, 0, 0, 0, 0) == 0);
  for (int missing = 0; missing < 4; ++missing) {
    auto bad = svc;
    if (missing == 0) bad.context = nullptr;
    if (missing == 1) bad.read = nullptr;
    if (missing == 2) bad.write = nullptr;
    if (missing == 3) bad.command = nullptr;
    assert(xp_ui_reader_projection(&bad, 0, 0, 0, 0) == 0);
    assert(s.calls == 0);
  }
  assert(xp_ui_reader_projection(&svc, 255, 0, 0, 0) == 0 && s.calls == 0);
  assert(xp_ui_reader_projection(&svc, XP_UI_READER_PROJECTION_OPERATION_BASIS_POINTS, 0, 0, 0) == UINT32_MAX);
  assert(s.lock == 0);
  s.fields[XP_UI_READER_PROJECTION_FIELD_HAS_BOOK] = 1;
  s.fields[XP_UI_READER_PROJECTION_FIELD_HAS_SECTION] = 1;
  s.fields[XP_UI_READER_PROJECTION_FIELD_PAGE_COUNT] = 4;
  struct Edge {
    uint32_t bits, percent, screenshot, basis;
  };
  for (const Edge edge : {
           Edge{0x7fc12345u, INT32_MAX, 100, 10000},
           Edge{0xffc12345u, static_cast<uint32_t>(INT32_MIN), 0, 0},
           Edge{0x7f800000u, 100, 100, 10000},
           Edge{0xff800000u, 0, 0, 0},
           Edge{0x7f7fffffu, 100, 100, 10000},
           Edge{0xff7fffffu, 0, 0, 0},
       }) {
    s.progress = edge.bits;
    assert(xp_ui_reader_projection(&svc, XP_UI_READER_PROJECTION_OPERATION_BOOK_PERCENT, 0, 4, 0) == edge.percent);
    xp_ui_reader_projection(&svc, XP_UI_READER_PROJECTION_OPERATION_SCREENSHOT, 0, 0, 0);
    assert(s.fields[XP_UI_READER_PROJECTION_FIELD_OUTPUT_PERCENT] == edge.screenshot);
    assert(xp_ui_reader_projection(&svc, XP_UI_READER_PROJECTION_OPERATION_BASIS_POINTS, 0, 0, 0) == edge.basis);
  }
  for (uint8_t kind = 0; kind < 5; ++kind)
    for (uint8_t flags = 0; flags < 4; ++flags) {
      s.clockDepth = 99;
      const auto result = xp_ui_reader_projection(&svc, XP_UI_READER_PROJECTION_OPERATION_APPLY_MOVE, kind, 3, flags);
      assert(s.lock == 0);
      assert(result == (kind >= 1 && kind <= 3));
      if (result && (flags & 2)) assert(s.clockDepth == (kind == 2));
    }
}

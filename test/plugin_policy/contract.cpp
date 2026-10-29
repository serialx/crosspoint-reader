#include <xp_plugin.h>

#include <cassert>
#include <cstring>
#include <initializer_list>

#include "util/PluginEvents.h"

namespace {
constexpr const char* NAMES[] = {"reader.open", "reader.exit", "reader.session", "book.downloaded", "sleep.enter"};
constexpr pluginevents::Event EVENTS[] = {pluginevents::Event::ReaderOpen, pluginevents::Event::ReaderExit,
                                          pluginevents::Event::ReaderSession, pluginevents::Event::BookDownloaded,
                                          pluginevents::Event::SleepEnter};
XpPluginSpan span(const char* s) { return {reinterpret_cast<const uint8_t*>(s), strlen(s)}; }
struct Append {
  uint64_t size;
  bool removed = false;
  uint64_t rollback = 0;
};
struct Emit {
  size_t bytes;
  bool appended = false, oversized = false;
};
}  // namespace

int main() {
  static_assert(static_cast<unsigned>(pluginevents::Event::COUNT) == 5);
  static_assert(XP_PLUGIN_MAX_OUTBOX_BYTES == 4096 && XP_PLUGIN_MAX_EVENT_LINE == 512);
  static_assert(XP_PLUGIN_MAX_OUTBOX_BYTES + XP_PLUGIN_MAX_EVENT_LINE == 4608);
  static_assert(XP_PLUGIN_MAX_EVENT_PLUGINS == 8 && XP_PLUGIN_JSON_SLOTS == 4);
  for (unsigned i = 0; i < 5; ++i) {
    assert(static_cast<unsigned>(EVENTS[i]) == i);
    assert(xp_plugin_event(span(NAMES[i])) == static_cast<int>(i));
    assert(strcmp(xp_plugin_event_name(i), NAMES[i]) == 0);
    assert(xp_plugin_event_mask(i) == (1u << i));
  }
  assert(xp_plugin_event(span("unknown")) == -1);
  assert(strcmp(xp_plugin_event_name(5), "") == 0);
  XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS]{};
  const XpPluginSubscriptionOps subscriptions{
      nullptr,
      [](void*) -> size_t { return 1; },
      [](void*, size_t) { return true; },
      [](void*, size_t) { return span("demo"); },
      [](void*, size_t) { return span("/plugins/demo"); },
      [](void*, size_t) { return true; },
      [](void*, size_t i) { return i < 5 ? span(NAMES[i]) : XpPluginSpan{nullptr, 0}; },
      [](void*, size_t) { return false; },
      [](void*) {},
      nullptr};
  xp_plugin_subscriptions(table, &subscriptions);
  assert(table[0].mask == 31);
  assert(table[0].connect_mask == xp_plugin_event_mask(static_cast<unsigned>(pluginevents::Event::SleepEnter)));
  for (unsigned i = 0; i < 5; ++i) assert(xp_plugin_any(table, i));
  for (size_t size : {size_t{512}, size_t{513}}) {
    Emit state{size};
    XpPluginSequence sequence{};
    const XpPluginEmitOps emit{
        &state,
        [](void*) -> uint32_t { return 0; },
        [](void*) -> int64_t { return 0; },
        [](void*, const char*, const char*, bool) {},
        [](void*, const char*, int64_t) {},
        [](void*, const char*) {},
        [](void* p) { return static_cast<Emit*>(p)->bytes; },
        [](void* p, const XpPluginSubscriber*, size_t) { static_cast<Emit*>(p)->appended = true; },
        [](void* p, size_t) { static_cast<Emit*>(p)->oversized = true; }};
    xp_plugin_emit(table, &sequence, 0, nullptr, 0, &emit);
    assert(state.appended == (size == 512));
    assert(state.oversized == (size == 513));
  }
  // Exact cap remains writable; cap+1 resets. Rollback must retain all 64 bits.
  for (uint64_t size : {uint64_t{4096}, uint64_t{4097}, (uint64_t{1} << 32) + 4096}) {
    Append state{size};
    const XpPluginOps ops{&state, [](void* p, uint32_t op, uint64_t argument) -> uint64_t {
                            auto& s = *static_cast<Append*>(p);
                            switch (op) {
                              case XP_PLUGIN_FILE_OPEN:
                                return 1;
                              case XP_PLUGIN_FILE_SIZE:
                                return s.size;
                              case XP_PLUGIN_FILE_REMOVE:
                                s.removed = true;
                                s.size = 0;
                                break;
                              case XP_PLUGIN_FILE_TRUNCATE:
                                s.rollback = argument;
                                break;
                            }
                            return 0;
                          }};
    xp_plugin_append(&ops, XP_PLUGIN_MAX_EVENT_LINE);
    assert(state.removed == (size == 4097));
    assert(state.rollback == (state.removed ? 0 : size));
  }
}

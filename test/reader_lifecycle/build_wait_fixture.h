#pragma once

#include <xp_ui_reader.h>

// Callbacks borrow their captures for this synchronous wait; no storage escapes.
template <typename Ready, typename Now, typename Sleep>
bool waitForBuildRoom(Ready ready, Now now, Sleep sleep, const uint32_t waitMs, const uint32_t stepMs) {
  struct Calls {
    Ready& ready;
    Now& now;
    Sleep& sleep;
  } calls{ready, now, sleep};
  const XpUiReaderBuildWait services{&calls, [](void* ctx) -> uint8_t { return static_cast<Calls*>(ctx)->ready(); },
                                     [](void* ctx) -> uint32_t { return static_cast<Calls*>(ctx)->now(); },
                                     [](void* ctx, uint32_t ms) { static_cast<Calls*>(ctx)->sleep(ms); }};
  return xp_ui_reader_wait_build(&services, waitMs, stepMs);
}

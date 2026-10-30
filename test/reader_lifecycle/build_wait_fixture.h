#pragma once

#include <xp_ui_reader.h>

#include <cassert>

// Supply radio and clock leaves to the production Room operation.
template <typename Status, typename Now, typename Sleep>
bool waitForBuildRoom(Status status, Now now, Sleep sleep) {
  struct Calls {
    Status& status;
    Now& now;
    Sleep& sleep;
  } calls{status, now, sleep};
  const XpUiReaderBuildServices services{&calls,
                                         [](void*, [[maybe_unused]] uint8_t field) -> uint32_t {
                                           assert(field == XP_UI_READER_BUILD_FIELD_RADIO_RELEASED);
                                           return 1;  // Radio release is covered by the complete build-policy oracle.
                                         },
                                         [](void*, uint8_t, uint32_t) { assert(false); },
                                         [](void* ctx, uint8_t command, int32_t a, int32_t) -> uint32_t {
                                           auto& c = *static_cast<Calls*>(ctx);
                                           switch (command) {
                                             case XP_UI_READER_BUILD_COMMAND_RADIO_STATUS:
                                               return c.status();
                                             case XP_UI_READER_BUILD_COMMAND_CLOCK:
                                               return c.now();
                                             case XP_UI_READER_BUILD_COMMAND_SLEEP:
                                               c.sleep(static_cast<uint32_t>(a));
                                               break;
                                             case XP_UI_READER_BUILD_COMMAND_RELEASE_STRIPS:
                                             case XP_UI_READER_BUILD_COMMAND_LOG_ROOM_DENIED:
                                               break;
                                             default:
                                               assert(false);
                                           }
                                           return 0;
                                         }};
  return xp_ui_reader_build_policy(&services, XP_UI_READER_BUILD_OPERATION_ROOM);
}

inline bool buildRoomForRadio(bool running, bool stopping, bool starting) {
  uint32_t now = 0;
  return waitForBuildRoom([=] { return uint32_t(running) | (uint32_t(stopping) << 1) | (uint32_t(starting) << 2); },
                          [&] { return now; }, [&](uint32_t ms) { now += ms; });
}

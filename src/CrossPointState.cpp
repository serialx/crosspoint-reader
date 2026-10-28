#include "CrossPointState.h"

#include <StoreBridge.h>

namespace {
XpStoreState view(CrossPointState& state) {
  return {&state.openEpubPath,
          [](void* ctx) { return store_bridge::bytes(*static_cast<std::string*>(ctx)); },
          [](void* ctx, XpStoreBytes value) {
            static_cast<std::string*>(ctx)->assign(reinterpret_cast<const char*>(value.data), value.len);
          },
          state.recentSleepImages,
          &state.recentSleepPos,
          &state.recentSleepFill,
          state.recentOverlaySleepImages,
          &state.recentOverlaySleepPos,
          &state.recentOverlaySleepFill,
          &state.readerActivityLoadCount,
          &state.lastSleepFromReader,
          &state.showBootScreen};
}
}  // namespace
bool CrossPointState::isRecentSleep(uint16_t idx, uint8_t checkCount) const {
  return xp_store_recent_contains(recentSleepImages, recentSleepPos, recentSleepFill, idx, checkCount);
}
bool CrossPointState::isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const {
  return xp_store_recent_contains(recentOverlaySleepImages, recentOverlaySleepPos, recentOverlaySleepFill, idx,
                                  checkCount);
}
void CrossPointState::pushRecentSleep(uint16_t idx) {
  xp_store_recent_push(recentSleepImages, &recentSleepPos, &recentSleepFill, idx);
}
void CrossPointState::pushRecentOverlaySleep(uint16_t idx) {
  xp_store_recent_push(recentOverlaySleepImages, &recentOverlaySleepPos, &recentOverlaySleepFill, idx);
}
void CrossPointState::toJson(JsonDocument& doc) const {
  auto state = view(const_cast<CrossPointState&>(*this));
  store_bridge::Json json(doc);
  const auto ops = json.ops();
  xp_store_state_save(&state, &ops);
}
bool CrossPointState::fromJson(JsonVariantConst doc) {
  auto state = view(*this);
  store_bridge::JsonInput json(doc);
  const auto ops = json.ops();
  xp_store_state_load(&state, &ops);
  return true;
}

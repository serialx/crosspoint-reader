#include "CrossPointState.h"

#include <StoreBridge.h>

namespace {
XpStoreState view(CrossPointState& state) {
  return {&state.openEpubPath,
          [](void* ctx) { return store_bridge::bytes(*static_cast<std::string*>(ctx)); },
          [](void* ctx, XpStoreBytes value) {
            static_cast<std::string*>(ctx)->assign(reinterpret_cast<const char*>(value.data), value.len);
          },
          &state.sleepCycle.count,
          &state.sleepCycle.state,
          &state.overlayCycle.count,
          &state.overlayCycle.state,
          &state.readerActivityLoadCount,
          &state.lastSleepFromReader,
          &state.showBootScreen};
}
}  // namespace
XpStoreState CrossPointState::readerView() { return view(*this); }
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

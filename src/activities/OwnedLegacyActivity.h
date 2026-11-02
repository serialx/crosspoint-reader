#pragma once

#include <Memory.h>

#include "Activity.h"

struct OwnedLegacyActivity {
  std::unique_ptr<Activity> activity;
  uint8_t kind = UINT8_MAX;
  bool reader = false;
  bool identified = false;

  explicit OwnedLegacyActivity(std::unique_ptr<Activity> activity) : activity(std::move(activity)) {}
};

inline std::unique_ptr<OwnedLegacyActivity> ownLegacy(std::unique_ptr<Activity> activity) {
  if (!activity) return nullptr;
  auto owner = makeUniqueNoThrow<OwnedLegacyActivity>(std::move(activity));
  if (!owner) LOG_ERR("ACT", "Cannot allocate legacy activity owner");
  return owner;
}

template <typename T, typename... Args>
std::unique_ptr<OwnedLegacyActivity> makeLegacyActivity(Args&&... args) {
  // A failed wrapper must leave constructor arguments, including borrowed Pages, untouched.
  auto owner = makeUniqueNoThrow<OwnedLegacyActivity>(nullptr);
  if (!owner) return nullptr;
  owner->activity = makeUniqueNoThrow<T>(std::forward<Args>(args)...);
  return owner->activity ? std::move(owner) : nullptr;
}

inline Activity* nativeActivity(const void* object) {
  return static_cast<const OwnedLegacyActivity*>(object)->activity.get();
}

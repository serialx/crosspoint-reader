#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

#include "SleepImageCycle.h"

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  std::string openEpubPath;
  SleepImageCycle sleepCycle;
  SleepImageCycle overlayCycle;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  XpStoreState readerView();
  bool fromJson(JsonVariantConst doc);
};

#define APP_STATE CrossPointState::getInstance()

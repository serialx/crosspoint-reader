#pragma once
#include <span>
#include <vector>

#include "CrossPointSettings.h"
namespace store_test {
inline bool hasHome = false;
}
struct BoardConfig {
  static bool hasHomeKey() { return store_test::hasHome; }
};
enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };
struct SettingInfo {
  const char* key;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;
  bool obfuscated = false;
  SettingType type = SettingType::TOGGLE;
  struct {
    uint8_t min, max;
  } valueRange{};
  size_t count = 0;
  std::vector<uint8_t> enumLabels() const { return std::vector<uint8_t>(count); }
};
// Fixed descriptors exercise each persistence metadata shape independently of UI construction.
template <typename F>
void forEachBaseSetting(F& visit) {
  SettingInfo entries[] = {
      {"sleepScreen", &CrossPointSettings::sleepScreen, 0, 0, false, SettingType::ENUM, {}, 9},
      {"wordSpacing", &CrossPointSettings::wordSpacing, 0, 0, false, SettingType::VALUE, {50, 200}},
      {"pluginsEnabled", &CrossPointSettings::pluginsEnabled},
      {"extraParagraphSpacing", &CrossPointSettings::extraParagraphSpacing},
      {"touchReaderControls", &CrossPointSettings::touchReaderControls},
      {"pageTurnGesture", &CrossPointSettings::pageTurnGesture, 0, 0, false, SettingType::ENUM, {}, 5},
      {"previousPageGesture", &CrossPointSettings::previousPageGesture, 0, 0, false, SettingType::ENUM, {}, 5},
      {"dynamic", nullptr},
      {"opdsDownloadFolder", nullptr,
       static_cast<size_t>(reinterpret_cast<char*>(CrossPointSettings::getInstance().opdsDownloadFolder) -
                           reinterpret_cast<char*>(&CrossPointSettings::getInstance())),
       64, false, SettingType::STRING},
  };
  for (const auto& entry : entries) visit(entry);
}

#include "CrossPointSettings.h"

#include <BlePageTurnerJson.h>
#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <StoreBridge.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "I18nKeys.h"
#include "ReaderFontSizes.h"
#include "SettingsList.h"
#include "fontIds.h"
#include "util/ParagraphIndentMigration.h"
#include "util/PluginLocations.h"

namespace {
XpStoreSettings view(CrossPointSettings& s) {
  return {{&s.frontButtonBack, &s.frontButtonConfirm, &s.frontButtonLeft, &s.frontButtonRight},
          &s.fontFamily,
          &s.fontPointSize,
          &s.longPressMenuFunction,
          &s.shortPwrBtn,
          &s.language,
          &s.keyboardLayouts,
          &s.pluginsEnabled,
          &s.paragraphIndentSpaces,
          &s.extraParagraphSpacing,
          &s.touchReaderControls,
          &s.pageTurnGesture,
          &s.previousPageGesture,
          &s.sleepTimeoutMinutes,
          &s.homeButtonLongPressAction,
          reinterpret_cast<uint8_t*>(s.sdFontFamilyName),
          sizeof(s.sdFontFamilyName),
          reinterpret_cast<uint8_t*>(s.dictionaryName),
          sizeof(s.dictionaryName),
          PluginLocations::anyPluginInstalledOnDisk,
          [](uint8_t language) -> XpStoreBytes {
            const char* code = language < getLanguageCount() ? LANGUAGE_CODES[language] : "EN";
            return {reinterpret_cast<const uint8_t*>(code), strlen(code)};
          },
          [](const uint8_t* code) {
            return static_cast<uint8_t>(I18n::languageFromCode(reinterpret_cast<const char*>(code)));
          },
          BoardConfig::hasHomeKey(),
          CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT,
          CrossPointSettings::SHORT_PWRBTN_COUNT};
}

struct FieldCodec {
  std::string decoded;
  String encoded;
  XpSettingCodec ops() {
    return {this,
            [](void* ctx, XpStoreBytes value, size_t limit, uint32_t* status) {
              auto& self = *static_cast<FieldCodec*>(ctx);
              bool ok = false, tooLong = false;
              self.decoded =
                  obfuscation::deobfuscateFromBase64(reinterpret_cast<const char*>(value.data), limit, &ok, &tooLong);
              *status = static_cast<uint32_t>(ok) | (static_cast<uint32_t>(tooLong) << 1);
              return store_bridge::bytes(self.decoded);
            },
            [](void* ctx, const uint8_t* value) -> XpStoreBytes {
              auto& self = *static_cast<FieldCodec*>(ctx);
              self.encoded = obfuscation::obfuscateToBase64(reinterpret_cast<const char*>(value));
              return {reinterpret_cast<const uint8_t*>(self.encoded.c_str()), self.encoded.length()};
            },
            [](void*, uint32_t event, const char* key) {
              if (event == 1) LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", key);
              if (event == 2) LOG_ERR("CPS", "Oversized obfuscated value for key '%s'", key);
            }};
  }
};

bool transferSetting(CrossPointSettings& settings, const SettingInfo& info, const XpStoreJsonOps& json, bool save) {
  FieldCodec codec;
  const auto codecOps = codec.ops();
  const XpSettingField field{info.key,
                             info.valuePtr ? &(settings.*info.valuePtr) : nullptr,
                             info.stringOffset ? reinterpret_cast<uint8_t*>(&settings) + info.stringOffset : nullptr,
                             info.stringMaxLen,
                             static_cast<uint32_t>(info.type),
                             info.valueRange.min,
                             info.type == SettingType::ENUM     ? static_cast<uint8_t>(info.enumLabels().size())
                             : info.type == SettingType::TOGGLE ? uint8_t{2}
                                                                : info.valueRange.max,
                             info.obfuscated};
  return xp_setting_field(&field, &json, &codecOps, save);
}
}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& s) {
  uint8_t* front[] = {&s.frontButtonBack, &s.frontButtonConfirm, &s.frontButtonLeft, &s.frontButtonRight};
  xp_setting_front(front);
}
uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(uint8_t legacyValue) {
  return xp_setting_sleep_minutes(legacyValue);
}
void CrossPointSettings::toJson(JsonDocument& doc) const {
  auto& self = const_cast<CrossPointSettings&>(*this);
  store_bridge::Json json(doc);
  const auto ops = json.ops();
  auto visit = [&](const SettingInfo& info) { transferSetting(self, info, ops, true); };
  forEachBaseSetting(visit);
  const auto settings = view(self);
  xp_settings_tail(&settings, &ops, true);
  bleturner::writeJson(ble, doc);
}
bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  store_bridge::JsonInput json(doc);
  const auto ops = json.ops();
  bool needsResave = false;
  auto visit = [&](const SettingInfo& info) { needsResave |= transferSetting(*this, info, ops, false); };
  forEachBaseSetting(visit);
  const auto settings = view(*this);
  needsResave |= xp_settings_tail(&settings, &ops, false);
  if (!bleturner::readJson(ble, doc)) needsResave = true;
  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }
  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  spec.showChapterPageCount = statusBarChapterPageCount != 0;
  spec.showBookProgressPercent = statusBarBookProgressPercentage != 0;
  spec.titleMode = statusBarTitle;
  spec.showBattery = statusBarBattery != 0;
  spec.showBatteryPercent = hideBatteryPercentage == HIDE_NEVER;
  spec.clockMode = statusBarClock;
  spec.clock12h = clockFormat == 1;
  spec.progressBarMode = statusBarProgressBar;
  spec.progressBarHeightPx =
      statusBarProgressBar != HIDE_PROGRESS ? static_cast<uint8_t>((statusBarProgressBarThickness + 1) * 2) : 0;
  spec.xtcMode = xtcStatusBarMode;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.characterSpacing = getCharacterSpacing();
  spec.wordSpacingPercent = wordSpacing;
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphIndentSpaces = paragraphIndentSpaces;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  return spec;
}

float CrossPointSettings::getReaderLineCompression() const {
  return xp_setting_line_compression(sdFontFamilyName[0] != 0, lineSpacing);
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const { return xp_setting_sleep_ms(sleepTimeoutMinutes); }

int CrossPointSettings::getRefreshFrequency() const { return xp_setting_refresh(refreshFrequency); }

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  fontPointSize =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  saveToFile();
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontPointSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating — this runs
  // in the page render loop) so rendering is correct even before it has run.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  switch (pt) {
    case 12:
      return NOTOSANS_12_FONT_ID;
    case 16:
      return NOTOSANS_16_FONT_ID;
    case 18:
      return NOTOSANS_18_FONT_ID;
    case 14:
    default:
      return NOTOSANS_14_FONT_ID;
  }
}

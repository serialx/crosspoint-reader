#include "ButtonRemapActivity.h"

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

void ButtonRemapActivity::onEnter() {
  xp_ui_settings_remap_init(&remap);
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;
}
void ButtonRemapActivity::loop() {
  if (consumeRejectedInput()) return;
  XpUiInput input{};
  input.now_ms = millis();
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) input.pressed |= XP_UI_UP;
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) input.pressed |= XP_UI_DOWN;
  // Raw assignment remains serialized with painting, as on the original screen.
  RenderLock lock(*this);
  const auto effect = xp_ui_settings_remap_update(&remap, &input, mappedInput.getPressedFrontButton());
  switch (effect.kind) {
    case 4:
      SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
      SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      SETTINGS.saveToFile();
      finish();
      break;
    case 5:
      SETTINGS.frontButtonBack = remap.mapping[0];
      SETTINGS.frontButtonConfirm = remap.mapping[1];
      SETTINGS.frontButtonLeft = remap.mapping[2];
      SETTINGS.frontButtonRight = remap.mapping[3];
      SETTINGS.saveToFile();
      finish();
      break;
    case XP_UI_CANCEL:
      finish();
      break;
    default:
      if (effect.invalidate) {
        selectIndex(remap.step);
        requestUpdate();
      }
      break;
  }
}
const char* ButtonRemapActivity::roleName(uint8_t index) {
  static constexpr StrId roles[] = {StrId::STR_BACK, StrId::STR_CONFIRM, StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT};
  return I18N.get(roles[index < 4 ? index : 3]);
}
const char* ButtonRemapActivity::hardwareName(uint8_t index) {
  switch (index) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    default:
      return "Unknown";
  }
}
const SettingsUiRow* ButtonRemapActivity::rowAt(int index) const {
  if (index < 0 || index >= 4) return nullptr;
  row = {};
  row.label = roleName(index);
  row.value = remap.mapping[index] == 255 ? tr(STR_UNASSIGNED) : hardwareName(remap.mapping[index]);
  return &row;
}
const char* ButtonRemapActivity::specialText(uint32_t handle) const {
  switch (handle) {
    case 0x80000004u:
      return tr(STR_REMAP_PROMPT);
    case 0x80000005u:
      return tr(STR_ALREADY_ASSIGNED);
    case 0x80000006u:
      return tr(STR_REMAP_RESET_HINT);
    case 0x80000007u:
      return tr(STR_REMAP_CANCEL_HINT);
    default:
      return nullptr;
  }
}
void ButtonRemapActivity::drawFooter() {
  const auto label = [this](uint8_t hardware) {
    for (uint8_t i = 0; i < 4; ++i)
      if (remap.mapping[i] == hardware) return roleName(i);
    return "-";
  };
  GUI.drawButtonHints(renderer, label(CrossPointSettings::FRONT_HW_BACK), label(CrossPointSettings::FRONT_HW_CONFIRM),
                      label(CrossPointSettings::FRONT_HW_LEFT), label(CrossPointSettings::FRONT_HW_RIGHT));
}

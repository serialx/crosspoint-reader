#include "IntervalSelectionActivity.h"

#include <HalGPIO.h>

#include <cstdio>

#include "components/UITheme.h"
IntervalSelectionActivity::IntervalSelectionActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                     const char* activityName, const StrId titleId,
                                                     const int initialValue, const int minValue, const int maxValue,
                                                     const int smallStep, const int largeStep,
                                                     const StrId valueFormatId, const bool readerActivity,
                                                     const StrId maxBoundaryLabelId)
    : UtilityActivity(activityName, renderer, input, XP_UI_UTILITY_INTERVAL, 18, 5),
      titleId(titleId),
      valueFormatId(valueFormatId),
      maxBoundaryLabelId(maxBoundaryLabelId),
      readerActivity(readerActivity) {
  state.value = initialValue;
  state.minimum = minValue;
  state.maximum = maxValue;
  state.small_step = smallStep;
  state.large_step = largeStep;
  state.edge_buttons = gpio.hasEdgeSideButtons();
}
void IntervalSelectionActivity::formatValue(char* buffer, const size_t size, const int value) const {
  if (maxBoundaryLabelId != StrId::STR_NONE_OPT && value == renderingState.maximum)
    snprintf(buffer, size, "%s", I18N.get(maxBoundaryLabelId));
  else if (valueFormatId != StrId::STR_NONE_OPT)
    snprintf(buffer, size, I18N.get(valueFormatId), static_cast<unsigned>(value));
  else
    snprintf(buffer, size, "%d", value);
}
void IntervalSelectionActivity::prepareUtilityView() {
  formatValue(readout, sizeof(readout), renderingState.value);
  formatValue(minText, sizeof(minText), renderingState.minimum);
  formatValue(maxText, sizeof(maxText), renderingState.maximum);
  char stepText[24];
  for (int i = 0; i < 2; ++i) {
    const int step = i ? renderingState.large_step : renderingState.small_step;
    if (valueFormatId != StrId::STR_NONE_OPT)
      snprintf(stepText, sizeof(stepText), I18N.get(valueFormatId), static_cast<unsigned>(step));
    else
      snprintf(stepText, sizeof(stepText), "%d", step);
    snprintf(hints[i], sizeof(hints[i]), "%s %s", I18N.get(i ? StrId::STR_STEP_HINT_SIDE : StrId::STR_STEP_HINT_FRONT),
             stepText);
  }
}
const char* IntervalSelectionActivity::utilityText(const uint32_t h) const {
  switch (h) {
    case Title:
      return I18N.get(titleId);
    case Readout:
      return readout;
    case Minimum:
      return minText;
    case Maximum:
      return maxText;
    case Hint1:
      return hints[0];
    case Hint2:
      return hints[1];
    default:
      return UtilityActivity::utilityText(h);
  }
}
void IntervalSelectionActivity::utilityHints() const {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
void IntervalSelectionActivity::applyUtilityEffect(const XpUiUtilityEffect& effect) {
  if (effect.navigation.kind == XP_UI_ACTIVATE) {
    setResult(IntervalResult{static_cast<uint32_t>(effect.navigation.value)});
    finish();
  } else
    UtilityActivity::applyUtilityEffect(effect);
}

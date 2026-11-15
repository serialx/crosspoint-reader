#include "IntervalSelectionActivity.h"

#include <HalGPIO.h>
IntervalSelectionActivity::IntervalSelectionActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                     const char* activityName, const StrId titleId,
                                                     const int initialValue, const int minValue, const int maxValue,
                                                     const int smallStep, const int largeStep,
                                                     const StrId valueFormatId, const bool readerActivity,
                                                     const StrId maxBoundaryLabelId)
    : DialogActivity(activityName, renderer, input),
      spec{static_cast<uint16_t>(titleId),
           static_cast<uint16_t>(valueFormatId),
           static_cast<uint16_t>(maxBoundaryLabelId),
           initialValue,
           minValue,
           maxValue,
           smallStep,
           largeStep,
           static_cast<uint8_t>(gpio.hasEdgeSideButtons()),
           static_cast<uint8_t>(readerActivity)} {}
XpDialogLaunch IntervalSelectionActivity::launch() const {
  XpDialogLaunch out{};
  out.kind = XP_DIALOG_INTERVAL;
  out.name = {reinterpret_cast<const uint8_t*>(name.data()), name.size()};
  out.interval = spec;
  return out;
}

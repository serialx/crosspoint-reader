#pragma once
#include <I18n.h>

#include "DialogActivity.h"
class IntervalSelectionActivity final : public DialogActivity {
  XpDialogInterval spec;

 public:
  explicit IntervalSelectionActivity(GfxRenderer&, MappedInputManager&, const char* activityName, StrId titleId,
                                     int initialValue, int minValue, int maxValue, int smallStep, int largeStep,
                                     StrId valueFormatId = StrId::STR_NONE_OPT, bool readerActivity = false,
                                     StrId maxBoundaryLabelId = StrId::STR_NONE_OPT);
  XpDialogLaunch launch() const override;
  bool isReaderActivity() const override { return spec.reader; }
};

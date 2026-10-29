#pragma once
#include <I18n.h>

#include "UtilityActivity.h"
class IntervalSelectionActivity final : public UtilityActivity {
  StrId titleId, valueFormatId, maxBoundaryLabelId;
  bool readerActivity;
  char readout[64]{}, hints[2][64]{}, minText[32]{}, maxText[32]{};
  void formatValue(char*, size_t, int) const;
  const char* utilityText(uint32_t) const override;
  void prepareUtilityView() override;
  void utilityHints() const override;
  void applyUtilityEffect(const XpUiUtilityEffect&) override;

 public:
  explicit IntervalSelectionActivity(GfxRenderer&, MappedInputManager&, const char* activityName, StrId titleId,
                                     int initialValue, int minValue, int maxValue, int smallStep, int largeStep,
                                     StrId valueFormatId = StrId::STR_NONE_OPT, bool readerActivity = false,
                                     StrId maxBoundaryLabelId = StrId::STR_NONE_OPT);
  bool isReaderActivity() const override { return readerActivity; }
};

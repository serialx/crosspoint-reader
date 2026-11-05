#pragma once
#include <Memory.h>
#include <xp_ui_utility.h>

#include "activities/RustActivityAdapter.h"

// Shared storage and borrowed resource bindings for the utility screen family.
class UtilityActivity : public RustActivityAdapter {
 protected:
  enum Text : uint32_t {
    Title = 1,
    Body,
    Cancel,
    Confirm,
    Readout,
    Minimum,
    Maximum,
    Hint1,
    Hint2,
    BrightLabel,
    WarmLabel,
    BrightValue,
    WarmValue,
    On,
    Off,
    Author,
    Source,
    Metadata,
    Status,
    Copies,
    Holds,
    DescriptionHeading,
    Description,
    ActionLabel,
    MoreLabel,
    Minus,
    Plus
  };
  enum Resource : uint32_t {
    Header = 1000,
    Footer,
    Cover,
    Html,
    Qr,
    Battery,
    Sun = 1010,
    SunFilled,
    Moon,
    RefreshIcon,
    OrientationIcon,
    TouchIcon
  };
  XpUiUtilityState state{};
  XpUiUtilityState renderingState{};
  XpUiUtilityFeedback feedback{}, buildingFeedback{};
  Publication publication{};
  std::unique_ptr<uint8_t[]> arena;
  uint16_t commandCapacity, hitCapacity;
  uint8_t buildHits = 0;
  XpUiUtilityEffect pendingEffect{};

  UtilityActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input, XpUiUtilityKind kind,
                  uint16_t commands, uint16_t hits);
  virtual const char* utilityText(uint32_t handle) const;
  using UtilityView = decltype(&xp_ui_utility_view);
  virtual UtilityView utilityView() const { return xp_ui_utility_view; }
  virtual void prepareUtilityView() {}
  virtual void utilityLayout(XpUiUtilityLayout&) const {}
  virtual void utilityHints() const = 0;
  virtual bool utilityResourceValid(uint32_t handle) const;
  virtual void utilityResource(const XpUiCommand& command);
  virtual void applyUtilityEffect(const XpUiUtilityEffect&);
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  void rustSceneFailed() override;
  void cancelRejectedInput(const XpUiInput&) override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t now) const override { return xp_ui_utility_interest(&state, now); }
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;

 public:
  void onEnter() override;
  void onExit() override;
};

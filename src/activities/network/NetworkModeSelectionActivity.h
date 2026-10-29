#pragma once
#include <BoardConfig.h>
#include <FreeInkUIGfxRenderer.h>
#include <xp_ui_network.h>

#include "activities/RustActivityAdapter.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, USB_DRIVE };
class NetworkModeSelectionActivity final : public RustActivityAdapter {
 public:
  explicit NetworkModeSelectionActivity(GfxRenderer&, MappedInputManager&);
#if FREEINK_CAP_USB_MSC
  static constexpr int MENU_ITEM_COUNT = 4;
#else
  static constexpr int MENU_ITEM_COUNT = 3;
#endif
  void onEnter() override;
  void onModeSelected(NetworkMode mode);
  void onCancel();

 private:
  XpUiListState state{}, next{};
  XpUiSettingsHits hits{}, nextHits{};
  XpUiCommand commands[24]{};
  XpUiSettingsRow rows[MENU_ITEM_COUNT]{};
  Publication publication;
  int32_t renderingScroll = 0;
  bool touchActivated = false;
  freeink::ui::GfxRendererTarget textTarget;
  const char* text(uint32_t) const;
  XpUiSettingsResources resources();
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  void rustSceneFailed() override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};

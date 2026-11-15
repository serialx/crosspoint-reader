#pragma once
#include <BoardConfig.h>
#include <FreeInkUIGfxRenderer.h>
#include <xp_network_owner.h>
#include <xp_ui_network.h>

#include "activities/Activity.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, USB_DRIVE };
class NetworkModeSelectionActivity final : public Activity {
 public:
  explicit NetworkModeSelectionActivity(GfxRenderer&, MappedInputManager&);
#if FREEINK_CAP_USB_MSC
  static constexpr int MENU_ITEM_COUNT = 4;
#else
  static constexpr int MENU_ITEM_COUNT = 3;
#endif
  void physicalLayout(XpUiSettingsLayout&);
  XpUiSettingsResources resources();
  void paint(const XpUiCommand&, XpUiRect);
  void prepareResult(bool cancelled, uint8_t mode);

 private:
  XpUiSettingsRow rows[MENU_ITEM_COUNT]{};
  freeink::ui::GfxRendererTarget textTarget;
  const char* text(uint32_t) const;
};

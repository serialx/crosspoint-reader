#pragma once
#include <xp_ui_boot.h>

#include "activities/RustActivityAdapter.h"

class BootUiActivity : public RustActivityAdapter {
 public:
  void loop() override {}
  void render(RenderLock&&) override {}

 protected:
  using RustActivityAdapter::RustActivityAdapter;
  struct ViewData {
    const char* subtitle = nullptr;
    const char* error = nullptr;
    const std::vector<std::string>* description = nullptr;
    const std::vector<std::string>* panic = nullptr;
  };
  bool paintBootView(XpUiBootView view, XpUiRefresh refresh, const ViewData& data, uint16_t flags = 0);
  bool paintBootView(XpUiBootView view, XpUiRefresh refresh) { return paintBootView(view, refresh, ViewData{}); }
  XpUiPaint rustResources() override;
  virtual void drawBootHints();

 private:
  template <uint16_t Capacity>
  bool paintBootViewWithCapacity(XpUiBootView view, XpUiRefresh refresh, const ViewData& data, uint16_t flags);
  bool paintBootViewInto(XpUiBootView view, XpUiRefresh refresh, const ViewData& data, uint16_t flags,
                         const XpUiBuffer& buffer);
  const ViewData* activeView = nullptr;
  const char* resourceText(uint32_t handle) const;
};

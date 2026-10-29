#pragma once
#include <xp_ui_boot.h>

#include "activities/RustActivityAdapter.h"

class BootUiActivity : public RustActivityAdapter {
 public:
  void loop() override {}
  void render(RenderLock&&) override {}

 protected:
  using RustActivityAdapter::RustActivityAdapter;
  // Image/gray views publish together; a later successful hint cannot hide failure.
  class ViewPublication {
    BootUiActivity& owner;

   public:
    explicit ViewPublication(BootUiActivity& owner) : owner(owner) {
      owner.deferPublication = true;
      owner.viewsValid = true;
    }
    ~ViewPublication() {
      owner.deferPublication = false;
      if (owner.viewsValid) owner.publishRustScene(XpUiScene{});
    }
  };
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
  bool deferPublication = false, viewsValid = true;
  const char* resourceText(uint32_t handle) const;
};

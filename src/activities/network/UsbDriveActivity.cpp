#include "UsbDriveActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;
static_assert(sizeof(XpUiUsbState) == 24 && alignof(XpUiUsbState) == 4);
static_assert(sizeof(XpUiNetworkLayout) == 32 && alignof(XpUiNetworkLayout) == 4);
static_assert(sizeof(XpUiUsbServices) == 3 * sizeof(void*));
static_assert(sizeof(XpUiNetworkTextServices) == 2 * sizeof(void*));
static_assert(offsetof(XpUiUsbState, phase) == 16 && offsetof(XpUiUsbState, reserved) == 22);
static_assert(offsetof(XpUiNetworkLayout, small_font) == 16 && offsetof(XpUiNetworkLayout, reserved) == 30);
static_assert(static_cast<uint8_t>(UsbDriveState::Unsupported) == XP_UI_USB_UNSUPPORTED);
static_assert(static_cast<uint8_t>(UsbDriveState::WaitingForHost) == XP_UI_USB_WAITING);
static_assert(static_cast<uint8_t>(UsbDriveState::Connected) == XP_UI_USB_CONNECTED);
static_assert(static_cast<uint8_t>(UsbDriveState::Ejected) == XP_UI_USB_EJECTED);
static_assert(static_cast<uint8_t>(UsbDriveState::Disconnected) == XP_UI_USB_DISCONNECTED);
static_assert(static_cast<uint8_t>(UsbDriveState::IoError) == XP_UI_USB_IO_ERROR);

void UsbDriveActivity::onEnter() {
  Activity::onEnter();
  run(XP_UI_NETWORK_ENTER);
}
void UsbDriveActivity::onExit() {
  run(XP_UI_NETWORK_EXIT);
  Activity::onExit();
}
void UsbDriveActivity::loop() {
  consumeRejectedInput();
  // Transport progress and the existing cancel policy continue during rejection.
  run(XP_UI_NETWORK_TICK);
}
void UsbDriveActivity::render(RenderLock&& lock) {
  RustActivityAdapter::render(std::move(lock));
  ModelLock modelLock(*this);
  if (renderingState.preparing && preparingFrame != PreparingFrame::Rejected)
    preparingFrame = PreparingFrame::Displayed;
}
void UsbDriveActivity::run(const XpUiNetworkEvent event) {
  XpUiUsbState next;
  {
    ModelLock lock(*this);
    next = model;
  }
  XpUiInput input{};
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) input.pressed |= XP_UI_BACK;
  if (mappedInput.wasPressed(MappedInputManager::Button::Power)) input.pressed |= XP_UI_POWER;
  const XpUiUsbServices services{this, service, [](void* context, const XpUiUsbState* state) {
                                   auto& self = *static_cast<UsbDriveActivity*>(context);
                                   ModelLock lock(self);
                                   self.model = *state;
                                 }};
  xp_ui_network_usb_run(&next, &services, event, &input, mappedInput.wasHomeGesture());
}
uint32_t UsbDriveActivity::service(void* context, const uint32_t operation, const uint32_t value) {
  auto& self = *static_cast<UsbDriveActivity*>(context);
  switch (static_cast<XpUiNetworkCall>(operation)) {
    case XP_UI_NETWORK_NOW:
      return millis();
    case XP_UI_NETWORK_PAINT:
      self.requestUpdate();
      break;
    case XP_UI_NETWORK_PAINT_WAIT:
      self.requestUpdateAndWait();
      {
        ModelLock lock(self);
        return self.preparingFrame == PreparingFrame::Displayed;
      }
    case XP_UI_NETWORK_DELAY:
      delay(value);
      break;
    case XP_UI_NETWORK_DIAGNOSTIC:
      switch (static_cast<XpUiUsbDiagnostic>(value)) {
        case XP_UI_USB_START_FAILURE:
          LOG_ERR("USB", "Unable to start USB Drive");
          break;
        case XP_UI_USB_SUSPEND_TIMEOUT:
          LOG_INF("USB", "USB Drive host suspend timed out; ending session");
          break;
        case XP_UI_USB_WAIT_TIMEOUT:
          LOG_INF("USB", "USB Drive host wait timed out");
          break;
        case XP_UI_USB_START_TIMEOUT:
          LOG_INF("USB", "USB Drive startup failure timed out");
          break;
        case XP_UI_USB_IO_FAILURE:
          LOG_ERR("USB", "USB Drive I/O error; disconnecting host");
          break;
        case XP_UI_USB_DISCONNECT_FAILURE:
          LOG_ERR("USB", "Unable to request USB Drive host disconnect");
          break;
        case XP_UI_USB_DISCONNECT_TIMEOUT:
          LOG_ERR("USB", "USB Drive host disconnect timed out; forcing restart");
          break;
      }
      break;
    case XP_UI_USB_PREPARING_STATUS: {
      ModelLock lock(self);
      const auto status = self.preparingFrame;
      if (status == PreparingFrame::Rejected) self.preparingFrame = PreparingFrame::Pending;
      return static_cast<uint8_t>(status);
    }
    case XP_UI_USB_CANCEL_PREPARING:
      self.finish();
      break;
    case XP_UI_USB_BEGIN:
      return Storage.beginUsbDrive();
    case XP_UI_USB_END:
      Storage.endUsbDrive();
      break;
    case XP_UI_USB_STATE:
      return static_cast<uint8_t>(Storage.usbDriveState());
    case XP_UI_USB_SUSPENDED:
      return Storage.usbDriveHostSuspended();
    case XP_UI_USB_DISCONNECT:
      return Storage.disconnectUsbDriveHost();
    case XP_UI_USB_RESTART:
      restartToHomeAfterStorageHandoff();
      break;
  }
  return 0;
}
const char* UsbDriveActivity::text(const uint32_t handle) {
  switch (handle) {
    case XP_UI_USB_PREPARING_TEXT:
      return tr(STR_USB_DRIVE_PREPARING);
    case XP_UI_USB_EJECT_TEXT:
      return tr(STR_USB_DRIVE_EJECT_HINT);
    case XP_UI_USB_WAITING_TEXT:
      return tr(STR_USB_DRIVE_WAITING);
    case XP_UI_USB_CONNECTED_TEXT:
      return tr(STR_USB_DRIVE_CONNECTED);
    case XP_UI_USB_DELAY_TEXT:
      return tr(STR_USB_DRIVE_CONNECT_DELAY);
    case XP_UI_USB_START_ERROR_TEXT:
      return tr(STR_USB_DRIVE_START_ERROR);
    case XP_UI_USB_ERROR_TEXT:
      return tr(STR_USB_DRIVE_ERROR);
    default:
      return nullptr;
  }
}
bool UsbDriveActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    renderingState = model;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  auto target = makeUiTarget(renderer);
  const auto& theme = refreshSharedUiThemeTokens(target);
  const auto safe = target.deviceContext().safeRect();
  const XpUiNetworkLayout layout{
      {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
      {safe.x, safe.y, safe.width, safe.height},
      uiScaleSpec().smallFontId,
      static_cast<int16_t>(m.topPadding),
      static_cast<int16_t>(m.headerHeight),
      static_cast<int16_t>(m.buttonHintsHeight),
      static_cast<int16_t>(m.contentSidePadding),
      theme.spaceMd,
      0};
  const XpUiNetworkTextServices texts{
      &target, [](void* context, uint32_t handle, int32_t, int16_t width, uint8_t bold, uint8_t lines) -> int16_t {
        auto& target = *static_cast<fui::GfxRendererTarget*>(context);
        fui::TextStyle style;
        style.font = fui::GfxRendererTarget::FONT_SMALL;
        style.align = fui::TextAlign::Center;
        style.bold = bold;
        style.maxLines = lines;
        return fui::measureWrappedText(target, text(handle), style, width).height;
      }};
  const XpUiBuffer buffer{commands, nullptr, 6, 0};
  return xp_ui_network_usb_view(&renderingState, &layout, &texts, &buffer, &scene);
}
XpUiPaint UsbDriveActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void*, uint32_t handle, uint32_t generation) -> uint8_t {
            return generation == 1 &&
                   (handle == XP_UI_NETWORK_HEADER || handle == XP_UI_NETWORK_FOOTER || text(handle));
          },
          [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<UsbDriveActivity*>(context);
            const auto previous = self.renderer.getClipRect();
            self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
            const auto& r = command->rect;
            if (command->kind == XP_UI_TEXT_HANDLE) {
              self.renderer.drawUiText(*command, clip, text(command->resource));
            } else if (command->resource == XP_UI_NETWORK_HEADER) {
              GUI.drawHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, tr(STR_USB_DRIVE));
            } else if (command->resource == XP_UI_NETWORK_FOOTER) {
              const auto labels = self.mappedInput.mapLabels(tr(STR_BACK), "", "", "");
              GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
            }
            self.renderer.setClipRect(previous[0], previous[1], previous[2], previous[3]);
          }};
}

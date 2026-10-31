#include <HalDisplay.h>
#include <HalGPIO.h>

// Rust display policy interprets these SDK and HAL values directly.
static_assert(EInkDisplay::FULL_REFRESH == 0 && EInkDisplay::HALF_REFRESH == 1 && EInkDisplay::FAST_REFRESH == 2);
static_assert(static_cast<uint32_t>(HalGPIO::WakeupReason::PowerButton) == 0 &&
              static_cast<uint32_t>(HalGPIO::WakeupReason::AfterFlash) == 1 &&
              static_cast<uint32_t>(HalGPIO::WakeupReason::AfterUSBPower) == 2 &&
              static_cast<uint32_t>(HalGPIO::WakeupReason::Other) == 3);

// Global HalDisplay instance
HalDisplay display;

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

HalDisplay::Controller HalDisplay::getController() const { return BoardConfig::ACTIVE.displayController; }

uint32_t HalDisplay::dispatch(void* ctx, XpHalDisplayOp op, uint32_t refresh, bool off, uint32_t gray) {
  auto& panel = static_cast<HalDisplay*>(ctx)->einkDisplay;
  const auto mode = static_cast<EInkDisplay::RefreshMode>(refresh);
  switch (op) {
    case XP_HAL_DISPLAY_X3:
      panel.setDisplayX3();
      break;
    case XP_HAL_DISPLAY_BEGIN:
      panel.begin();
      break;
    case XP_HAL_DISPLAY_SKIP:
      panel.skipInitialResync();
      break;
    case XP_HAL_DISPLAY_WAKE:
      return static_cast<uint32_t>(gpio.getWakeupReason());
    case XP_HAL_DISPLAY_RESYNC:
      panel.requestResync(static_cast<uint8_t>(refresh));
      break;
    case XP_HAL_DISPLAY_BUFFER:
      panel.displayBuffer(mode, off);
      break;
    case XP_HAL_DISPLAY_ASYNC:
      panel.displayBufferAsyncNoShadow(mode);
      break;
    case XP_HAL_DISPLAY_REFRESH:
      panel.refreshDisplay(mode, off);
      break;
    case XP_HAL_DISPLAY_GRAY:
      panel.displayGrayscaleBase(mode, off);
      break;
    case XP_HAL_DISPLAY_GRAY_MODE:
      return panel.displayGrayscaleBase(static_cast<GrayscaleMode>(gray), mode, off);
  }
  return 0;
}
void HalDisplay::begin(bool seamless) { xp_hal_display_begin(this, dispatch, gpio.deviceIsX3(), seamless); }

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  einkDisplay.drawImageTransparent(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  xp_hal_display_refresh(this, dispatch, gpio.deviceIsX3(), XP_HAL_DISPLAY_BUFFER, mode, turnOffScreen, 0);
}

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode) {
  xp_hal_display_refresh(this, dispatch, gpio.deviceIsX3(), XP_HAL_DISPLAY_ASYNC, mode, false, 0);
}

void HalDisplay::waitRefreshComplete() { einkDisplay.waitRefreshComplete(); }

bool HalDisplay::supportsAsyncRefresh() const { return einkDisplay.supportsAsyncRefresh(); }

HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode mode) const {
  return einkDisplay.grayscaleCapabilities(mode);
}

bool HalDisplay::supportsAsyncGrayscaleBase() const { return grayscaleCapabilities().asyncBase; }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  xp_hal_display_refresh(this, dispatch, gpio.deviceIsX3(), XP_HAL_DISPLAY_REFRESH, mode, turnOffScreen, 0);
}

void HalDisplay::setInverted(bool inverted) { einkDisplay.setInverted(inverted); }

bool HalDisplay::toggleInverted() { return einkDisplay.toggleInverted(); }

bool HalDisplay::isInverted() const { return einkDisplay.isInverted(); }

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) { return einkDisplay.lendBuildStorage(sizeOut); }

void HalDisplay::returnFrameBufferStorage() { einkDisplay.returnBuildStorage(); }

bool HalDisplay::displayGrayscaleBase(GrayscaleMode mode, RefreshMode fallback, bool turnOffScreen) {
  return xp_hal_display_refresh(this, dispatch, gpio.deviceIsX3(), XP_HAL_DISPLAY_GRAY_MODE, fallback, turnOffScreen,
                                static_cast<uint32_t>(mode));
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen) {
  xp_hal_display_refresh(this, dispatch, gpio.deviceIsX3(), XP_HAL_DISPLAY_GRAY, fallback, turnOffScreen, 0);
}

void HalDisplay::preconditionGrayscale() { einkDisplay.preconditionGrayscale(); }

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { einkDisplay.displayGrayBuffer(turnOffScreen); }

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  einkDisplay.writeGrayscalePlaneStrip(lsbPlane ? EInkDisplay::GRAY_PLANE_LSB : EInkDisplay::GRAY_PLANE_MSB, rows,
                                       yStart, numRows);
}

bool HalDisplay::supportsStripGrayscale() const { return grayscaleCapabilities().stripUploads; }

bool HalDisplay::combinesGrayscaleBase() const { return grayscaleCapabilities().base == GrayscaleBase::Combined; }

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
bool HalDisplay::setFrameSource(XpDisplaySource* source) { return einkDisplay.setFrameSource(source); }
#endif

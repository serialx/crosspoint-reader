  FontCacheManager frameCache;
  int frameStride = 60;
  RenderMode frameMode = BW;
  uint8_t framePixels[100 * 800]{};
  uint8_t* stripPixels = nullptr;
  int stripRows = 0;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  XpPagePlan* framePlan = nullptr;
#endif
  unsigned displays = 0, stripBegins = 0;
  void (*onDisplay)() = nullptr;
  uint8_t* getFrameBuffer() { return framePixels; }
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  void usePagePlan(XpPagePlan* plan, XpPageRowBounds*, size_t, bool recording = false) {
    framePlan = plan;
    if (recording) plan->valid = true;
  }
#endif
  void copyStatusToStrip(int, bool) {}

  FontCacheManager* getFontCacheManager() const { return const_cast<FontCacheManager*>(&frameCache); }
  FrameCapabilities grayscaleCapabilities(HalDisplay::GrayscaleMode = HalDisplay::GrayscaleMode::Overlay) const { return {}; }
  int getScreenHeight() const { return getDisplayHeight(); }
  void displayBuffer(HalDisplay::Mode) { ++displays; if (onDisplay) onDisplay(); }
  void displayBufferAsync(HalDisplay::Mode) {}
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode, HalDisplay::Mode) { return false; }
  void displayGrayscaleBase(HalDisplay::Mode) {}
  void clearScreen(uint8_t value = 0xff) { if (stripPixels) memset(stripPixels, value, stripRows * frameStride); }
  void preconditionGrayscale() {}
  void setRenderMode(RenderMode mode) { frameMode = mode; }
  void beginStripTarget(uint8_t* pixels, int, int rows) { assert(!stripPixels); stripPixels = pixels; stripRows = rows; ++stripBegins; }
  void endStripTarget() { assert(stripPixels); stripPixels = nullptr; }
  void waitRefreshComplete() {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, int, int) {}
  void displayGrayBuffer() {}
  void cleanupGrayscaleWithFrameBuffer() {}
  bool storeBwBuffer() { return false; }
  void restoreBwBuffer() {}
  void copyGrayscaleLsbBuffers() {}
  void copyGrayscaleMsbBuffers() {}

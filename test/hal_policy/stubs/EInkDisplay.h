#pragma once
#include <Arduino.h>
namespace freeink {
enum class GrayscaleMode { Overlay, Absolute, Direct };
enum class GrayscaleBase { Separate, Combined };
enum class GrayscaleEncoding { Separate, Combined };
struct GrayscaleCapabilities { bool asyncBase=true, stripUploads=true; GrayscaleBase base=GrayscaleBase::Separate; };
}
struct EInkDisplay {
 enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
 enum GrayPlane { GRAY_PLANE_LSB, GRAY_PLANE_MSB };
 static constexpr uint16_t DISPLAY_WIDTH=792, DISPLAY_HEIGHT=528;
 EInkDisplay(int,int,int,int,int,int) {}
 void setDisplayX3() { puts("display x3"); }
 void begin() { puts("display begin"); }
 void skipInitialResync() { puts("skip resync"); }
 void requestResync(int n=1) { printf("resync %d\n",n); }
 void displayBuffer(RefreshMode mode, bool off) { printf("buffer %d %u\n",mode,off); }
 void displayBufferAsyncNoShadow(RefreshMode mode) { printf("async %d\n",mode); }
 void refreshDisplay(RefreshMode mode, bool off) { printf("refresh %d %u\n",mode,off); }
 bool displayGrayscaleBase(freeink::GrayscaleMode mode, RefreshMode fallback, bool off) { printf("gray mode %d %d %u\n",int(mode),fallback,off); return true; }
 void displayGrayscaleBase(RefreshMode fallback, bool off) { printf("gray %d %u\n",fallback,off); }
 freeink::GrayscaleCapabilities grayscaleCapabilities(freeink::GrayscaleMode) const { return {}; }
 template<typename... T> void clearScreen(T...) const {}
 template<typename... T> void drawImage(T...) const {}
 template<typename... T> void drawImageTransparent(T...) const {}
 template<typename... T> void waitRefreshComplete(T...) const {}
 template<typename... T> void setInverted(T...) const {}
 template<typename... T> void deepSleep(T...) const {}
 template<typename... T> void returnBuildStorage(T...) const {}
 template<typename... T> void copyGrayscaleBuffers(T...) const {}
 template<typename... T> void preconditionGrayscale(T...) const {}
 template<typename... T> void copyGrayscaleLsbBuffers(T...) const {}
 template<typename... T> void copyGrayscaleMsbBuffers(T...) const {}
 template<typename... T> void cleanupGrayscaleBuffers(T...) const {}
 template<typename... T> void displayGrayBuffer(T...) const {}
 template<typename... T> void writeGrayscalePlaneStrip(T...) const {}
 bool supportsAsyncRefresh() const { return true; }
 bool toggleInverted() const { return true; }
 bool isInverted() const { return true; }
 uint32_t getDisplayWidth() const { return 0; }
 uint32_t getDisplayHeight() const { return 0; }
 uint32_t getDisplayWidthBytes() const { return 0; }
 uint32_t getBufferSize() const { return 0; }
 uint8_t* getFrameBuffer() const { return nullptr; }
 uint8_t* lendBuildStorage(uint32_t*) { return nullptr; }
};

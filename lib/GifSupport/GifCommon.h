#pragma once

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <xp_ffi.h>

using GifBasicInfo = XpGifInfo;

namespace GifCommon {
struct Source {
  HalFile& file;
  uint32_t lastYieldMs = 0;
  const char* path = "";
  const char* cachePath = "";
  int targetWidth = 0, targetHeight = 0;
  uint32_t freeHeap = 0;
  bool bmp = false;
  bool decoding = false;
  bool seekFailed = false;
  bool animated = false;
  static void diagnostic(void* raw, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    auto& self = *static_cast<Source*>(raw);
    switch (static_cast<XpGifDiagnostic>(event)) {
      case XP_GIF_DIAG_BlockLength:
        if (self.seekFailed && a) {
          LOG_ERR("GIF", "Failed to skip GIF sub-block (%u bytes)", a);
        } else {
          LOG_ERR("GIF", "Failed to read GIF sub-block length");
        }
        break;
      case XP_GIF_DIAG_BlockSkip:
        LOG_ERR("GIF", "Failed to skip GIF sub-block (%u bytes)", a);
        break;
      case XP_GIF_DIAG_HeaderRead:
        if (self.seekFailed) {
          LOG_ERR("GIF", "Failed to seek to GIF header");
        } else {
          LOG_ERR("GIF", "Failed to read GIF header");
        }
        break;
      case XP_GIF_DIAG_Signature:
        LOG_ERR("GIF", "Invalid GIF signature");
        break;
      case XP_GIF_DIAG_MarkerRead:
        if (self.seekFailed && a) {
          LOG_ERR("GIF", "Failed to skip GIF global color table");
        } else {
          LOG_ERR("GIF", "Unexpected EOF before GIF image descriptor");
        }
        break;
      case XP_GIF_DIAG_Trailer:
        LOG_ERR("GIF", "GIF trailer reached before image descriptor");
        break;
      case XP_GIF_DIAG_ExtensionLabel:
        LOG_ERR("GIF", "Failed to read GIF extension label");
        break;
      case XP_GIF_DIAG_ExtensionData:
        LOG_ERR("GIF", "Failed to skip GIF extension data");
        break;
      case XP_GIF_DIAG_Descriptor:
        LOG_ERR("GIF", "Failed to read GIF image descriptor");
        break;
      case XP_GIF_DIAG_CodeSize:
        LOG_ERR("GIF", "Invalid or missing LZW minimum code size");
        break;
      case XP_GIF_DIAG_Marker:
        LOG_ERR("GIF", "Unexpected GIF block marker 0x%02X", a);
        break;
      case XP_GIF_DIAG_InvalidHeader:
        if (self.bmp) {
          LOG_ERR("GIF", "Invalid GIF header");
        } else {
          LOG_ERR("GIF", "Failed to parse GIF header: %s", self.path);
        }
        break;
      case XP_GIF_DIAG_InterlaceBmp:
        LOG_ERR("GIF", "Interlaced GIFs are not supported for BMP conversion");
        break;
      case XP_GIF_DIAG_Dimensions:
        LOG_ERR("GIF", "Image too large or zero (%ux%u)", a, b);
        break;
      case XP_GIF_DIAG_OutputDimensions:
        LOG_ERR("GIF", "Invalid GIF output dimensions: %dx%d", static_cast<int>(a), static_cast<int>(b));
        break;
      case XP_GIF_DIAG_OutputLimit:
        LOG_ERR("GIF", "GIF BMP exceeds output byte limit");
        break;
      case XP_GIF_DIAG_BmpHeader:
        LOG_ERR("GIF", "Failed to write BMP header");
        break;
      case XP_GIF_DIAG_BmpRows:
        LOG_ERR("GIF", "OOM: GIF BMP row buffers");
        break;
      case XP_GIF_DIAG_Scaling:
        LOG_ERR("GIF", "OOM: GIF scaling accumulators");
        break;
      case XP_GIF_DIAG_Atkinson1:
        LOG_ERR("GIF", "OOM: Atkinson1BitDitherer");
        break;
      case XP_GIF_DIAG_Atkinson2:
        LOG_ERR("GIF", "OOM: AtkinsonDitherer");
        break;
      case XP_GIF_DIAG_DecoderAlloc:
        LOG_ERR("GIF", "OOM: AnimatedGIF decoder");
        break;
      case XP_GIF_DIAG_Decode:
        LOG_ERR("GIF", "GIF decode failed");
        break;
      case XP_GIF_DIAG_Postprocess:
        LOG_ERR("GIF", "GIF BMP post-process failed");
        break;
      case XP_GIF_DIAG_Complete:
        if (self.animated) LOG_INF("GIF", "Animated GIF detected - using first frame only for BMP conversion");
        LOG_DBG("GIF", "Successfully converted GIF to BMP");
        break;
      case XP_GIF_DIAG_FrameHeader:
        LOG_DBG("GIF",
                "GIF header: canvas=%ux%u frame=%ux%u frameOffset=%u,%u interlaced=%s renderTarget=%dx%d MAX_WIDTH=%d "
                "freeHeap=%u file=%s",
                a & 65535, a >> 16, b & 65535, b >> 16, c & 65535, c >> 16, d ? "yes" : "no", self.targetWidth,
                self.targetHeight, 3072, self.freeHeap, self.path);
        break;
      case XP_GIF_DIAG_CanvasWidth:
        LOG_ERR("GIF", "GIF canvas too wide for AnimatedGIF: canvas=%ux%u MAX_WIDTH=%d renderTarget=%dx%d file=%s", a,
                b, 3072, self.targetWidth, self.targetHeight, self.path);
        break;
      case XP_GIF_DIAG_Cache:
        LOG_ERR("GIF", "Failed to start cache stream");
        break;
      case XP_GIF_DIAG_Finish:
        if (a) {
          LOG_ERR("GIF", "GIF post-process failed: %s", self.path);
        } else {
          LOG_ERR("GIF", "Failed to finalize pixel cache: %s", self.cachePath);
        }
        break;
      case XP_GIF_DIAG_Pipeline:
        LOG_ERR("IMG", "Invalid image pipeline dimensions/configuration");
        break;
      case XP_GIF_DIAG_PipelineAlloc:
        LOG_ERR("IMG", "Failed to allocate image row buffers");
        break;
      case XP_GIF_DIAG_DecodeStart:
        self.decoding = true;
        break;
      case XP_GIF_DIAG_Interlace:
        LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.",
                "interlaced GIF", self.path);
        break;
      case XP_GIF_DIAG_FrameDimensions:
        if (!a || !b) {
          LOG_ERR("IMG", "Invalid %s dimensions: %lldx%lld", "GIF", static_cast<long long>(a),
                  static_cast<long long>(b));
        } else if (a > 32767 || b > 32767) {
          LOG_ERR("IMG", "%s dimensions exceed supported limit: %lldx%lld (max %lld per dimension)", "GIF",
                  static_cast<long long>(a), static_cast<long long>(b), 32767LL);
        } else {
          LOG_ERR("IMG", "%s too large (%lldx%lld = %lld pixels), max supported: %lld pixels", "GIF",
                  static_cast<long long>(a), static_cast<long long>(b), static_cast<long long>(a) * b, 8388608LL);
        }
        break;
      case XP_GIF_DIAG_FrameEnd:
        if (self.file.size() > 10 && a < self.file.size() - 10) {
          if (self.bmp) {
            self.animated = true;
          } else {
            LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.",
                    "animation (using first frame only)", self.path);
          }
        }
        break;
      case XP_GIF_DIAG_Open:
        if (self.bmp) {
          LOG_ERR("GIF", "Failed to open GIF");
        } else {
          LOG_ERR("GIF", "AnimatedGIF open failed: canvas=%ux%u MAX_WIDTH=%d file=%s", a, b, 3072, self.path);
        }
        break;
    }
  }
  static void poll(void* context) {
    auto& source = *static_cast<Source*>(context);
    const uint32_t now = millis();
    if (now - source.lastYieldMs >= 250) {
      source.lastYieldMs = now;
      vTaskDelay(1);
    }
  }
  static size_t read(void* context, uint32_t offset, uint8_t* output, size_t len) {
    auto& source = *static_cast<Source*>(context);
    source.seekFailed = source.file.position() != offset && !source.file.seek(offset);
    if (source.seekFailed) {
      if (source.decoding && !source.bmp) LOG_ERR("GIF", "Seek failed at offset %d", static_cast<int>(offset));
      return 0;
    }
    const int count = source.file.read(output, len);
    if (count < 0 && source.decoding && !source.bmp)
      LOG_ERR("GIF", "Read failed at offset %d for %d bytes", static_cast<int>(offset), static_cast<int>(len));
    poll(context);
    return count > 0 ? static_cast<size_t>(count) : 0;
  }
  XpGifInput input() { return {this, read, poll, diagnostic}; }
};
inline bool readBasicInfo(HalFile& file, GifBasicInfo& info) {
  Source source{file};
  const auto input = source.input();
  const bool success = xp_gif_inspect(&input, &info);
  return file.seek(0) && success;
}
}  // namespace GifCommon

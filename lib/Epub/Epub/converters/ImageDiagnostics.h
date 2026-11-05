#pragma once

#include <Logging.h>
#include <xp_ffi.h>

#include <cstring>

inline float imageDiagnosticFloat(uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

// Integer event payloads are documented beside XpImageDiagnostic in xp_ffi.h.
inline void logImageDiagnostic(uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d, const char* cachePath,
                               const char* path = "", int pngColor = -1, int pngDepth = 0) {
  enum class Kind : uint32_t { Png, Jpeg, Gif };
  switch (static_cast<XpImageDiagnostic>(event)) {
    case XP_IMAGE_DIAG_CacheBand:
      LOG_ERR("IMG", "Cache band too small (%d < %d rows) for %dx%d", static_cast<int>(a), static_cast<int>(b),
              static_cast<int>(c), static_cast<int>(d));
      break;
    case XP_IMAGE_DIAG_CacheAllocation:
      LOG_ERR("IMG", "OOM cache band: %u bytes", a);
      break;
    case XP_IMAGE_DIAG_CacheOpen:
      LOG_ERR("IMG", "Failed to open cache file for writing: %s", cachePath);
      break;
    case XP_IMAGE_DIAG_CacheHeader:
      LOG_ERR("IMG", "Failed to write cache header: %s", cachePath);
      break;
    case XP_IMAGE_DIAG_CacheStarted:
      LOG_DBG("IMG", "Cache stream started: %s (%dx%d, band %d rows)", cachePath, static_cast<int>(a),
              static_cast<int>(b), static_cast<int>(c));
      break;
    case XP_IMAGE_DIAG_CacheRow:
      LOG_ERR("IMG", "Cache write error at row %d", static_cast<int>(a));
      break;
    case XP_IMAGE_DIAG_CacheClose:
      LOG_ERR("IMG", "Failed to close cache: %s", cachePath);
      break;
    case XP_IMAGE_DIAG_CacheWritten:
      LOG_DBG("IMG", "Cache written: %s (%dx%d, %d bytes)", cachePath, static_cast<int>(a), static_cast<int>(b),
              static_cast<int>(c));
      break;
    case XP_IMAGE_DIAG_OutputDimensions:
      switch (static_cast<Kind>(a)) {
        case Kind::Jpeg:
          LOG_ERR("JPG", "Degenerate output dimensions %dx%d for %s, skipping render", static_cast<int>(b),
                  static_cast<int>(c), path);
          break;
        case Kind::Gif:
          LOG_ERR("GIF", "Degenerate output dimensions %dx%d for %s", static_cast<int>(b), static_cast<int>(c), path);
          break;
        case Kind::Png:
          LOG_ERR("IMG", "Invalid image pipeline dimensions/configuration");
          break;
      }
      break;
    case XP_IMAGE_DIAG_RowAllocation:
      switch (static_cast<Kind>(a)) {
        case Kind::Png:
          LOG_ERR("PNG", "Failed to allocate PNG line buffers (%u bytes)", b);
          break;
        case Kind::Gif:
          LOG_ERR("GIF", "OOM: GIF row buffers (%d bytes each)", static_cast<int>(b));
          break;
        case Kind::Jpeg:
          break;
      }
      break;
    case XP_IMAGE_DIAG_Configuration:
      LOG_ERR("IMG", "Invalid image pipeline dimensions/configuration");
      break;
    case XP_IMAGE_DIAG_PngSummary:
      if (pngColor >= 0) {
        LOG_DBG("PNG", "PNG %dx%d (visible %dx%d) -> %dx%d (scale %.2f), type: %d, bpp: %d",
                static_cast<int>(a & 65535), static_cast<int>(a >> 16), static_cast<int>(b & 65535),
                static_cast<int>(b >> 16), static_cast<int16_t>(c & 65535), static_cast<int16_t>(c >> 16),
                imageDiagnosticFloat(d), pngColor, pngDepth);
      }
      break;
    case XP_IMAGE_DIAG_JpegSummary:
      LOG_DBG("JPG", "JPEG %dx%d -> %dx%d (scale %.2f, jpegScale 1/%d, fineScale %.2f)%s", static_cast<int>(a & 32767),
              static_cast<int>((a >> 15) & 32767), static_cast<int>(b & 32767), static_cast<int>((b >> 16) & 32767),
              imageDiagnosticFloat(c), 1 << (a >> 30), imageDiagnosticFloat(d), b >> 31 ? " [progressive]" : "");
      break;
  }
}

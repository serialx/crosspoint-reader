#pragma once

#include <Logging.h>
#include <xp_ffi.h>

// C++ owns formatting and log-level gating; Rust reports integers only.
inline void logPngDiagnostic(uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d, int targetWidth,
                             int targetHeight, const char* path = "") {
  switch (static_cast<XpPngDiagnostic>(event)) {
    case XP_PNG_DIAG_UnsupportedDepth:
      LOG_ERR("IMG",
              "Warning: Unsupported feature 'bit depth (%ubpp) for pixel type %u' in image '%s'. Image may not display "
              "correctly.",
              a, b, path);
      break;
    case XP_PNG_DIAG_RowBuffer:
      LOG_ERR(
          "PNG",
          "PNG row buffer too small: need %d bytes for width=%d type=%d bpp=%d, configured PNG_MAX_BUFFERED_PIXELS=%d",
          static_cast<int>(a), static_cast<int>(b), static_cast<int>(c), static_cast<int>(d), 16416);
      LOG_ERR("PNG", "Aborting decode to avoid PNGdec internal buffer overflow");
      break;
    case XP_PNG_DIAG_FrameDimensions:
      if (static_cast<int32_t>(a) <= 0 || static_cast<int32_t>(b) <= 0) {
        LOG_ERR("IMG", "Invalid %s dimensions: %lldx%lld", "PNG", static_cast<long long>(static_cast<int32_t>(a)),
                static_cast<long long>(static_cast<int32_t>(b)));
      } else if (a > 32767 || b > 32767) {
        LOG_ERR("IMG", "%s dimensions exceed supported limit: %lldx%lld (max %lld per dimension)", "PNG",
                static_cast<long long>(a), static_cast<long long>(b), 32767LL);
      } else {
        LOG_ERR("IMG", "%s too large (%lldx%lld = %lld pixels), max supported: %lld pixels", "PNG",
                static_cast<long long>(a), static_cast<long long>(b), static_cast<long long>(a) * b, 8388608LL);
      }
      break;
    case XP_PNG_DIAG_GrayWidth:
      LOG_ERR("PNG", "Expanded gray row too wide: need %u bytes for width=%d, max=%u", a, static_cast<int>(a), 8208u);
      break;
    case XP_PNG_DIAG_Signature:
      LOG_ERR("PNG", "Invalid PNG signature");
      break;
    case XP_PNG_DIAG_Ihdr:
      LOG_ERR("PNG", "Missing IHDR chunk");
      break;
    case XP_PNG_DIAG_Image:
      LOG_DBG("PNG", "Image: %ux%u, depth=%u, color=%u, interlace=%u", a, b, c & 255, (c >> 8) & 255, c >> 16);
      break;
    case XP_PNG_DIAG_Method:
      LOG_ERR("PNG", "Unsupported compression/filter method");
      break;
    case XP_PNG_DIAG_Interlace:
      LOG_ERR("PNG", "Interlaced PNGs not supported");
      break;
    case XP_PNG_DIAG_Dimensions:
      LOG_ERR("PNG", "Image too large or zero (%ux%u)", a, b);
      break;
    case XP_PNG_DIAG_Color:
      LOG_ERR("PNG", "Unsupported color type: %d", static_cast<int>(a));
      break;
    case XP_PNG_DIAG_RowSize:
      LOG_ERR("PNG", "Row too large: %u bytes", a);
      break;
    case XP_PNG_DIAG_Scanline:
      LOG_ERR("PNG", "OOM: scanline buffers (%u bytes each)", a);
      break;
    case XP_PNG_DIAG_Idat:
      LOG_ERR("PNG", "No IDAT chunk found");
      break;
    case XP_PNG_DIAG_Inflate:
      LOG_ERR("PNG", "Failed to init inflate stream");
      break;
    case XP_PNG_DIAG_Scaling:
      LOG_DBG("PNG", "Scaling %ux%u -> %dx%d (target %dx%d)", a, b, static_cast<int>(c), static_cast<int>(d),
              targetWidth, targetHeight);
      break;
    case XP_PNG_DIAG_RowScratch:
      LOG_ERR("PNG", "OOM: row scratch buffer (%u bytes)", a);
      break;
    case XP_PNG_DIAG_Atkinson1:
      LOG_ERR("PNG", "OOM: Atkinson1BitDitherer or row buffers");
      break;
    case XP_PNG_DIAG_Atkinson2:
      LOG_ERR("PNG", "OOM: AtkinsonDitherer or row buffers");
      break;
    case XP_PNG_DIAG_Accumulators:
      LOG_ERR("PNG", "OOM: scaling accumulators");
      break;
    case XP_PNG_DIAG_ScanlineDecode:
      LOG_ERR("PNG", "Failed to decode scanline %u", a);
      break;
    case XP_PNG_DIAG_Filter:
      LOG_ERR("PNG", "Unknown filter type: %d", static_cast<int>(a));
      break;
    case XP_PNG_DIAG_Complete:
      LOG_DBG("PNG", "Successfully converted PNG to BMP");
      break;
    case XP_PNG_DIAG_Open:
      LOG_ERR("PNG", "Failed to open PNG");
      break;
    case XP_PNG_DIAG_DecoderAlloc:
      LOG_ERR("PNG", "Failed to allocate PNG decoder");
      break;
    case XP_PNG_DIAG_Pipeline:
      LOG_ERR("IMG", "Invalid image pipeline dimensions/configuration");
      break;
    case XP_PNG_DIAG_PipelineAlloc:
      LOG_ERR("IMG", "Failed to allocate image row buffers");
      break;
    case XP_PNG_DIAG_Cache:
      LOG_ERR("PNG", "Failed to start cache stream, continuing without caching");
      break;
    case XP_PNG_DIAG_DecodeStart:
      break;
    case XP_PNG_DIAG_DecodeFailed:
      LOG_ERR("PNG", "Decode failed");
      break;
    case XP_PNG_DIAG_DecodeComplete:
      break;
  }
}

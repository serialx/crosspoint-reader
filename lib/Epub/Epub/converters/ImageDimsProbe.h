#pragma once
#include <Print.h>
#include <xp_ffi.h>

#include "ImageToFramebufferDecoder.h"

// Streaming JPEG/PNG/GIF header parser: finds image dimensions from the first few
// KB of a compressed stream without inflating the whole image. Feed bytes via
// the Print interface (e.g. Epub::readItemContentsToStream with
// allowEarlyStop=true); write() returns short once the dimensions are known or
// the stream is known to be unusable, which the zip layer treats as a polite
// early stop rather than an error.
//
// JPEG: walks marker segments (skipping EXIF/APPn of any size statefully, so
// nothing is buffered) until a SOFn frame header yields the dimensions.
// PNG: reads the IHDR fields at their fixed offsets (bytes 16..23).
// GIF: validates GIF87a/GIF89a and reads the logical screen size (bytes 6..9).
class ImageDimsProbe : public Print {
 public:
  ImageDimsProbe() { xp_image_probe_init(&probe); }
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* data, size_t len) override;

  // True only when a valid header was found; fills `out`.
  bool getDimensions(ImageDimensions& out) const;

 private:
  XpImageProbe probe;
};

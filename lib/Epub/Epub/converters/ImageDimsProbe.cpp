#include "ImageDimsProbe.h"

size_t ImageDimsProbe::write(uint8_t byte) { return xp_image_probe_write(&probe, &byte, 1); }
size_t ImageDimsProbe::write(const uint8_t* bytes, size_t len) { return xp_image_probe_write(&probe, bytes, len); }
bool ImageDimsProbe::getDimensions(ImageDimensions& out) const {
  uint32_t width, height;
  if (!xp_image_probe_dimensions(&probe, &width, &height)) return false;
  out.width = static_cast<int16_t>(width);
  out.height = static_cast<int16_t>(height);
  return true;
}

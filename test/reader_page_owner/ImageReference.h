#pragma once
#include <Epub/Page.h>
#include <xp_ffi.h>

#include <cstdio>
#include <cstdlib>
#include <string>
namespace imageReference {
inline bool needsDecode(const ImageBlock& image) {
  const auto source = image.getImagePath();
  if (xp_assets_image_failed(reinterpret_cast<const uint8_t*>(source.data()), source.size())) return false;
  std::string cache(source);
  const auto dot = cache.rfind('.');
  if (dot != std::string::npos) cache.resize(dot);
  cache += ".px2";
  FILE* file = std::fopen(cache.c_str(), "rb");
  if (!file) return true;
  uint8_t bytes[4]{};
  const bool header = std::fread(bytes, 1, 4, file) == 4;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fclose(file);
  const int w = bytes[0] | (int(bytes[1]) << 8), h = bytes[2] | (int(bytes[3]) << 8);
  return !header || !w || !h || std::abs(w - image.getWidth()) > 1 || std::abs(h - image.getHeight()) > 1 ||
         size < 4 + ((w + 3) / 4) * h;
}
inline bool needsDecode(const Page& page) {
  for (uint16_t i = 0; i < page.elementCount(); ++i) {
    const auto element = page.element(i);
    if (element.getTag() == TAG_PageImage && needsDecode(element.imageBlock())) return true;
  }
  return false;
}
}  // namespace imageReference

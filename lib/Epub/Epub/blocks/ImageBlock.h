#pragma once
#include <HalStorage.h>
#include <xp_page_paint.h>

#include <memory>
#include <string>
#include <string_view>

#include "Block.h"

class ImageBlock final : public Block {
 public:
  ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height);
  struct View {};
  // Borrowed paths must be NUL-terminated and outlive this view.
  ImageBlock(View, std::string_view imagePath, std::string_view srcPath, int16_t width, int16_t height)
      : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}
  ImageBlock(View, const ImageBlock& source)
      : ImageBlock(View{}, source.imagePath, source.srcPath, source.width, source.height) {}
  ~ImageBlock() override = default;

  std::string_view getImagePath() const { return imagePath; }
  int16_t getWidth() const { return width; }
  int16_t getHeight() const { return height; }

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  static void setRenderReadWindow(uint8_t* data, size_t size);
#endif

  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return false; }

#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file);
#endif

 private:
  std::string ownedImagePath;
  std::string ownedSrcPath;
  std::string_view imagePath;
  std::string_view srcPath;  // book-internal source href; empty once known-extracted
  int16_t width;
  int16_t height;
};

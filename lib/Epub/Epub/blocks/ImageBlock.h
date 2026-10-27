#pragma once
#include <HalStorage.h>

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

  bool hasValidCache() const;
  bool needsDecode() const;
  void renderPlaceholder(GfxRenderer& renderer, int x, int y) const;
  static void clearRenderFailures();

  // A page render draws its image up to ~13 times (BW double-refresh plus every
  // grayscale band pass), and each draw streams the whole .pxc off SD. The
  // first draw caches the pixel payload in RAM (chunked, heap-gated, falls back
  // to streaming when it doesn't fit); the reader calls this when the page
  // render completes so nothing stays resident between pages.
  static void releaseRenderCache();

  // Lazy extraction hook: the section build only header-probes images for their
  // dimensions; the file at imagePath is extracted out of the book on first
  // render, via this callback (function pointer + context, not std::function —
  // this is render-loop code). Registered by the reader activity that owns the
  // Epub, cleared on its exit.
  using ExtractFn = bool (*)(void* ctx, const char* srcPath, const char* destPath);
  static void setExtractor(void* ctx, ExtractFn fn);

  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return false; }

  void render(GfxRenderer& renderer, const int x, const int y);
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

  static void* extractCtx;
  static ExtractFn extractFn;
};

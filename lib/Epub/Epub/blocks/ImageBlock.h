#pragma once
#include <HalStorage.h>
#include <xp_page_paint.h>

#include <memory>
#include <string>
#include <string_view>

#include "Block.h"

class ImageBlock final : public Block {
#ifdef XP_APP_TEST
  friend uint8_t xp_native_page_image_extract(const XpPageImage*, uint8_t);
#endif

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

  // Host oracle wrapper for the shared Rust image cache. The first draw retains
  // pixel payload chunks in RAM when heap permits, otherwise it streams rows.
  // Shipping reader teardown releases these chunks directly in Rust after the
  // page render; it does not call this wrapper. Repeated grayscale passes can
  // reuse the payload within one render.
  static void releaseRenderCache();
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  static void setRenderReadWindow(uint8_t* data, size_t size);
#endif

  // Host oracle extraction hook. Reference fixtures register their native Epub
  // facade here and clear the borrowed pointer before destroying that facade.
  // The shipping reader instead binds its stable ReaderBook allocation in Rust.
  // Lazy extraction still runs on the first render when the source file is
  // missing; section construction normally probes only the image header.
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

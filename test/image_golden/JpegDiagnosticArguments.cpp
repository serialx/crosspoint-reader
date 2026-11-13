#include <GfxRenderer.h>
#include <JpegToFramebufferConverter.h>
#include <xp_book.h>

#include <cassert>
#include <cstdlib>
#include <new>

static bool failAllocation = false;
static unsigned events[12]{};
void* operator new(size_t size, const std::nothrow_t&) noexcept { return failAllocation ? nullptr : std::malloc(size); }
void jpegDiagnosticArguments(uint32_t event, int width, int height) {
  assert(event < 12);
  ++events[event];
  if (event == XP_BOOK_JPEG_EVENT_DIMENSIONS) {
    assert(width > 0 && height > 0);
  } else {
    assert(width == 0 && height == 0);
  }
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string path = argv[1];
  JpegToFramebufferConverter jpeg;
  GfxRenderer renderer;
  RenderConfig config{};
  config.maxWidth = 137;
  config.maxHeight = 173;
  assert(jpeg.decodeToFramebuffer(path, renderer, config));
  ImageDimensions output;
  assert(!jpeg.getDimensions(path + ".missing", output));
  assert(!jpeg.decodeToFramebuffer(path + ".missing", renderer, config));
  ESP.freeHeap = 0;
  assert(!jpeg.getDimensions(path, output));
  assert(!jpeg.decodeToFramebuffer(path, renderer, config));
  ESP.freeHeap = UINT32_MAX;
  failAllocation = true;
  assert(!jpeg.getDimensions(path, output));
  assert(!jpeg.decodeToFramebuffer(path, renderer, config));
  failAllocation = false;
  assert(jpeg.getDimensions(path, output));
  for (auto event :
       {XP_BOOK_JPEG_EVENT_RENDER, XP_BOOK_JPEG_EVENT_DIMENSIONS, XP_BOOK_JPEG_EVENT_HEAP,
        XP_BOOK_JPEG_EVENT_DIMENSION_HEAP, XP_BOOK_JPEG_EVENT_ALLOCATION, XP_BOOK_JPEG_EVENT_DIMENSION_ALLOCATION,
        XP_BOOK_JPEG_EVENT_OPEN, XP_BOOK_JPEG_EVENT_DIMENSION_OPEN}) {
    assert(events[event] > 0);
  }
}

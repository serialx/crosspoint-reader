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
  assert(argc == 3);
  const std::string path = argv[1], mode = argv[2];
  assert(mode == "decode" || mode == "probe");
  const bool probe = mode == "probe";
  JpegToFramebufferConverter jpeg;
  GfxRenderer renderer;
  RenderConfig config{};
  config.maxWidth = 137;
  config.maxHeight = 173;
  ImageDimensions output;
  auto run = [&](const std::string& source) {
    return probe ? jpeg.getDimensions(source, output) : jpeg.decodeToFramebuffer(source, renderer, config);
  };
  // Probe mode starts with unset output and a failed open, independently of decode.
  if (!probe) assert(run(path));
  assert(!run(path + ".missing"));
  ESP.freeHeap = 0;
  assert(!run(path));
  ESP.freeHeap = UINT32_MAX;
  failAllocation = true;
  assert(!run(path));
  failAllocation = false;
  assert(run(path));
  for (auto event : {probe ? XP_BOOK_JPEG_EVENT_DIMENSIONS : XP_BOOK_JPEG_EVENT_RENDER,
                     probe ? XP_BOOK_JPEG_EVENT_DIMENSION_HEAP : XP_BOOK_JPEG_EVENT_HEAP,
                     probe ? XP_BOOK_JPEG_EVENT_DIMENSION_ALLOCATION : XP_BOOK_JPEG_EVENT_ALLOCATION,
                     probe ? XP_BOOK_JPEG_EVENT_DIMENSION_OPEN : XP_BOOK_JPEG_EVENT_OPEN}) {
    assert(events[event] > 0);
  }
}

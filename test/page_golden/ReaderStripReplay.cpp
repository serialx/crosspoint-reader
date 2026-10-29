#include <SdCardFont.h>
#include <activities/reader/ReaderStripPainter.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

static void require(bool ok, const char* message) {
  if (!ok) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

#include "StripDisplayReplay.h"

int main(int argc, char** argv) {
  require(argc == 2, "usage: ReaderStripReplay FONT.cpfont (run in a scratch directory)");
  std::filesystem::copy_file(argv[1], "font.cpfont", std::filesystem::copy_options::overwrite_existing);
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  SdCardFont font;
  require(font.load("font.cpfont"), "load font");
  renderer.insertFont(100,
                      EpdFontFamily(font.getEpdFont(0), font.getEpdFont(1), font.getEpdFont(2), font.getEpdFont(3)));
  renderer.registerSdCardFont(100, &font);
  require(font.prewarm("AB", 1, false, false, false) == 0, "partial prewarm");
  Page owning;
  for (int y = 30; y < 700; y += 45) {
    auto block = std::make_unique<TextBlock>(
        std::vector<std::string>{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"}, std::vector<int16_t>{0},
        std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR}, std::vector<uint8_t>{}, std::vector<uint16_t>{});
    owning.elements.push_back(std::make_unique<PageLine>(std::move(block), 0, y));
  }
  {
    HalFile file("page.bin");
    require(owning.serialize(file), "serialize fixture");
  }
  std::ifstream file("page.bin", std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  auto page = Page::fromBytes(bytes.data(), bytes.size());
  require(bool(page), "decode fixture");
  alignas(8) std::array<uint8_t, 16384> commands{};
  std::vector<XpPageRowBounds> bounds(page->elementCount());
  XpPagePlan plan{commands.data(), commands.size(), 0, 0};
  renderer.usePagePlan(&plan, bounds.data(), bounds.size(), true);
  page->render(renderer, 100, 12, 12);
  require(plan.valid, "prepare production plan");
  renderer.usePagePlan(&plan, bounds.data(), bounds.size());
  renderer.clearScreen();
  std::array<uint8_t, 8000> scratch{};
  ReaderStripPainter painter{renderer, *page, 100, 12, 12, renderer.getScreenHeight(), false};
  auto replay = [&] {
    std::vector<uint8_t> result;
    for (uint16_t y = 0; y < display.getDisplayHeight(); y += 80) {
      const uint16_t rows = std::min<unsigned>(80, display.getDisplayHeight() - y);
      require(ReaderStripPainter::paint(&painter, scratch.data(), y, rows) == 0, "source callback");
      result.insert(result.end(), scratch.data(), scratch.data() + rows * display.getDisplayWidthBytes());
    }
    return result;
  };
  HalFileTest::watchedReadPath = "font.cpfont";
  HalFileTest::watchedReads = 0;
  const auto committed = replay();
  require(HalFileTest::watchedReads > 8, "fixture must exceed overflow glyph cache");
  const auto initialReads = HalFileTest::watchedReads;
  require(replay() == committed, "no-fault replay");
  require(HalFileTest::watchedReads == initialReads, "frozen replay must not reread font");
  HalFileTest::failWatchedReads = true;
  for (int pass = 0; pass < 4; ++pass) {
    require(replay() == committed, "late font failure changed committed BW replay");
    require(std::equal(committed.begin(), committed.end(), display.pixels.begin()),
            "late failure changed retained frame");
  }
  require(HalFileTest::watchedReads == initialReads, "late replay accessed failed font");
  HalFileTest::allowGlyphMetadata = true;
  painter.committedRows = 0;
  require(replay() != committed, "bitmap fault control did not affect pixels");
  require(HalFileTest::watchedReads > initialReads, "bitmap control did not read font");
  HalFileTest::allowGlyphMetadata = false;
  // A fresh raster under the same fault must lose pixels: prove the injected failure is relevant.
  painter.committedRows = 0;
  const auto damaged = replay();
  require(HalFileTest::watchedReads > initialReads, "fault control did not read font");
  require(damaged != committed, "fault control did not affect pixels");
  HalFileTest::failWatchedReads = false;
  painter.committedRows = 0;
  require(replay() == committed, "font recovery did not restore first upload");
  require(replay() == committed, "recovered next-frame baseline differs");
  StripDisplayReplay controllerReplay;
  for (const auto controller : std::array<uint32_t, 2>{XP_DISPLAY_UC8253_X3, XP_DISPLAY_SSD1677}) {
    painter.committedRows = 0;
    const auto control = controllerReplay.run(controller, painter, scratch.data(), false);
    require(std::equal(committed.begin(), committed.end(), display.pixels.begin()), "no-fault controller BW");
    painter.committedRows = 0;
    const auto fault = controllerReplay.run(controller, painter, scratch.data(), true);
    require(control == fault, "late fault changed display/reseed/next FAST controller bytes");
    require(std::equal(committed.begin(), committed.end(), display.pixels.begin()), "fault controller retained BW");
  }
  std::cout << "PASS real UC8253/SSD1677 source: every display/reseed/next FAST bus byte matches no-fault control\n";
  // A streamed image succeeds before a late cache failure; the retained BW stays intact.
  renderer.usePagePlan(nullptr, nullptr, 0);
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  ESP.freeHeap = 24 * 1024;
  std::vector<uint8_t> image(4 + 90 * 380, 0x1b);
  const uint16_t width = 360, height = 380;
  memcpy(image.data(), &width, 2);
  memcpy(image.data() + 2, &height, 2);
  {
    HalFile cache("image.px2");
    require(cache.write(image.data(), image.size()) == image.size(), "write image cache");
  }
  Page imageOwner;
  imageOwner.elements.push_back(
      std::make_unique<PageImage>(std::make_unique<ImageBlock>("image.png", "", width, height), 0, 0));
  {
    HalFile output("image-page.bin");
    require(imageOwner.serialize(output), "serialize image");
  }
  std::ifstream input("image-page.bin", std::ios::binary);
  std::vector<uint8_t> imageBytes{std::istreambuf_iterator<char>(input), {}};
  auto imagePage = Page::fromBytes(imageBytes.data(), imageBytes.size());
  require(bool(imagePage), "decode image page");
  ImageBlock::clearRenderFailures();
  renderer.clearScreen();
  imagePage->render(renderer, 100, 0, 0);
  const auto imageBw = display.pixels;
  ReaderStripPainter imagePainter{renderer, *imagePage, 100, 0, 0, renderer.getScreenHeight(), false};
  HalFileTest::watchedReadPath = "image.px2";
  HalFileTest::watchedReads = 0;
  require(ReaderStripPainter::paint(&imagePainter, scratch.data(), 0, 80) == 0, "streamed image control");
  require(HalFileTest::watchedReads > 2, "image RAM slot unexpectedly resident");
  HalFileTest::watchedReadsBeforeFailure = HalFileTest::watchedReads + 2;
  HalFileTest::failWatchedReads = true;
  require(ReaderStripPainter::paint(&imagePainter, scratch.data(), 0, 80) != 0, "late cache failure not reported");
  require(display.pixels == imageBw, "late image failure changed retained BW");
  HalFileTest::failWatchedReads = false;
  ImageBlock::clearRenderFailures();
  require(ReaderStripPainter::paint(&imagePainter, scratch.data(), 0, 80) == 0, "image cache recovery");
  std::cout << "PASS streamed image cache failure with RAM slot unavailable preserves retained BW\n";
  std::array<uint8_t, 4096> window;
  window.fill(0xcd);
  ImageBlock::releaseRenderCache();
  ImageBlock::setRenderReadWindow(window.data(), window.size());
  imagePage->render(renderer, 100, 0, 0);
  require(window[0] == 0x1b, "streaming did not use borrowed window");
  ImageBlock::releaseRenderCache();  // Must not free the caller's storage.
  ImageBlock::setRenderReadWindow(nullptr, 0);
  window.fill(0x5a);
  imagePage->render(renderer, 100, 0, 0);
  require(std::all_of(window.begin(), window.end(), [](uint8_t b) { return b == 0x5a; }),
          "ended window loan was reused");
  ImageBlock::releaseRenderCache();
  ImageBlock::setRenderReadWindow(window.data(), 1);
  renderer.clearScreen();
  imagePage->render(renderer, 100, 0, 0);
  require(display.pixels == imageBw, "undersized loan did not fall back to owned window");
  require(window[0] == 0x5a, "undersized loan was written");
  ImageBlock::setRenderReadWindow(nullptr, 0);
  std::cout << "PASS borrowed image window: used, not freed, ended, undersized fallback\n";
  std::array<size_t, 4> streamingReads{};
  for (const auto heap : {24 * 1024, 200 * 1024}) {
    ESP.freeHeap = heap;
    for (int orientation = 0; orientation < 4; ++orientation) {
      renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
      ImageBlock::releaseRenderCache();
      for (const auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
        renderer.setRenderMode(mode);
        renderer.clearScreen();
        imagePage->render(renderer, 100, 0, 0);
        const auto full = display.pixels;
        std::vector<uint8_t> strips;
        HalFileTest::watchedReads = 0;
        for (uint16_t y = 0; y < display.getDisplayHeight(); y += 80) {
          const uint16_t rows = std::min<unsigned>(80, display.getDisplayHeight() - y);
          renderer.beginStripTarget(scratch.data(), y, rows);
          renderer.clearScreen();
          imagePage->render(renderer, 100, 0, 0);
          renderer.endStripTarget();
          strips.insert(strips.end(), scratch.data(), scratch.data() + rows * display.getDisplayWidthBytes());
        }
        require(std::equal(strips.begin(), strips.end(), full.begin()), "image row culling changed band pixels");
        if (heap == 24 * 1024 && mode == GfxRenderer::BW) streamingReads[orientation] = HalFileTest::watchedReads;
      }
    }
  }
  require(streamingReads[1] < streamingReads[0] && streamingReads[3] < streamingReads[2],
          "landscape row culling did not reduce cache reads");
  std::cout << "PASS production image row culling: all orientations, BW/gray planes, streaming/resident; reads=";
  for (auto reads : streamingReads) std::cout << reads << ' ';
  std::cout << '\n';

  std::cout << "PASS production page/plan/source: partial prewarm, " << initialReads
            << " reads, late font failure, frozen replays and recovery\n";
}

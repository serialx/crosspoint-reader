#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <Epub/SectionCodec.h>
#include <GfxRenderer.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <vector>

namespace allocations {
bool enabled = false;
size_t failAt = SIZE_MAX, requests = 0, live = 0;
void* pointers[8]{};
void removed(void* p) {
  for (auto& entry : pointers)
    if (entry && entry == p) {
      entry = nullptr;
      --live;
    }
}
}  // namespace allocations
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  if (allocations::enabled && allocations::requests++ == allocations::failAt) return nullptr;
  void* p = std::malloc(size ? size : 1);
  if (p && allocations::enabled) {
    for (auto& entry : allocations::pointers)
      if (!entry) {
        entry = p;
        ++allocations::live;
        return p;
      }
    std::abort();
  }
  return p;
}
void* operator new[](size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void operator delete(void* p) noexcept {
  allocations::removed(p);
  std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

static void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
static std::vector<uint8_t> sample() {
  uint8_t bytes[1024]{};
  XpPageBuilder builder{};
  require(xp_page_builder_init(&builder, bytes, sizeof(bytes)) == 0, "builder init");
  const char text[] = "word";
  const uint16_t offset = 0;
  const int16_t x = 4;
  const uint8_t style = 0;
  const uint32_t ranges[] = {3, 7};
  XpParaLine line{};
  line.text = {reinterpret_cast<const uint8_t*>(text), sizeof(text)};
  XpParaBlock block{};
  line.block = &block;
  line.offsets = &offset;
  line.positions = &x;
  line.styles = &style;
  line.sources = ranges;
  const XpParaSpan ruby{};
  line.ruby = &ruby;
  line.count = 1;
  line.paragraph_start = 0;
  require(xp_page_builder_line(&builder, 5, 9, &line) == 0, "builder line");
  size_t size = 0;
  require(xp_page_builder_finish(&builder, &size) == 0, "builder finish");
  return {bytes, bytes + size};
}
static void writeSection(const std::vector<uint8_t>& bytes, bool partial = false, int offsetDelta = 0) {
  HalFile file;
  require(file.open("cache/sections/0.bin", "wb"), "open section");
  auto header = sectioncodec::header(ReaderRenderSpec{}, 1);
  header.values[0] = partial ? XP_SEC_PARTIAL_VERSION : XP_SEC_VERSION;
  header.values[15] = XP_SEC_HEADER_SIZE + bytes.size();
  header.values[17] = header.values[15] + 4;
  header.values[18] = header.values[15] + 6;
  header.values[19] = header.values[15] + 8;
  require(sectioncodec::write(file, header), "write header");
  require(file.write(bytes.data(), bytes.size()) == bytes.size(), "write page");
  require(sectioncodec::writeValue(file, XP_SEC_U32, XP_SEC_HEADER_SIZE + offsetDelta), "write offset");
  require(sectioncodec::writeValue(file, XP_SEC_U16, 0), "write paragraph");
  require(sectioncodec::writeValue(file, XP_SEC_U16, 0), "write list item");
  require(sectioncodec::writeValue(file, XP_SEC_U32, 321), "write visible");
  if (partial) {
    require(sectioncodec::writeValue(file, XP_SEC_U32, 100), "partial consumed");
    require(sectioncodec::writeValue(file, XP_SEC_U32, 200), "partial total");
  }
}
int main(int argc, char** argv) {
  require(argc == 2, "mode");
  const auto dir = std::filesystem::temp_directory_path() / ("crosspoint-page-load-" + std::to_string(getpid()));
  std::filesystem::create_directories(dir / "cache/sections");
  const auto old = std::filesystem::current_path();
  std::filesystem::current_path(dir);
  auto epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  Section section(epub, 0, renderer);
  section.pageCount = 1;
  const auto bytes = sample();
  const std::string mode = argv[1];
  if (mode == "oom") {
    writeSection(bytes);
    for (size_t fail = 0; fail < 3; ++fail) {
      allocations::requests = 0;
      allocations::failAt = fail;
      allocations::enabled = true;
      auto page = section.loadPage(0);
      allocations::enabled = false;
      require(!page, "allocation failure must reject page");
      require(allocations::live == 0, "allocation failure leaked storage");
    }
    allocations::requests = 0;
    allocations::failAt = SIZE_MAX;
    allocations::enabled = true;
    auto page = section.loadPage(0);
    allocations::enabled = false;
    require(page && allocations::requests == 3 && allocations::live == 2, "page must retain only object + arena");
    require(page->elements.empty() && page->links.empty() && page->footnotes.empty(),
            "load materialized C++ containers");
    page.reset();
    require(allocations::live == 0, "page destruction leaked storage");
    for (size_t fail = 0; fail < 2; ++fail) {
      allocations::requests = 0;
      allocations::failAt = fail;
      allocations::enabled = true;
      auto fromBytes = Page::fromBytes(bytes.data(), bytes.size());
      allocations::enabled = false;
      require(!fromBytes && allocations::live == 0, "byte factory failure leaked storage");
    }
    auto input = bytes;
    auto fromBytes = Page::fromBytes(input.data(), input.size(), 42);
    require(bool(fromBytes), "byte factory");
    std::fill(input.begin(), input.end(), 0);
    const auto block = fromBytes->element(0).textBlock();
    require(std::string(block.wordText(0)) == "word" && fromBytes->visibleTextOffset == 42,
            "byte factory retained the caller input");
  } else if (mode == "malformed") {
    size_t cases = 0;
    const auto check = [&](const std::vector<uint8_t>& input) {
      writeSection(input);
      XpPageInfo info{};
      const bool expected = xp_page_arena_size(input.data(), input.size(), &info) == 0;
      auto page = section.loadPage(0);
      require(bool(page) == expected, "Section load disagrees with validated page bytes");
      ++cases;
    };
    for (size_t cut = 0; cut < bytes.size(); ++cut) check({bytes.begin(), bytes.begin() + cut});
    for (size_t i = 0; i < bytes.size(); ++i) {
      for (uint8_t bit : {uint8_t(1), uint8_t(128)}) {
        auto b = bytes;
        b[i] ^= bit;
        check(b);
      }
      for (uint32_t value : {0u, 1u, 10000u, 10001u, 65535u, UINT32_MAX}) {
        for (size_t width : {2u, 4u})
          if (i + width <= bytes.size()) {
            auto b = bytes;
            for (size_t j = 0; j < width; ++j) b[i + j] = value >> (j * 8);
            check(b);
          }
      }
    }
    for (int delta : {-1, int(bytes.size()), int(bytes.size()) + 4}) {
      writeSection(bytes, false, delta);
      require(!section.loadPage(0), "invalid LUT range accepted");
    }
    require(!section.loadPage(-1) && !section.loadPage(1), "page index bounds");
    std::cout << cases << " malformed/truncated cases\n";
  } else {
    writeSection(bytes, true);
    require(section.loadSectionFile(ReaderRenderSpec{}), "load partial header");
    require(section.isPartial(), "partial state");
    auto page = section.loadPage(0);
    require(page && page->visibleTextOffset == 321 && page->elementCount() == 1, "partial page load");
    const auto line = page->element(0);
    const auto text = line.textBlock();
    require(line.xPos == 5 && line.yPos == 9 && std::string(text.wordText(0)) == "word", "borrowed view");
    require(text.wordSourceRange(0).start == 3 && text.wordStartsParagraph(0), "source metadata");
  }
  std::filesystem::current_path(old);
  std::filesystem::remove_all(dir);
}

#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>

#include "../reader_lifecycle/physical_defaults.h"
#include "FrameChecks.h"
#include "PageViews.h"
#include "activities/reader/RustEpubPage.h"

extern "C" void* xp_epub_test_page_transfer(const XpAppResources*, const XpEpubLeaves*, XpAppText, uint8_t,
                                            void* (*)(XpEpubOwnedPage*, uint8_t, void*), void*);

void testPageFrame(const XpEpubPageView&, GfxRenderer&);
void testStripPageBorrow(const XpAppResources&, const XpEpubLeaves&, XpAppText, GfxRenderer&);

// Virtual UI methods are deliberately unavailable. This composition constructs
// and destroys the complete native children, without running their input loops.
void EpubReaderFootnoteSelectActivity::onEnter() { assert(false); }
XpUiEffect EpubReaderFootnoteSelectActivity::updateRust(const XpUiInput&) {
  assert(false);
  return {};
}
void EpubReaderFootnoteSelectActivity::applyRustEffect(const XpUiEffect&) { assert(false); }
void EpubReaderFootnoteSelectActivity::render(RenderLock&&) { assert(false); }
void DictionaryWordSelectActivity::onEnter() { assert(false); }
void DictionaryWordSelectActivity::loop() { assert(false); }
void DictionaryWordSelectActivity::render(RenderLock&&) { assert(false); }
XpUiPaint DictionaryWordSelectActivity::rustResources() {
  assert(false);
  return {};
}
void ClipSelectionActivity::onEnter() { assert(false); }
void ClipSelectionActivity::loop() { assert(false); }
void ClipSelectionActivity::render(RenderLock&&) { assert(false); }
bool ClipSelectionActivity::handleHomeGesture() {
  assert(false);
  return false;
}
XpUiPaint ClipSelectionActivity::rustResources() {
  assert(false);
  return {};
}
void ClipSelectionActivity::cancel() { assert(false); }

extern "C" void xp_epub_test_page_query(const XpEpubPageView*, const XpEpubLeaves*, uint16_t, uint16_t, int32_t,
                                        XpEpubPageInfo*, XpEpubElement*, XpEpubWord*, int32_t (*)[4]);

static void compareRustQueries(const Page& native, const XpEpubPageView& view, const XpEpubLeaves& leaves,
                               GfxRenderer& renderer) {
  XpEpubPageInfo info{};
  xp_epub_test_page_query(&view, &leaves, 0, 0, 24, &info, nullptr, nullptr, nullptr);
  assert(info.visible_offset == native.visibleTextOffset && info.elements == native.elementCount());
  assert(info.footnotes == native.footnoteCount() && info.links == native.linkCount());
  assert(info.images == native.hasImages() && info.images_need_decode == native.hasImagesNeedingDecode());
  for (uint16_t i = 0; i < native.elementCount(); ++i) {
    XpEpubElement element{};
    xp_epub_test_page_query(&view, &leaves, i, 0, 24, nullptr, &element, nullptr, nullptr);
    const auto source = native.element(i);
    assert(element.x == source.xPos && element.y == source.yPos && element.tag == source.getTag());
    if (source.getTag() != TAG_PageLine) {
      assert(!element.valid && !element.words);
      continue;
    }
    const auto block = source.textBlock();
    assert(element.valid == block.valid() && element.words == block.wordCount());
    for (uint16_t word = 0; word < element.words; ++word) {
      for (int32_t font : {0, 8, 24, 64, 999}) {
        XpEpubWord value{};
        int32_t metrics[4]{};
        xp_epub_test_page_query(&view, &leaves, i, word, font, nullptr, nullptr, &value, &metrics);
        assert(value.text.len == std::strlen(block.wordText(word)));
        assert(value.text.data[value.text.len] == 0 &&
               !std::memcmp(value.text.data, block.wordText(word), value.text.len));
        assert(value.x == block.wordXpos(word) && value.style == block.wordStyle(word));
        assert(value.character_spacing == block.getBlockStyle().characterSpacing);
        const auto range = block.wordSourceRange(word);
        assert(value.source_start == range.start && value.source_end == range.end);
        const int lineFont = TextBlock::renderFontId(renderer, block.blockFontId(font), font);
        assert(metrics[0] == lineFont);
        assert(metrics[1] == TextBlock::renderFontId(renderer, block.wordFontId(word, font), font));
        assert(metrics[2] == block.wordYOffset(renderer, font, word));
        assert(metrics[3] == block.getRubyShift(renderer.getFontAscenderSize(lineFont)));
      }
    }
  }
}

namespace {
struct Allocation {
  void* pointer;
  size_t bytes;
};
std::array<Allocation, 3> rustLive{};
std::array<Allocation, 5> nativeLive{};
size_t rustRequests, nativeRequests, releases, cases, transactions;
size_t rustFail = SIZE_MAX, nativeFail = SIZE_MAX;
bool factoryActive;
GfxRenderer renderer;
MappedInputManager input;
XpAppResources resources = appTestResources();
XpEpubLeaves leaves = unexpectedEpubLeaves();

size_t liveCount() {
  size_t count = 0;
  for (const auto& a : rustLive) count += a.pointer != nullptr;
  return count;
}
void reset() {
  assert(liveCount() == 0 && !factoryActive);
  for (const auto& a : nativeLive) assert(!a.pointer);
  rustRequests = nativeRequests = releases = 0;
  rustFail = nativeFail = SIZE_MAX;
}
uint8_t* allocate(XpAppMemoryDomain domain, size_t bytes) {
  assert(domain == XP_APP_MEMORY_DEFAULT);
  if (rustRequests++ == rustFail) return nullptr;
  void* p = std::malloc(bytes);
  assert(p);
  for (auto& a : rustLive)
    if (!a.pointer) {
      a = {p, bytes};
      return static_cast<uint8_t*>(p);
    }
  std::abort();
}
void release(XpAppMemoryDomain domain, uint8_t* pointer) {
  assert(domain == XP_APP_MEMORY_DEFAULT && !factoryActive);
  assert(!frameChecks::loan && !frameChecks::window);
  for (auto& a : rustLive)
    if (a.pointer == pointer) {
      a = {};
      ++releases;
      std::free(pointer);
      return;
    }
  std::abort();
}
struct Request {
  unsigned kind;
  const uint8_t* wire;
  size_t size;
};
void* factory(XpEpubOwnedPage* pages, uint8_t count, void* raw) {
  auto& request = *static_cast<Request*>(raw);
  assert(liveCount() == count && releases == 0);
  std::array<void*, 3> owners{};
  for (uint8_t i = 0; i < count; ++i) {
    owners[i] = pages[i].owner;
    assert(owners[i] && pages[i].destroy);
    auto native = Page::fromBytes(request.wire, request.size, i);
    assert(native);
    Page borrowed(Page::Borrowed{}, pages[i].view.arena, pages[i].view.size, i);
    comparePage(*native, borrowed);
    compareRustQueries(*native, pages[i].view, leaves, renderer);
    testPageFrame(pages[i].view, renderer);
    assert(liveCount() == count && releases == 0);
    for (uint8_t mode = 0; mode < 3; ++mode) {
      renderer.boxPaints.clear();
      renderer.lastLineY = -1;
      renderer.drawnLineCount = 0;
      switch (static_cast<PageRenderMode>(mode)) {
        case PageRenderMode::All:
          native->render(renderer, 24, 7, 11);
          break;
        case PageRenderMode::Images:
          native->renderImages(renderer, 24, 7, 11);
          break;
        case PageRenderMode::Placeholders:
          native->renderWithImagePlaceholders(renderer, 24, 7, 11);
          break;
      }
      const auto boxes = renderer.boxPaints;
      const auto y = renderer.lastLineY, lines = renderer.drawnLineCount;
      renderer.boxPaints.clear();
      renderer.lastLineY = -1;
      renderer.drawnLineCount = 0;
      epubPage::paint(renderer, pages[i].view, 24, 7, 11, mode);
      assert(renderer.boxPaints == boxes && renderer.lastLineY == y && renderer.drawnLineCount == lines);
    }
    for (uint16_t element = 0; element < native->elementCount(); ++element) {
      if (native->element(element).getTag() != TAG_PageImage) continue;
      XpPageElement image{};
      assert(!xp_page_element(pages[i].view.arena, pages[i].view.size, element, &image));
      assert(image.first.data[image.first.len] == 0 && image.second.data[image.second.len] == 0);
      auto block = native->element(element).imageBlock();
      assert(epubPage::imageNeedsDecode({image.first.data, image.first.len}, {image.second.data, image.second.len},
                                        image.width, image.height) == block.needsDecode());
    }
  }
  factoryActive = true;
  void* result = nullptr;
  switch (request.kind) {
    case 0:
      assert(count == 1);
      result = epubPage::footnote(renderer, input, pages[0], 7, 11);
      break;
    case 1: {
      assert(count == 1);
      const uint8_t word[] = {'w', 'o', 'r', 'd'};
      result = epubPage::dictionary(renderer, input, pages[0], 7, 11, {word, sizeof(word)});
      break;
    }
    case 2: {
      XpEpubSelectionArgs args{};
      args.left = 7;
      args.top = 11;
      args.x = args.y = -1;
      result = epubPage::selection(renderer, input, pages, count, args);
      break;
    }
  }
  factoryActive = false;
  assert(liveCount() == count && releases == 0);
  for (uint8_t i = 0; i < count; ++i) assert(pages[i].owner == (result ? nullptr : owners[i]));
  if (result) {
    auto* owner = static_cast<OwnedLegacyActivity*>(result);
    assert(owner->activity);
    if (request.kind == 0) assert(dynamic_cast<EpubReaderFootnoteSelectActivity*>(owner->activity.get()));
    if (request.kind == 1) assert(dynamic_cast<DictionaryWordSelectActivity*>(owner->activity.get()));
    if (request.kind == 2) assert(dynamic_cast<ClipSelectionActivity*>(owner->activity.get()));
  }
  ++transactions;
  return result;
}
void exercise(const uint8_t* wire, size_t size, bool faults) {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  reset();
  testStripPageBorrow(resources, leaves, {wire, size}, renderer);
  assert(liveCount() == 0 && releases > 0);
#endif
  for (unsigned kind = 0; kind < 3; ++kind) {
    for (uint8_t count = 1; count <= (kind == 2 ? 3 : 1); ++count) {
      Request request{kind, wire, size};
      reset();
      auto* child = static_cast<OwnedLegacyActivity*>(
          xp_epub_test_page_transfer(&resources, &leaves, {wire, size}, count, factory, &request));
      assert(child && liveCount() == count && releases == 0);
      const auto attempts = nativeRequests;
      assert(attempts == count + 2u);  // Each facade, wrapper, and child object.
      delete child;
      assert(liveCount() == 0 && releases == count);
      if (faults) {
        for (size_t fail = 0; fail < attempts; ++fail) {
          reset();
          nativeFail = fail;
          assert(!xp_epub_test_page_transfer(&resources, &leaves, {wire, size}, count, factory, &request));
          assert(nativeRequests == fail + 1 && releases == count && liveCount() == 0);
        }
        for (size_t fail = 0; fail < count; ++fail) {
          reset();
          rustFail = fail;
          const auto prior = transactions;
          assert(!xp_epub_test_page_transfer(&resources, &leaves, {wire, size}, count, factory, &request));
          assert(transactions == prior && rustRequests == fail + 1 && releases == fail && liveCount() == 0);
        }
      }
      ++cases;
    }
  }
  reset();
  // The dictionary viewer's native Pages remain borrowed by its selection child.
  std::vector<std::unique_ptr<Page>> nativePages;
  nativePages.push_back(Page::fromBytes(wire, size));
  auto* retained = nativePages[0].get();
  {
    ClipSelectionActivity child(renderer, input, std::span<const std::unique_ptr<Page>>(nativePages), 7, 11,
                                ClipSelectionActivity::Selection{});
  }
  assert(nativePages[0].get() == retained);
  assert(retained->visibleTextOffset == 0);
}
}  // namespace

void pageOwnerLog(const char*, const char*, ...) {}
void* operator new(size_t bytes) {
  if (void* p = std::malloc(bytes ? bytes : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
  if (frameChecks::allocating && frameChecks::requests++ == frameChecks::failAt) return nullptr;
  if (factoryActive && nativeRequests++ == nativeFail) return nullptr;
  void* p = std::malloc(bytes ? bytes : 1);
  if (p && frameChecks::allocating) {
    for (auto& slot : frameChecks::live)
      if (!slot) {
        slot = p;
        return p;
      }
    std::abort();
  }
  if (p && factoryActive) {
    for (auto& a : nativeLive)
      if (!a.pointer) {
        a = {p, bytes};
        return p;
      }
    std::abort();
  }
  return p;
}
void* operator new[](size_t bytes, const std::nothrow_t& tag) noexcept { return ::operator new(bytes, tag); }
void operator delete(void* pointer) noexcept {
  for (auto& slot : frameChecks::live)
    if (pointer && slot == pointer) {
      assert(!frameChecks::loan);
      slot = nullptr;
      break;
    }
  for (auto& a : nativeLive)
    if (pointer && a.pointer == pointer) {
      a = {};
      break;
    }
  std::free(pointer);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

int main(int argc, char** argv) {
  leaves.font_ready = [](int32_t font, int32_t fallback) -> int32_t {
    return TextBlock::renderFontId(renderer, font, fallback);
  };
  leaves.font_ascent = [](int32_t font) -> int32_t { return epubPage::fontAscent(renderer, font); };
  leaves.image_needs_decode = epubPage::imageNeedsDecode;
  resources.buffer_allocate = allocate;
  resources.buffer_release = release;
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 1; };
  leaves.log = [](uint8_t, XpAppText, XpAppText) {};
  for (int font : {0, 8, 24, 64, 999})
    assert(epubPage::fontAscent(renderer, font) == renderer.getFontAscenderSize(font));
  const uint8_t empty[6]{};
  exercise(empty, sizeof(empty), true);
  assert(argc >= 2);
  std::ifstream corpus(argv[1], std::ios::binary);
  uint32_t size, visible;
  size_t pages = 0;
  while (corpus.read(reinterpret_cast<char*>(&size), 4)) {
    assert(corpus.read(reinterpret_cast<char*>(&visible), 4));
    std::vector<uint8_t> wire(size);
    assert(corpus.read(reinterpret_cast<char*>(wire.data()), size));
    exercise(wire.data(), wire.size(), pages++ < 10);
  }
  assert(corpus.eof() && pages > 0);
  reset();
  std::cout << "{\"pages\":" << pages << ",\"cases\":" << cases << ",\"transactions\":" << transactions;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  assert(frameChecks::imageBorrows > 0 && frameChecks::allocationFailures[3] > 0);
  std::cout << ",\"strip_borrows\":" << frameChecks::borrows << ",\"active_sources\":" << frameChecks::successes
            << ",\"image_borrows\":" << frameChecks::imageBorrows
            << ",\"source_rejections\":" << frameChecks::sourceRejections << ",\"allocation_failures\":[";
  for (size_t i = 0; i < frameChecks::allocationFailures.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << frameChecks::allocationFailures[i];
  }
  std::cout << ']';
#endif
  std::cout << "}\n";
}

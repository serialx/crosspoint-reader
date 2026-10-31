#include <cstdlib>
#include <iostream>

#include "FootnoteReader.h"
#include "activities/reader/EpubReaderFootnoteSelectActivity.h"
static void require(bool ok, const char* message) {
  if (!ok) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
static std::unique_ptr<Page> page(size_t count) {
  uint8_t wire[2048]{};
  XpPageBuilder builder{};
  require(!xp_page_builder_init(&builder, wire, sizeof(wire)), "init");
  for (size_t i = 0; i < count; ++i) {
    uint8_t number[32] = {}, href[256] = {};
    number[0] = '1' + i;
    href[0] = '#';
    href[1] = '1' + i;
    require(!xp_page_builder_footnote(&builder, number, href), "note");
  }
  // Reverse link order to prove matching uses href, not parallel indices.
  for (size_t i = count; i > 0; --i) {
    uint8_t href[256] = {'#', static_cast<uint8_t>('0' + i)};
    XpPageLink link{{href, sizeof(href)}, static_cast<int16_t>(i * 20), 30, 8, 10};
    require(!xp_page_builder_link(&builder, &link), "link");
  }
  size_t size;
  require(!xp_page_builder_finish(&builder, &size), "finish");
  auto result = Page::fromBytes(wire, size);
  require(bool(result), "decoded page");
  return result;
}
static CheckedPageMetadata* metadataToClear;
static void clearMetadataAfterUnlock() {
  RenderLock::requireUnlocked();
  metadataToClear->clear();
}
static void rejectionRecovery() {
  for (bool first : {false, true})
    for (bool buildFailure : {false, true}) {
      GfxRenderer renderer;
      MappedInputManager input;
      EpubReaderFootnoteSelectActivity selector(renderer, input, page(2), 0, 0);
      Activity& activity = selector;
      activity.onEnter();
      if (first) activity.render({});
      renderer.failFootnoteBuild = buildFailure;
      renderer.paintOk = buildFailure;
      activity.render({});
      for (auto button : {MappedInputManager::Button::Confirm, MappedInputManager::Button::Power}) {
        input.released = button;
        activity.loop();
        require(!activity.finished, "rejected highlight must block activation");
      }
      renderer.failFootnoteBuild = false;
      renderer.paintOk = true;
      renderer.snapshotOk = false;
      activity.render({});
      input.released = MappedInputManager::Button::Confirm;
      activity.loop();
      require(activity.finished, "successful highlight without a snapshot must publish");
      EpubReaderFootnoteSelectActivity back(renderer, input, page(2), 0, 0);
      Activity& cancel = back;
      cancel.onEnter();
      renderer.paintOk = false;
      cancel.render({});
      input.released = MappedInputManager::Button::Back;
      cancel.loop();
      require(cancel.finished && cancel.result.isCancelled, "rejected highlight keeps Back");
    }
}
int main() {
  rejectionRecovery();
  GfxRenderer renderer;
  MappedInputManager input;
  EpubReaderFootnoteSelectActivity selector(renderer, input, page(2), 5, 7);
  Activity& activity = selector;
  activity.onEnter();
  activity.render({});
  require(renderer.highlightNumber == "1" && renderer.highlightX == 23 && renderer.highlightY == 35,
          "initial highlight and href matching");
  input.pressed = MappedInputManager::Button::ScreenDown;
  activity.loop();
  activity.render({});
  require(renderer.highlightNumber == "2" && renderer.highlightX == 43, "next highlight");
  input.pressed = MappedInputManager::Button::ScreenUp;
  activity.loop();
  activity.render({});
  require(renderer.highlightNumber == "1", "previous highlight");
  input.pressed = MappedInputManager::Button::ScreenRight;
  activity.loop();
  input.pressed = MappedInputManager::Button::None;
  input.released = MappedInputManager::Button::Confirm;
  activity.loop();
  require(activity.finished && std::get<FootnoteResult>(activity.result.data).href == "#2", "confirm href");
  EpubReaderFootnoteSelectActivity back(renderer, input, page(2), 0, 0);
  Activity& cancel = back;
  cancel.onEnter();
  input.released = MappedInputManager::Button::Back;
  cancel.loop();
  require(cancel.finished && cancel.result.isCancelled, "back cancels");
  FootnoteSection section;
  EpubReaderActivity reader(section, renderer, input);
  require(reader.currentPageMetadata.assign(*page(1)), "single footnote metadata");
  metadataToClear = &reader.currentPageMetadata;
  RenderLock::afterUnlock = clearMetadataAfterUnlock;
  reader.openFootnoteSelect(false);
  require(reader.navigatedHref == "#1" && reader.isFootnote && !reader.child && section.loads == 0,
          "single footnote must navigate directly without reloading or opening selector");
  reader.navigatedHref.clear();
  require(!reader.testMenuAvailability() && !reader.testMoreAvailability(), "empty menu availability");
  require(reader.currentPageMetadata.assign(*page(2)), "link metadata");
  require(reader.testMenuAvailability() && reader.testMoreAvailability(), "populated menu availability");
  input.tapped = true;
  input.touchX = 46;
  input.touchY = 40;
  RenderLock::afterUnlock = clearMetadataAfterUnlock;
  reader.testLinkTap();
  require(reader.navigatedHref == "#2", "link href survives metadata destruction after unlock");
  reader.navigatedHref.clear();
  reader.testLinkTap();
  require(reader.navigatedHref.empty(), "cleared metadata has no link target");
  input.tapped = false;
  require(reader.currentPageMetadata.assign(*page(2)), "multiple footnote metadata");
  section.loaded = page(2);
  reader.openFootnoteSelect(true);
  require(reader.child && section.loads == 1 && reader.navigatedHref.empty(), "multiple notes open selector");
  reader.child->onEnter();
  input.released = MappedInputManager::Button::Confirm;
  reader.child->loop();
  reader.completion(reader.child->result);
  require(reader.navigatedHref == "#1" && reader.isFootnote, "selector result navigates reader");
  EpubReaderFootnoteSelectActivity power(renderer, input, page(2), 0, 0);
  Activity& powerActivity = power;
  powerActivity.onEnter();
  input.released = MappedInputManager::Button::None;
  input.pressed = MappedInputManager::Button::ScreenRight;
  powerActivity.loop();
  powerActivity.render({});
  input.pressed = MappedInputManager::Button::None;
  input.released = MappedInputManager::Button::Power;
  powerActivity.loop();
  require(powerActivity.finished && std::get<FootnoteResult>(powerActivity.result.data).href == "#2", "power href");
  reader.completion(cancel.result);
  require(reader.menuRequests == 1, "cancel reopens requested reader menu");
}

#include <I18nKeys.h>

#include <cassert>
#include <cstdio>

#include "overlay_actions_session_fixture.h"

extern "C" {
XpEpubBuildPopup xp_epub_test_build_popup_prepare(void*, uint8_t);
uint8_t xp_epub_test_build_popup_pending(void*);
uint8_t xp_epub_test_publication_render(void*, void (*)(void*, void*), void*);
uint8_t xp_epub_test_publication_frame_present(void*);
}

static void* owner;
static int native;
static unsigned renderDepth, modelDepth, paints, geometryReads, translations;
static bool framebuffer = true;
static const uint8_t indexingLabel[] = "Indexing";
static void leaf() { assert(owner && renderDepth == 1 && modelDepth == 0); }
struct RenderLock {
  RenderLock() {
    assert(renderDepth == 0 && modelDepth == 0);
    ++renderDepth;
  }
  ~RenderLock() {
    assert(renderDepth == 1 && modelDepth == 0);
    --renderDepth;
  }
};
static const XpAppResources resources = [] {
  auto table = overlayUnexpectedResources();
  table.translate = [](uint16_t key) -> XpAppText {
    leaf();
    assert(key == static_cast<uint16_t>(StrId::STR_INDEXING));
    ++translations;
    return {indexingLabel, sizeof(indexingLabel) - 1};
  };
  table.popup_draw = [](XpAppText text) -> XpUiRect {
    leaf();
    assert(text.data == indexingLabel && text.len == sizeof(indexingLabel) - 1);
    ++paints;
    return {};
  };
  return table;
}();
static const XpAppRuntime runtime = [] {
  auto table = overlayUnexpectedRuntime();
  table.model_lock = [] {
    assert(modelDepth == 0);
    ++modelDepth;
  };
  table.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
  };
  return table;
}();
static const LegacyEpubLeaves leaves = [] {
  auto table = overlayLegacyLeaves();
  table.physical.render_geometry = [](XpEpubGeometry* out) {
    leaf();
    assert(out);
    ++geometryReads;
    *out = {};
    out->has_framebuffer = framebuffer;
  };
  table.physical.book_set_extractor = [](void* handle) {
    if (handle)
      leaves.book.book_install_extractor(handle);
    else
      leaves.book.book_clear_extractor();
  };
  return table;
}();
struct RetainingSection {
  XpEpubBuildPopup callback = &OverlayUnexpected<XpEpubBuildPopup>::call;
  void* context = nullptr;
  unsigned calls = 0;
  void retain(XpEpubBuildPopup value, void* ownerContext) {
    assert(value && ownerContext == owner);
    callback = value;
    context = ownerContext;
  }
  void invoke() {
    leaf();
    assert(context == owner && callback);
    ++calls;
    callback(context);
  }
};
static XpEpubTestOverlayObservation observe() {
  XpEpubTestOverlayObservation out{};
  xp_epub_test_overlay_observe(owner, &out);
  return out;
}
int main() {
  XpEpubTestOverlaySeed seed{};
  seed.native = &native;
  seed.context = &native;
  seed.counter = 8;
#define UNEXPECTED(field) seed.field = &OverlayUnexpected<decltype(seed.field)>::call
  UNEXPECTED(home_reset);
  UNEXPECTED(current_toc);
  UNEXPECTED(build_more);
  UNEXPECTED(paint);
  UNEXPECTED(push);
  UNEXPECTED(a4);
  UNEXPECTED(xteink);
  UNEXPECTED(settle);
  UNEXPECTED(discard);
  UNEXPECTED(update);
#undef UNEXPECTED
  owner = xp_epub_test_overlay_create_legacy_book(&seed, &leaves.physical, &runtime, &resources, &leaves.book);
  assert(owner && observe().counter == 8);
  RetainingSection section;
  {
    RenderLock lock;
    section.retain(xp_epub_test_build_popup_prepare(owner, 1), owner);
    assert(!xp_epub_test_publication_frame_present(owner));
    const auto painted = xp_epub_test_publication_render(
        owner,
        [](void* context, void* frame) {
          leaf();
          assert(frame && xp_epub_test_publication_frame_present(owner));
          auto& section = *static_cast<RetainingSection*>(context);
          section.invoke();
          assert(paints == 1 && !xp_epub_test_build_popup_pending(owner));
        },
        &section);
    assert(painted && !xp_epub_test_publication_frame_present(owner));
    assert(observe().counter == 1 && paints == 1 && geometryReads == 1 && translations == 1);
    assert(!xp_epub_test_build_popup_pending(owner));
  }
  {
    RenderLock lock;
    assert(!xp_epub_test_publication_frame_present(owner) && !xp_epub_test_build_popup_pending(owner));
    std::fputs("background no-frame callback\n", stderr);
    std::fflush(stderr);
    section.invoke();
    assert(!xp_epub_test_publication_frame_present(owner));
    assert(paints == 1 && geometryReads == 1 && translations == 1 && observe().counter == 1);
  }
  {
    RenderLock lock;
    framebuffer = false;
    assert(xp_epub_test_build_popup_prepare(owner, 1) == section.callback);
    assert(section.context == owner && !xp_epub_test_publication_frame_present(owner));
    section.invoke();
    assert(paints == 1 && translations == 1 && geometryReads == 2);
    assert(xp_epub_test_build_popup_pending(owner) && observe().counter == 1);
    assert(!xp_epub_test_publication_frame_present(owner) && section.calls == 3);
  }
  {
    RenderLock lock;
    xp_epub_test_overlay_destroy(owner);
    owner = nullptr;
  }
  assert(renderDepth == 0 && modelDepth == 0);
  std::puts("retained build popup: three real callback cases; frame expired; same owner context");
}

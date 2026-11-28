static GfxRenderer* target;
static MappedInputManager* mapped;
// ACTUAL_OWNED_TEXT
static CrossPointPosition position(const XpEpubPosition& value) { return {value.spine}; }
#include <xp_book.h>
#include <xp_ffi.h>
#include <xp_page_paint.h>

#include <string_view>
#include <vector>

#include "../hal_contract/Contract.h"
#include "../hal_fake_strict.h"
extern "C" void xp_epub_test_image_stream(void*, uint8_t (*)(void*, const uint8_t*, size_t, XpBookSink, void*));
extern "C" void xp_page_image_test_paint(const XpPageTarget*, const XpPageImage*);
static const uint8_t recoveryPng[] = {
    137, 80,  78,  71,  13,  10,  26,  10,  0,   0,  0,   13, 73, 72,  68, 82,  0,  0,   0,   16, 0,   0,   0,   16,
    8,   0,   0,   0,   0,   58,  152, 160, 189, 0,  0,   0,  60, 73,  68, 65,  84, 120, 156, 93, 193, 193, 0,   0,
    32,  0,   192, 192, 33,  132, 16,  66,  8,   33, 132, 16, 66, 8,   33, 132, 16, 66,  8,   33, 132, 16,  66,  8,
    253, 119, 7,   22,  132, 40,  36,  33,  11,  69, 168, 66, 19, 186, 48, 132, 41, 44,  97,  11, 71,  184, 194, 147,
    15,  47,  119, 120, 1,   97,  1,   187, 246, 0,  0,   0,  0,  73,  69, 78,  68, 174, 66,  96, 130};
static std::vector<uint8_t> recoveredSource;
struct RecoveryFile {
  size_t offset;
  uint8_t kind;
  bool initialized;
  bool hasImpl;
};
extern "C" size_t xp_native_page_image_file(XpPageImageFile* cell, XpPageImageFileOp op, const uint8_t* path,
                                            uint8_t* bytes, size_t value) {
  auto& f = *reinterpret_cast<RecoveryFile*>(cell);
  static_assert(sizeof(f) <= sizeof(*cell));
  assert(lockDepth == 1 && !modelDepth);
  switch (op) {
    case XP_IMAGE_FILE_INIT:
      f = {0, 0, true, false};
      return 0;
    case XP_IMAGE_FILE_DROP:
      assert(f.initialized);
      f.initialized = false;
      return 0;
    case XP_IMAGE_FILE_FREE_HEAP:
    case XP_IMAGE_FILE_LARGEST_BLOCK:
    case XP_IMAGE_FILE_FREE_INTERNAL_HEAP:
      return 100000;
    case XP_IMAGE_FILE_NOW:
      return 0;
    case XP_IMAGE_FILE_YIELD:
      return 0;
    case XP_IMAGE_FILE_OPEN_WRITE:
    case XP_IMAGE_FILE_OPEN_CACHE_WRITE:
    case XP_IMAGE_FILE_OPEN:
    case XP_IMAGE_FILE_OPEN_PNG:
      assert(f.initialized);
      f.hasImpl = true;
      break;
    case XP_IMAGE_FILE_EXISTS:
    case XP_IMAGE_FILE_IS_OPEN:
      assert(f.initialized);
      break;
    default:
      hal_fake::requireImpl(f.hasImpl, "image operation");
  }
  switch (op) {
    case XP_IMAGE_FILE_EXISTS:
      return imageSourceExists;
    case XP_IMAGE_FILE_OPEN_WRITE:
      f.kind = 1;
      f.offset = 0;
      recoveredSource.clear();
      return 1;
    case XP_IMAGE_FILE_OPEN_CACHE_WRITE:
      return 0;
    case XP_IMAGE_FILE_OPEN:
    case XP_IMAGE_FILE_OPEN_PNG:
      f.offset = 0;
      if (!imageSourceExists || std::string(reinterpret_cast<const char*>(path)).ends_with(".px2")) {
        f.kind = 0;
        return 0;
      }
      f.kind = 2;
      return 1;
    case XP_IMAGE_FILE_IS_OPEN:
      return f.kind != 0;
    case XP_IMAGE_FILE_READ:
    case XP_IMAGE_FILE_READ_RAW: {
      assert(f.kind == 2);
      const size_t n = std::min(value, recoveredSource.size() - f.offset);
      std::memcpy(bytes, recoveredSource.data() + f.offset, n);
      f.offset += n;
      return n;
    }
    case XP_IMAGE_FILE_SEEK:
      if (value > recoveredSource.size()) return 0;
      f.offset = value;
      return 1;
    case XP_IMAGE_FILE_POSITION:
      return f.offset;
    case XP_IMAGE_FILE_SIZE:
      assert(f.kind == 2);
      return recoveredSource.size();
    case XP_IMAGE_FILE_FLUSH:
      assert(f.kind == 1);
      return 0;
    case XP_IMAGE_FILE_CLOSE:
      f.kind = 0;
      return 0;
    case XP_IMAGE_FILE_REMOVE:
      assert(false && "PNG cache cannot start in this fixture");
      return 0;
    default:
      assert(false && "unexpected recovery file operation");
      return 0;
  }
}
extern "C" uint8_t xp_native_page_image_write(void* raw, const uint8_t* bytes, size_t length) {
  auto& f = *static_cast<RecoveryFile*>(raw);
  assert(f.initialized && f.kind == 1);
  recoveredSource.insert(recoveredSource.end(), bytes, bytes + length);
  return 1;
}
extern "C" void xp_native_page_image_codec_log(const XpPageImageCodecLog*, uint32_t, uint32_t, uint32_t, uint32_t,
                                               uint32_t) {}
extern "C" void xp_native_page_image_log(void*, const XpPageImage*, const uint8_t*, uint32_t event, int32_t, int32_t) {
  assert(event != 12);
  if (event == 13) ++imageDecodes;
}
static void renderRecoveredImage() {
  RenderLock lock;
  xp_epub_test_image_stream(
      ImageBlock::extractCtx,
      [](void* owner, const uint8_t* path, size_t length, XpBookSink sink, void* context) -> uint8_t {
        assert(ImageBlock::extractFn && std::string(reinterpret_cast<const char*>(path), length) == "OPS/unseen.png");
        if (!ImageBlock::extractFn(owner, "OPS/unseen.png", "/cache/unseen.png")) return 0;
        return sink(context, recoveryPng, sizeof(recoveryPng));
      });
  uint8_t pixels[32]{};
  const XpPageTarget target{
      {pixels, sizeof(pixels), 16, 16, 2, 0, 16, {0, 0, 16, 16}, 3, 0, 0, 0, 0}, nullptr, nullptr, 0};
  const XpPageImage image{reinterpret_cast<const uint8_t*>("/cache/unseen.png"),
                          reinterpret_cast<const uint8_t*>("OPS/unseen.png"),
                          17,
                          14,
                          0,
                          0,
                          16,
                          16};
  xp_page_image_test_paint(&target, &image);
  assert(imageSourceExists && imageExtractions == 1 && imageDecodes == 1);
  xp_epub_test_image_stream(nullptr, nullptr);
  xp_assets_image_reset(true);
}

static void installNativeChildren(EpubReaderActivity& owner) {
  target = &owner.renderer;
  mapped = &owner.mappedInput;
  childLeaves.book.section.page.child_selection = /* ACTUAL_SELECTION */;
  childLeaves.book.section.page.child_dictionary = /* ACTUAL_DICTIONARY */;
  childLeaves.book.section.page.child_footnote = /* ACTUAL_FOOTNOTE */;
  childLeaves.book.children.child_clippings = /* ACTUAL_CLIPPINGS */;
  xp_epub_test_legacy_children(activeChild->rust, &childLeaves.book.children);
  childLeaves.physical.child_sync = /* ACTUAL_SYNC */;
  static const auto install = +/* ACTUAL_INSTALL_EXTRACTOR */;
  childLeaves.book.book_install_extractor = [](void* raw) {
    assert(lockDepth == 1 && !modelDepth && raw == expectedExtractor);
    ++extractorInstalls;
    install(raw);
  };
  childLeaves.book.book_clear_extractor = [] {
    assert(lockDepth == 1 && !modelDepth);
    ++extractorClears;
    ImageBlock::setExtractor(nullptr, nullptr);
  };
  expectedExtractor = owner.epub.get();
  install(expectedExtractor);
}

extern "C" size_t xp_test_allocation_attempts();
extern "C" size_t xp_test_live_allocations();
extern "C" void xp_test_fail_allocation_after(size_t);

static void armOuterFailure(int after) {
  assert(after >= -1 && after <= 1);
  failOuterAfter = after;
  outerAttempts = 0;
  failedOuterAttempt = -1;
  outerSizes[0] = outerSizes[1] = 0;
}

static void resetFaults() {
  armOuterFailure(-1);
  finishCount = 0;
  xp_test_fail_allocation_after(SIZE_MAX);
  assert(livePages == 0 && xp_test_live_allocations() == 0);
  loads = 0;
  loadLimit = 99;
  titleBytes = 128;
  toc = 1;
  saveOk = allocationOk = true;
  CLIPPINGS.clips = true;
  checkExtractor = imageSourceExists = false;
  extractorInstalls = extractorClears = imageExtractions = imageDecodes = 0;
}

static void outerAllocation(int kind, int failAfter) {
  resetFaults();
  const bool fail = failAfter >= 0;
  EpubReaderActivity owner;
  {
    ChildSessionBridge session(owner);
    installNativeChildren(owner);
    armOuterFailure(failAfter);
    switch (kind) {
      case 0:
        owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Clip);
        break;
      case 1: {
        armOuterFailure(-1);
        owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Lookup);
        assert(owner.child);
        auto callback = std::move(owner.callback);
        owner.child.reset();
        armOuterFailure(failAfter);
        ActivityResult result;
        ClippingResult clipping;
        clipping.action = ClippingResult::Action::Lookup;
        result.data = clipping;
        callback(result);
        break;
      }
      case 2:
        owner.currentPageMetadata.notes = {{"a#one"}, {"b#two"}};
        owner.openFootnoteSelect(false);
        break;
      case 3:
        owner.openClippings();
        break;
      case 4:
      case 5:
        KOREADER_STORE.credentials = kind == 5;
        owner.launchKOReaderSync();
        assert(finishCount == (fail && kind == 5 ? 1 : 0));
        assert(bool(owner.epub) == (kind == 4));
        break;
    }
    const size_t childSizes[] = {sizeof(ClipSelectionActivity),
                                 sizeof(DictionaryWordSelectActivity),
                                 sizeof(EpubReaderFootnoteSelectActivity),
                                 sizeof(EpubReaderClippingListActivity),
                                 sizeof(KOReaderSyncActivity),
                                 sizeof(KOReaderSyncActivity)};
    assert(outerAttempts == (failAfter == 0 ? 1 : 2) && "wrapper precedes nullable child allocation");
    assert(failedOuterAttempt == failAfter && "the selected allocation stage must fail");
    assert(outerSizes[0] == sizeof(OwnedLegacyActivity));
    assert(outerSizes[1] == (failAfter == 0 ? 0 : childSizes[kind]));
    if (fail) {
      assert(!owner.child && !activityManager.child && livePages == 0);
      assert(xp_test_live_allocations() == 1 && "failed child must release Rust captures");
    } else {
      assert(owner.child || activityManager.child);
    }
    failOuterAfter = -1;
    owner.child.reset();
    owner.callback = {};
    activityManager.child.reset();
    assert(livePages == 0);
  }
  assert(xp_test_live_allocations() == 0 && !lockDepth && !modelDepth);
}

static void syncCaptureFailure(size_t after) {
  resetFaults();
  KOREADER_STORE.credentials = true;
  EpubReaderActivity owner;
  {
    ChildSessionBridge session(owner);
    installNativeChildren(owner);
    xp_test_fail_allocation_after(after);
    checkExtractor = true;
    owner.launchKOReaderSync();
    assert(xp_test_allocation_attempts() == after + 1);
    xp_test_fail_allocation_after(SIZE_MAX);
    assert(owner.epub && bool(owner.section) == (after < 2));
    assert(!owner.child && !activityManager.child && finishCount == 0 && outerAttempts == 0);
    assert(xp_test_live_allocations() == 1 && "failed sync capture must release partial strings");
    requireExtractor();
    assert(extractorInstalls == (after == 2 ? 1u : 0u));
    assert(extractorClears == (after == 2 ? 1u : 0u));
    if (after == 2) renderRecoveredImage();
    assert(xp_test_live_allocations() == 1);
    checkExtractor = false;
  }
  assert(xp_test_live_allocations() == 0 && !lockDepth && !modelDepth);
}

int main() {
  {
    RenderLock lock;
    XpPageImageFile image{};
    hal_contract::rejects([&] { xp_native_page_image_file(&image, XP_IMAGE_FILE_CLOSE, nullptr, nullptr, 0); });
    xp_native_page_image_file(&image, XP_IMAGE_FILE_INIT, nullptr, nullptr, 0);
    hal_contract::rejects([&] { xp_native_page_image_file(&image, XP_IMAGE_FILE_CLOSE, nullptr, nullptr, 0); });
    assert(xp_native_page_image_file(&image, XP_IMAGE_FILE_OPEN, reinterpret_cast<const uint8_t*>("missing.px2"),
                                     nullptr, 0) == 0);
    xp_native_page_image_file(&image, XP_IMAGE_FILE_CLOSE, nullptr, nullptr, 0);
    xp_native_page_image_file(&image, XP_IMAGE_FILE_CLOSE, nullptr, nullptr, 0);
    xp_native_page_image_file(&image, XP_IMAGE_FILE_DROP, nullptr, nullptr, 0);
  }
  for (int kind = 0; kind < 6; ++kind) {
    outerAllocation(kind, -1);
    outerAllocation(kind, 0);
    outerAllocation(kind, 1);
  }
  for (size_t after = 0; after < 3; ++after) syncCaptureFailure(after);
  std::puts("native child allocation: 18 outer cases; three Rust sync capture failures; no lost Pages or captures");
}

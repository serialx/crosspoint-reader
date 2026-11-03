static GfxRenderer* target;
static MappedInputManager* mapped;
// ACTUAL_OWNED_TEXT
static CrossPointPosition position(const XpEpubPosition& value) { return {value.spine}; }
#include <xp_ffi.h>

#include <string_view>

// IMAGE_IO_ENUM
struct {
  bool exists(const char* path) {
    assert(std::string(path) == "/cache/unseen.png");
    return imageSourceExists;
  }
} Storage;
struct RecoveryImage {
  std::string_view path = "/cache/unseen.png", source = "OPS/unseen.png";
  void* extractContext = ImageBlock::extractCtx;
  ImageBlock::ExtractFn extract = ImageBlock::extractFn;
};
static size_t recoveryImageIo(void* raw, uint32_t op, const uint8_t*, uint8_t*, size_t) {
  auto& self = *static_cast<RecoveryImage*>(raw);
  assert(lockDepth == 1 && !modelDepth);
  switch (static_cast<ImageIo>(op)) {
    // ACTUAL_EXTRACTION_IO
    case ImageIo::OpenCache:
      return 0;
    case ImageIo::Close:
      return 0;
    case ImageIo::FreeHeap:
    case ImageIo::LargestBlock:
      return 100000;
    case ImageIo::SourceSize:
      return imageSourceExists ? 64 : SIZE_MAX;
    case ImageIo::Decode:
      assert(imageSourceExists && imageExtractions == 1);
      ++imageDecodes;
      return 2;
    default:
      assert(false && "unexpected uncached image IO");
      return 0;
  }
}
static void renderRecoveredImage() {
  RenderLock lock;
  RecoveryImage image;
  const XpAssetImageCallbacks callbacks{
      &image, recoveryImageIo,
      [](void*, XpImageTarget*) { assert(false && "uncached decode does not stream cached pixels"); },
      [](void*, uint32_t, int32_t, int32_t) {}};
  const XpAssetImageConfig config{0, 0, 16, 16, 600, 800, false, true};
  const auto result = xp_assets_image_render(reinterpret_cast<const uint8_t*>(image.path.data()), image.path.size(),
                                             &config, &callbacks);
  assert(result == 1 && imageExtractions == 1 && imageDecodes == 1);
  xp_assets_image_reset(true);
}

static void installNativeChildren(EpubReaderActivity& owner) {
  target = &owner.renderer;
  mapped = &owner.mappedInput;
  childLeaves.physical.child_selection = /* ACTUAL_SELECTION */;
  childLeaves.physical.child_dictionary = /* ACTUAL_DICTIONARY */;
  childLeaves.physical.child_footnote = /* ACTUAL_FOOTNOTE */;
  childLeaves.physical.child_clippings = /* ACTUAL_CLIPPINGS */;
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
  for (int kind = 0; kind < 6; ++kind) {
    outerAllocation(kind, -1);
    outerAllocation(kind, 0);
    outerAllocation(kind, 1);
  }
  for (size_t after = 0; after < 3; ++after) syncCaptureFailure(after);
  std::puts("native child allocation: 18 outer cases; three Rust sync capture failures; no lost Pages or captures");
}

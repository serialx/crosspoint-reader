static GfxRenderer* target;
static MappedInputManager* mapped;
// ACTUAL_OWNED_TEXT
static CrossPointPosition position(const XpEpubPosition& value) { return {value.spine}; }

static void installNativeChildren(EpubReaderActivity& owner) {
  target = &owner.renderer;
  mapped = &owner.mappedInput;
  childLeaves.child_selection = /* ACTUAL_SELECTION */;
  childLeaves.child_dictionary = /* ACTUAL_DICTIONARY */;
  childLeaves.child_footnote = /* ACTUAL_FOOTNOTE */;
  childLeaves.child_clippings = /* ACTUAL_CLIPPINGS */;
  childLeaves.child_sync = /* ACTUAL_SYNC */;
}

extern "C" size_t xp_test_allocation_attempts();
extern "C" size_t xp_test_live_allocations();
extern "C" void xp_test_fail_allocation_after(size_t);

static void resetFaults() {
  failOuter = false;
  outerAttempts = finishCount = 0;
  xp_test_fail_allocation_after(SIZE_MAX);
  assert(livePages == 0 && xp_test_live_allocations() == 0);
  loads = 0;
  loadLimit = 99;
  titleBytes = 128;
  toc = 1;
  saveOk = allocationOk = true;
  CLIPPINGS.clips = true;
}

static void outerAllocation(int kind, bool fail) {
  resetFaults();
  EpubReaderActivity owner;
  {
    ChildSessionBridge session(owner);
    installNativeChildren(owner);
    failOuter = fail;
    switch (kind) {
      case 0:
        owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Clip);
        break;
      case 1: {
        failOuter = false;
        owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Lookup);
        assert(owner.child);
        auto callback = std::move(owner.callback);
        owner.child.reset();
        failOuter = fail;
        outerAttempts = 0;
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
    assert(outerAttempts == 1 && "actual nullable outer allocation must be exercised");
    if (fail) {
      assert(!owner.child && !activityManager.child && livePages == 0);
      assert(xp_test_live_allocations() == 1 && "failed child must release Rust captures");
    } else {
      assert(owner.child || activityManager.child);
    }
    failOuter = false;
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
    owner.launchKOReaderSync();
    assert(xp_test_allocation_attempts() == after + 1);
    xp_test_fail_allocation_after(SIZE_MAX);
    assert(owner.epub && bool(owner.section) == (after < 2));
    assert(!owner.child && !activityManager.child && finishCount == 0 && outerAttempts == 0);
    assert(xp_test_live_allocations() == 1 && "failed sync capture must release partial strings");
  }
  assert(xp_test_live_allocations() == 0 && !lockDepth && !modelDepth);
}

int main() {
  for (int kind = 0; kind < 6; ++kind) {
    outerAllocation(kind, false);
    outerAllocation(kind, true);
  }
  for (size_t after = 0; after < 3; ++after) syncCaptureFailure(after);
  std::puts("native child allocation: 12 outer cases; three Rust sync capture failures; no lost Pages or captures");
}

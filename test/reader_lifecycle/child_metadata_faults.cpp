extern "C" size_t xp_test_allocation_attempts();
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_live_bytes();
extern "C" void xp_test_fail_allocation_after(size_t);

static void metadataFaultCase(size_t titleLength, bool failMetadata, bool failChild, int resultMode) {
  xp_test_fail_allocation_after(SIZE_MAX);
  assert(xp_test_live_allocations() == 0 && xp_test_live_bytes() == 0 && livePages == 0);
  titleBytes = titleLength;
  allocationOk = !failChild;
  loads = 0;
  loadLimit = 99;
  toc = 1;
  {
    EpubReaderActivity owner;
    {
      ChildSessionBridge session(owner);
      const auto baselineAllocations = xp_test_live_allocations();
      const auto baselineBytes = xp_test_live_bytes();
      assert(baselineAllocations == 1);
      xp_test_fail_allocation_after(failMetadata ? 0 : SIZE_MAX);
      owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Clip);
      assert(xp_test_allocation_attempts() == 1 && "one packed metadata allocation");
      xp_test_fail_allocation_after(SIZE_MAX);
      if (failMetadata || failChild) {
        assert(!owner.child && !owner.callback && livePages == 0);
        assert(xp_test_live_allocations() == baselineAllocations);
        assert(xp_test_live_bytes() == baselineBytes);
      } else {
        assert(owner.child && owner.callback && livePages == 3);
        const size_t metadataBytes =
            titleLength + std::string("Chapter title with enough bytes for a real string allocation").size() +
            std::string("Author with enough bytes for a real string allocation").size() + 3;
        assert(xp_test_live_allocations() == baselineAllocations + 1);
        assert(xp_test_live_bytes() == baselineBytes + metadataBytes);
        owner.child.reset();
        assert(livePages == 0);
        if (resultMode < 2) {
          auto callback = std::move(owner.callback);
          ActivityResult result;
          result.isCancelled = resultMode == 0;
          result.data = ClippingResult{};
          callback(result);
          assert(xp_test_live_allocations() == baselineAllocations && "result releases captured metadata");
          assert(xp_test_live_bytes() == baselineBytes && "result releases packed bytes");
        } else {
          owner.callback = {};
          // Pending metadata must be released even when shutdown delivers no result.
        }
      }
    }
    assert(xp_test_live_allocations() == 0 && xp_test_live_bytes() == 0);
  }
  assert(livePages == 0 && !activeChild && !lockDepth && !modelDepth);
}

int main() {
  for (const size_t length : {size_t(16), size_t(128), size_t(8192), size_t(65536)}) {
    metadataFaultCase(length, true, false, 0);
    metadataFaultCase(length, false, true, 0);
    for (int result = 0; result < 3; ++result) metadataFaultCase(length, false, false, result);
  }
  std::puts("metadata allocation: 20 cases; one allocation; failure, cancel, result and teardown release");
}

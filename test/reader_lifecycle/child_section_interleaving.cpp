extern "C" void xp_epub_test_child_drop_section(void*);
extern "C" void xp_epub_test_overlay_reject(void*);

enum class Change { Reject, Drop, Spine };

static void selectionInterleaving(int pause, Change change) {
  allocationOk = true;
  loadLimit = 99;
  loads = 0;
  titleBytes = 128;
  toc = 1;
  assert(livePages == 0);
  EpubReaderActivity owner;
  ChildSessionBridge session(owner);
  childRuntime.model_lock = [] {
    modelMutex.lock();
    assert(!modelDepth);
    ++modelDepth;
  };
  childRuntime.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
    modelMutex.unlock();
  };
  {
    std::lock_guard<std::mutex> guard(pauseMutex);
    pauseAt = pause;
    blocked = released = false;
  }
  auto task = std::async(std::launch::async, [&] {
    workerThread = true;
    lockOrdinal = 0;
    owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Clip);
  });
  {
    std::unique_lock<std::mutex> guard(pauseMutex);
    assert(pauseCondition.wait_for(guard, std::chrono::seconds(5), [] { return blocked; }));
  }
  {
    RenderLock guard;
    switch (change) {
      case Change::Reject:
        xp_epub_test_overlay_reject(session.rust);
        break;
      case Change::Drop:
        xp_epub_test_child_drop_section(session.rust);
        break;
      case Change::Spine:
        xp_epub_test_child_spine(session.rust, 7);
        break;
    }
  }
  {
    std::lock_guard<std::mutex> guard(pauseMutex);
    released = true;
    pauseCondition.notify_all();
  }
  assert(task.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
  task.get();
  assert(!owner.child && !owner.callback && "rejected selection must not publish a child");
  assert(loads == (pause == 1 ? 0 : 1) && livePages == 0);
  assert(!lockDepth && !modelDepth);
}

static void confirmedResultAfterSectionLoss() {
  allocationOk = true;
  loadLimit = 99;
  loads = 0;
  titleBytes = 128;
  CLIPPINGS.result = ClippingStore::AddResult::Added;
  exportOk = true;
  EpubReaderActivity owner;
  ChildSessionBridge session(owner);
  owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Clip);
  assert(owner.child && livePages == 3);
  auto callback = std::move(owner.callback);
  owner.child.reset();
  {
    RenderLock guard;
    xp_epub_test_overlay_reject(session.rust);
    xp_epub_test_child_drop_section(session.rust);
  }
  ActivityResult result;
  result.data = ClippingResult{};
  callback(result);
  assert(!owner.section && !owner.child && livePages == 0);
  assert(owner.showClippingMessage && owner.clippingMessage == StrId::STR_CLIPPING_SAVED);
}

int main() {
  selectionInterleaving(1, Change::Reject);
  selectionInterleaving(3, Change::Reject);
  selectionInterleaving(3, Change::Drop);
  selectionInterleaving(3, Change::Spine);
  confirmedResultAfterSectionLoss();
  std::puts("child Section: four blocked acquisitions; confirmed result survives rejection and Section loss");
}

#include <xp_ui_koreader.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#define tr(name) name
constexpr const char* STR_SECTION_PREFIX = "Section ";
constexpr const char* STR_PAGE_OVERALL_FORMAT = "Page %d, %.2f%% overall";
constexpr const char* STR_PAGE_TOTAL_OVERALL_FORMAT = "Page %d/%d, %.2f%% overall";
constexpr const char* STR_DEVICE_FROM_FORMAT = "  From: %s";
static void waitFor(const std::atomic<bool>& flag) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!flag) {
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
}
static thread_local bool inModelLock = false;
static std::atomic<bool> destroyed{false};
static bool longTitle = false;
struct AllocationTrace {
  size_t live = 0, peak = 0;
  int events[16]{};
  unsigned count = 0;
  void record(int bytes) {
    assert(count < 16);
    events[count++] = bytes;
    live += bytes;
    if (live > peak) peak = live;
  }
};
static AllocationTrace* trace = nullptr;
struct alignas(std::max_align_t) AllocationHeader {
  size_t size;
  AllocationTrace* owner;
};
void* operator new(size_t size) {
  auto* header = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + size));
  if (!header) throw std::bad_alloc();
  header->size = size;
  header->owner = trace;
  if (trace) {
    assert(!inModelLock);
    trace->record(static_cast<int>(size));
  }
  return header + 1;
}
void operator delete(void* pointer) noexcept {
  if (!pointer) return;
  auto* header = static_cast<AllocationHeader*>(pointer) - 1;
  if (header->owner) {
    assert(!inModelLock);
    header->owner->record(-static_cast<int>(header->size));
  }
  std::free(header);
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, size_t) noexcept { ::operator delete(pointer); }
struct Epub {
  ~Epub() {
    assert(!inModelLock);
    destroyed = true;
  }
  int getTocIndexForSpineIndex(int) {
    assert(!destroyed && !inModelLock);
    return 0;
  }
  struct Item {
    std::string title;
  };
  Item getTocItem(int) {
    assert(!destroyed && !inModelLock);
    return {longTitle ? std::string(4096, 'x') : std::string("Remote chapter")};
  }
};
class KOReaderSyncActivity {
 public:
  std::mutex mutex;
  struct ModelLock {
    KOReaderSyncActivity& owner;
    explicit ModelLock(KOReaderSyncActivity& o) : owner(o) {
      owner.mutex.lock();
      assert(!inModelLock);
      inModelLock = true;
    }
    ~ModelLock() {
      inModelLock = false;
      owner.mutex.unlock();
    }
  };
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  Epub* paintingEpub = nullptr;
  XpKoState model{}, painting{};
  struct {
    int spineIndex = 0, pageNumber = 2, totalPages = 10;
  } remotePosition, localPosition;
  struct {
    float percentage = .75f;
    std::string device = "Reader";
  } remoteProgress;
  struct {
    float percentage = .25f;
  } localProgress;
  std::string remoteChapter;
  char localChapterFallback[32]{}, remoteValue[64]{}, localValue[64]{}, deviceValue[80]{};
  void releaseEpub();
  void snapshotResources();
  void prepareText();
};
#include "book-methods.inc"
struct Context {
  KOReaderSyncActivity& owner;
  std::atomic<bool> waiting{false}, renderDone{false}, released{false};
};
static uint32_t call(void* raw, uint32_t operation, uint32_t a, uint32_t) {
  auto& c = *static_cast<Context*>(raw);
  switch (operation) {
    case XP_KO_PUBLISH: {
      KOReaderSyncActivity::ModelLock lock(c.owner);
      c.owner.model.phase = a;
      break;
    }
    case XP_KO_REPAINT:
      if (a == 2) {
        c.waiting = true;
        waitFor(c.renderDone);
      }
      break;
    case XP_KO_RELEASE_BOOK:
      assert(c.renderDone);
      c.owner.releaseEpub();
      c.released = true;
      assert(destroyed);
      {
        KOReaderSyncActivity::ModelLock lock(c.owner);
        c.owner.painting = c.owner.model;
        c.owner.snapshotResources();
        assert(c.owner.painting.phase == XP_KO_UPLOADING && !c.owner.paintingEpub);
      }
      break;
  }
  return 0;
}
int main() {
  KOReaderSyncActivity owner;
  owner.model.phase = XP_KO_RESULT;
  Context context{owner};
  std::atomic<bool> borrowed{false}, finishRender{false};
  std::thread render([&] {
    {
      KOReaderSyncActivity::ModelLock lock(owner);
      owner.painting = owner.model;
      owner.snapshotResources();
      assert(owner.epub.use_count() == 1);  // Snapshot must not touch the refcount.
    }
    borrowed = true;
    waitFor(finishRender);
    owner.prepareText();
    assert(owner.remoteChapter == "Remote chapter");
    context.renderDone = true;
  });
  waitFor(borrowed);
  auto upload = std::async(std::launch::async, [&] {
    XpKoOptions options{};
    XpKoServices services{&context, call};
    xp_ui_koreader_upload(&options, &services);
  });
  waitFor(context.waiting);
  assert(!context.released && !destroyed);
  finishRender = true;
  render.join();
  upload.get();
  assert(context.released && destroyed);
  // A new render snapshots the published phase, never the stale Result phase.
  {
    KOReaderSyncActivity::ModelLock lock(owner);
    owner.painting = owner.model;
    owner.snapshotResources();
    assert(owner.painting.phase == XP_KO_UPLOADED && !owner.paintingEpub);
  }
  owner.prepareText();
  assert(owner.remoteChapter.empty());

  destroyed = false;
  longTitle = true;
  KOReaderSyncActivity repeated;
  repeated.model.phase = XP_KO_RESULT;
  AllocationTrace original, candidate;
  trace = &original;
  for (int frame = 0; frame < 3; ++frame) {
    const std::string chapter = repeated.epub->getTocItem(0).title;
    assert(chapter.size() == 4096);
  }
  trace = &candidate;
  for (int frame = 0; frame < 3; ++frame) {
    {
      KOReaderSyncActivity::ModelLock lock(repeated);
      repeated.painting = repeated.model;
      repeated.snapshotResources();
    }
    repeated.prepareText();
    assert(repeated.remoteChapter.size() == 4096);
  }
  repeated.painting.phase = XP_KO_UPLOADING;
  repeated.prepareText();
  trace = nullptr;
  std::printf("long-title original peak=%zu candidate peak=%zu requested bytes\n", original.peak, candidate.peak);
  for (auto* record : {&original, &candidate}) {
    for (unsigned i = 0; i < record->count; ++i) std::printf("%+d ", record->events[i]);
    std::puts("");
  }
  std::fflush(stdout);
  assert(original.peak > 2048 && candidate.peak == original.peak);
  assert(original.live == 0 && candidate.live == 0 && original.count == candidate.count);
  for (unsigned i = 0; i < original.count; ++i) assert(original.events[i] == candidate.events[i]);
}

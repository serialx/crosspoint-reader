#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "clipping_owner_fixture.h"
#include "clipping_store/reader_fixture.h"
#include "clipping_store/reader_storage.h"
#include "owner_tables.h"

extern "C" {
uint32_t xp_epub_test_clipping_snapshot(void*, uint32_t, XpClippingRecord*, uint8_t*);
uint8_t xp_epub_test_clipping_try_text(void*, uint32_t);
uint8_t xp_epub_test_clipping_try_unload(void*);
void xp_epub_test_clipping_teardown(void*);
}

namespace {
std::mutex renderMutex, modelMutex, pauseMutex;
std::condition_variable paused;
thread_local int renderDepth = 0, modelDepth = 0;
thread_local bool track = false;
bool waiting = false, resume = false, failWrite = false, sawWrite = false;
unsigned readerIo = 0;
std::vector<char> order;

XpAppText wire(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
void* lock() {
  assert(!renderDepth && !modelDepth);
  renderMutex.lock();
  ++renderDepth;
  if (track) order.push_back('L');
  return reinterpret_cast<void*>(uintptr_t(1));
}
void unlock(void*) {
  assert(renderDepth == 1 && !modelDepth);
  if (track) order.push_back('U');
  --renderDepth;
  renderMutex.unlock();
}
void io() {
  assert(!renderDepth && !modelDepth);
  if (!track) ++readerIo;
}
void writing(const char* name) {
  assert(!renderDepth && !modelDepth);
  if (!track) return;
  const std::string path(name);
  if (path.ends_with(".deleted")) order.push_back('J');
  if (!path.ends_with(".tmp") || sawWrite) return;
  sawWrite = true;
  order.push_back('W');
  std::unique_lock<std::mutex> guard(pauseMutex);
  waiting = true;
  paused.notify_all();
  assert(paused.wait_for(guard, std::chrono::seconds(10), [] { return resume; }));
  if (failWrite) fake::failWrite = 0;
}

void run(bool deleting, bool failure) {
  fake::reset();
  auto leaves = retirement_fixture::leavesDefaults();
  auto runtime = retirement_fixture::modelRuntime();
  auto resources = retirement_fixture::resourcesDefaults();
  auto legacy = unexpectedLegacySectionServices();
  runtime.model_lock = [] {
    assert(!modelDepth);
    modelMutex.lock();
    ++modelDepth;
  };
  runtime.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
    modelMutex.unlock();
  };
  runtime.render_lock = lock;
  runtime.render_unlock = unlock;
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  leaves.book_set_extractor = [](void* raw) { assert(!raw && renderDepth == 1 && !modelDepth); };
  clipping_fixture::StorageAdapter storage(leaves, resources);
  storage.quietLog = true;
  storage.io = io;
  XpEpubTestClippingSeed seed{};
  seed.native = &seed;
  void* owner = xp_epub_test_clipping_create_legacy_section(&seed, &leaves, &runtime, &resources, &legacy);
  assert(owner);
  clipping_fixture::seedCount(owner, 0);
  const std::string book = "/interleaving.epub", chapter = "Chapter", original = "old", addition = "new";
  assert(xp_epub_test_clipping_load(owner, wire(book)));
  XpClippingRecord first{};
  first.page_count = 1;
  first.signature = 77;
  first.end_offset = 3;
  first.id[0] = 'a';
  assert(xp_epub_test_clipping_insert(owner, &first, wire(chapter), {wire(original), &original}) == 0);
  std::string path;
  for (const auto& [name, value] : fake::files)
    if (name.ends_with(".bin")) path = name;
  assert(!path.empty());
  const auto bytes = fake::files.at(path)->bytes;
  XpClippingRecord before{};
  uint8_t valid = 0;
  assert(xp_epub_test_clipping_snapshot(owner, 0, &before, &valid) == 1 && valid);

  order.clear();
  waiting = resume = sawWrite = false;
  failWrite = failure;
  storage.writing = writing;
  auto writer = std::async(std::launch::async, [&] {
    track = true;
    const uint8_t result =
        deleting ? xp_epub_test_clipping_remove(owner, 0)
                 : xp_epub_test_clipping_insert(owner, &first, wire(chapter), {wire(addition), &addition});
    track = false;
    return result;
  });
  {
    std::unique_lock<std::mutex> guard(pauseMutex);
    assert(paused.wait_for(guard, std::chrono::seconds(10), [] { return waiting; }));
  }
  // A real renderer can acquire the same mutex while physical persistence waits.
  assert(renderMutex.try_lock());
  renderMutex.unlock();
  lock();
  XpClippingRecord during{};
  assert(xp_epub_test_clipping_snapshot(owner, 0, &during, &valid) == 1 && valid);
  assert(std::memcmp(&before, &during, sizeof(before)) == 0);
  unlock(nullptr);
  readerIo = 0;
  assert(!xp_epub_test_clipping_try_text(owner, 0));
  assert(!xp_epub_test_clipping_remove(owner, 0));
  assert(xp_epub_test_clipping_insert(owner, &first, wire(chapter), {wire(addition), &addition}) == 2);
  assert(!xp_epub_test_clipping_try_unload(owner));
  assert(readerIo == 0 && fake::files.at(path)->bytes == bytes);
  {
    std::lock_guard<std::mutex> guard(pauseMutex);
    resume = true;
  }
  paused.notify_all();
  const uint8_t result = writer.get();
  assert(result == (deleting ? uint8_t(!failure) : uint8_t(failure ? 2 : 0)));
  const std::vector<char> expected =
      deleting ? (failure ? std::vector<char>{'J', 'L', 'U', 'W'} : std::vector<char>{'J', 'L', 'U', 'W', 'L', 'U'})
               : (failure ? std::vector<char>{'L', 'U', 'W'} : std::vector<char>{'L', 'U', 'W', 'L', 'U'});
  assert(order == expected);
  const auto count = xp_epub_test_clipping_snapshot(owner, 0, &during, &valid);
  assert(count == (failure ? 1u : deleting ? 0u : 2u));
  if (failure) {
    assert(valid && std::memcmp(&before, &during, sizeof(before)) == 0);
    assert(fake::files.at(path)->bytes == bytes);
    storage.writing = nullptr;
    assert(xp_epub_test_clipping_insert(owner, &first, wire(chapter), {wire(addition), &addition}) == 0);
  }
  storage.writing = nullptr;
  order.clear();
  readerIo = 0;
  track = true;
  xp_epub_test_clipping_teardown(owner);
  track = false;
  assert((order == std::vector<char>{'L', 'U'}));
  assert(!xp_epub_test_clipping_snapshot(owner, 0, &during, &valid) && !valid);
  assert(!renderDepth && !modelDepth && storage.files.empty());
  xp_epub_test_clipping_destroy(owner);
}
}  // namespace
int main() {
  for (bool remove : {false, true})
    for (bool fail : {false, true}) run(remove, fail);
  std::puts(
      "clipping physical sequence: four exact success/failure routes, old metadata during I/O, atomic publication and "
      "teardown");
}

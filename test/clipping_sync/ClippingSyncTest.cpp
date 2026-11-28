#include "clipping_store/reader_fixture.h"
#include "clipping_store/reader_storage.h"
#include "owner_tables.h"
#include "reader_lifecycle/read_folder_session_fixture.h"
static void* clippingReader;
#include <atomic>
static std::atomic<unsigned> clippingRenderDepth{0};
#include <ArduinoJson.h>
#include <ClippingStore.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <SecureHttpClient.h>

#include <cassert>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "BuildScratch.h"
#include "clippings/ClippingSync.h"
#include "outer_lock.h"

namespace {
bool syncInProgress = false;
constexpr const char* book = "/book.epub";
std::vector<uint8_t> framebuffer(52272);

void reset() {
  ClippingStoreFacade::unload();
  fake::reset();
  fake::syncHeap = 68000;
  fake::syncBufferAllocated = false;
  fake::uploads.clear();
  fake::requests = 0;
  fake::failHttpAt = -1;
  fake::incomplete = false;
  fake::remoteResponse = "{\"cursor\":0,\"more\":false,\"items\":[]}";
  syncCredentials.enabled = true;
}
void released() {
  assert(!clippingRenderDepth && !syncLoanActive);
  buildscratch::lend(framebuffer.data(), framebuffer.size());
  auto* scratch = buildscratch::claim(32768);
  assert(scratch == framebuffer.data());
  buildscratch::release(scratch);
  buildscratch::reclaim();
}
bool runSync() {
  const auto takes = syncLockTakes.load();
  const auto gives = syncLockGives.load();
  bool result;
  {
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
    RenderLock outer;
    SyncFrameBufferLoan loan(framebuffer.data(), framebuffer.size());
#endif
    syncInProgress = true;
    result = clippingSync::run(book, "hash");
    syncInProgress = false;
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
    assert(syncLockTakes == takes + 1 && syncLockGives == gives);
    assert(clippingRenderDepth == 1 && syncLoanActive);
#endif
  }
  assert(!clippingRenderDepth && !syncLoanActive);
  assert(syncLockTakes - takes == syncLockGives - gives);
  assert(ClippingStoreFacade::clippingCount() == 0);  // RAII unload on success and every failure.
  return result;
}
void differentTaskHolder() {
  reset();
  RenderLock outer;
  assert(syncRuntime->render_owned_by_current_task_retire_p18());
  std::promise<void> attempted;
  auto future = attempted.get_future();
  std::atomic<bool> completed{false};
  syncLockAttempt = &attempted;
  std::thread worker([&] {
    assert(!syncRuntime->render_owned_by_current_task_retire_p18());
    assert(ClippingStoreFacade::loadForBook(book, "", "", "epub"));
    ClippingStoreFacade::unload();
    completed = true;
  });
  future.wait();
  assert(!completed && clippingRenderDepth == 1);
  outer.unlock();
  worker.join();
  assert(completed && !clippingRenderDepth);
}
void add(const std::string& text) {
  assert(ClippingStoreFacade::loadForBook(book, "Book", "Author", "epub"));
  XpClippingRecord row{};
  row.page_count = 1;
  row.word_count = 1;
  row.end_offset = 3;
  row.pending = 1;
  row.signature = 1;
  const auto wire = [](const std::string& value) -> XpAppText {
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
  };
  assert(xp_epub_test_clipping_insert(clippingReader, &row, wire("Chapter"), {wire(text), &text}) == 0);
  ClippingStoreFacade::unload();
}
void facadeLockContracts(bool locked) {
  reset();
  add("text");
  fake::add(book, "epub");
  std::optional<RenderLock> outer;
  if (locked) outer.emplace();
  auto mutate = [&](auto action) {
    const auto takes = syncLockTakes.load();
    const auto gives = syncLockGives.load();
    action();
    assert(syncLockTakes == takes + unsigned(!locked));
    assert(syncLockGives == gives + unsigned(!locked));
    assert(syncOwnsLock == locked);
  };
  mutate([&] { assert(ClippingStoreFacade::loadForBook(book, "", "", "epub")); });
  mutate([] { assert(ClippingStoreFacade::prepareSync()); });
  mutate([] { assert(ClippingStoreFacade::finishUploads()); });
  Clipping row;
  assert(ClippingStoreFacade::clippingAt(0, row));
  mutate([&] { assert(ClippingStoreFacade::applyRemote(row, "text", false)); });
  const auto takes = syncLockTakes.load();
  const auto gives = syncLockGives.load();
  assert(ClippingStoreFacade::clippingCount() == 1);
  std::string text;
  assert(ClippingStoreFacade::readClippingText(0, text) && text == "text");
  uint32_t offset = 0;
  char id[65]{};
  assert(ClippingStoreFacade::nextDeletion(offset, id) && !id[0]);
  assert(ClippingStoreFacade::finishDeletions());
  assert(syncLockTakes == takes && syncLockGives == gives);
  mutate([] { ClippingStoreFacade::unload(); });
  const auto beforeMove = syncLockTakes.load();
  const auto beforeGive = syncLockGives.load();
  assert(ClippingStoreFacade::moveBook(book, "/renamed.epub"));
  assert(ClippingStoreFacade::deleteForFilePath("/renamed.epub", "epub"));
  assert(syncLockTakes == beforeMove && syncLockGives == beforeGive);
  assert(syncOwnsLock == locked);
}
void maximumTextRoundTrip() {
  reset();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
  fake::syncHeap = 100000;
#endif
  const std::string text(CLIPPING_TEXT_MAX, '\t');
  add(text);
  JsonDocument response;
  response["cursor"] = 1;
  response["more"] = false;
  auto remote = response["items"].to<JsonArray>().add<JsonObject>();
  remote["id"] = "remote";
  remote["deleted"] = 0;
  remote["revision"] = 1;
  for (auto key : {"spine", "start_page", "end_page", "pages", "start_word", "end_word", "words", "created_at",
                   "layout_signature"})
    remote[key] = 1;
  std::string escapedText = "\"";
  for (size_t i = 0; i < CLIPPING_TEXT_MAX; ++i) escapedText += "\\u0009";
  escapedText += '"';
  remote["text"] = serialized(escapedText);
  fake::remoteResponse.clear();
  serializeJson(response, fake::remoteResponse);
  assert(fake::remoteResponse.size() > 24 * 1024);
  assert(runSync());
  released();
  assert(fake::uploads.size() == 1);
  assert(fake::uploads.front().size() > 8 * 1024);
  JsonDocument upload;
  assert(!deserializeJson(upload, fake::uploads.front()));
  assert(upload["items"][0]["text"].as<std::string>() == text);
  assert(ClippingStoreFacade::loadForBook(book, "", "", "epub"));
  assert(ClippingStoreFacade::clippingCount() == 2);
  std::string stored;
  assert(ClippingStoreFacade::readClippingText(1, stored));
  assert(stored == text);
  ClippingStoreFacade::unload();
}
}  // namespace

int main() {
  auto leaves = retirement_fixture::leavesDefaults();
  auto runtime = retirement_fixture::modelRuntime();
  auto resources = retirement_fixture::resourcesDefaults();
  runtime.render_lock = syncTake;
  runtime.render_owned_by_current_task_retire_p18 = []() -> uint8_t { return syncOwnsLock; };
  runtime.render_unlock = syncGive;
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  clipping_fixture::StorageAdapter storage(leaves, resources);
  storage.quietLog = true;
  resources.path_exists = [](XpAppText name) -> uint8_t {
    clipping_fixture::StorageAdapter::check();
    return ClippingTestStorage::getInstance().exists(clipping_fixture::StorageAdapter::text(name).c_str());
  };
  leaves.path_rename = [](XpAppText from, XpAppText to) -> uint8_t {
    clipping_fixture::StorageAdapter::check();
    return ClippingTestStorage::getInstance().rename(clipping_fixture::StorageAdapter::text(from).c_str(),
                                                     clipping_fixture::StorageAdapter::text(to).c_str());
  };
  storage.io = [] {
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
    if (syncInProgress) assert(syncOwnsLock && clippingRenderDepth == 1 && syncLoanActive);
#endif
  };
  fake::onRequest = [] {
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
    assert(syncOwnsLock && clippingRenderDepth == 1 && syncLoanActive);
#endif
  };
  XpAppLegacy legacy{};
  XpAppPlatform platform{&legacy, &resources, &runtime, &leaves};
  XpAppCompatibility compatibility{};
  assert(xp_app_init(&platform, &compatibility) == XP_APP_OK);
  clippingReader = xp_epub_test_read_folder_create(&leaves, &runtime, &resources);
  assert(clippingReader);

  syncRuntime = &runtime;
  facadeLockContracts(false);
  facadeLockContracts(true);
  differentTaskHolder();
  maximumTextRoundTrip();
  for (int failure = 0; failure < 4; ++failure) {
    reset();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
    fake::syncHeap = 100000;
#endif
    if (failure == 0) fake::failHttpAt = 0;
    if (failure == 1) fake::incomplete = true;
    if (failure == 2) fake::remoteResponse.assign(32768, 'x');
    if (failure == 3) {
      add("upload must fail");
      fake::failHttpAt = 1;
    }
    assert(!runSync());
    released();
  }
  reset();
  fake::failAlloc = 0;
  assert(!runSync());
  released();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
  reset();
  assert(!runSync());  // Preserve the S3 heap guard.
#endif
  buildscratch::reclaim();
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
  reset();
  {
    RenderLock outer;
    assert(!clippingSync::run(book, "hash"));  // Missing loan must fail safely.
    assert(clippingRenderDepth == 1);
  }
  assert(fake::requests == 0);
#endif
  xp_epub_test_read_folder_destroy(clippingReader);
  assert(storage.files.empty() && !clippingRenderDepth && !retirement_fixture::modelDepth);
  std::puts("Clipping sync tests passed through the real App store and native facade");
}

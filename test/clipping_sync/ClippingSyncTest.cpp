#include "clipping_store/reader_fixture.h"
#include "clipping_store/reader_storage.h"
#include "owner_tables.h"
#include "reader_lifecycle/read_folder_session_fixture.h"
static void* clippingReader;
static unsigned clippingRenderDepth;
#include <ArduinoJson.h>
#include <ClippingStore.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <SecureHttpClient.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "BuildScratch.h"
#include "clippings/ClippingSync.h"

namespace {
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
  auto* scratch = buildscratch::claim(32768);
  assert(scratch == framebuffer.data());
  buildscratch::release(scratch);
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
  assert(clippingSync::run(book, "hash"));
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
  runtime.render_lock = []() -> void* {
    assert(!clippingRenderDepth);
    ++clippingRenderDepth;
    return reinterpret_cast<void*>(1);
  };
  runtime.render_unlock = [](void*) {
    assert(clippingRenderDepth == 1);
    --clippingRenderDepth;
  };
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  clipping_fixture::StorageAdapter storage(leaves, resources);
  storage.quietLog = true;
  XpAppLegacy legacy{};
  XpAppPlatform platform{&legacy, &resources, &runtime, &leaves};
  XpAppCompatibility compatibility{};
  assert(xp_app_init(&platform, &compatibility) == XP_APP_OK);
  clippingReader = xp_epub_test_read_folder_create(&leaves, &runtime, &resources);
  assert(clippingReader);

  buildscratch::lend(framebuffer.data(), framebuffer.size());
  maximumTextRoundTrip();
  for (int failure = 0; failure < 3; ++failure) {
    reset();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
    fake::syncHeap = 100000;
#endif
    if (failure == 0) fake::failHttpAt = 0;
    if (failure == 1) fake::incomplete = true;
    if (failure == 2) fake::remoteResponse.assign(32768, 'x');
    assert(!clippingSync::run(book, "hash"));
    released();
  }
  reset();
  fake::failAlloc = 0;
  assert(!clippingSync::run(book, "hash"));
  released();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
  reset();
  assert(!clippingSync::run(book, "hash"));  // Preserve the S3 heap guard.
#endif
  buildscratch::reclaim();
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
  reset();
  assert(!clippingSync::run(book, "hash"));  // Missing loan must fail safely.
  assert(fake::requests == 0);
#endif
  xp_epub_test_read_folder_destroy(clippingReader);
  assert(storage.files.empty() && !clippingRenderDepth && !retirement_fixture::modelDepth);
  std::puts("Clipping sync tests passed through the real App store and native facade");
}

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "clipping_store/reader_fixture.h"
#include "clipping_store/reader_storage.h"
#include "owner_tables.h"
#include "reader_lifecycle/read_folder_session_fixture.h"

namespace {
unsigned renderDepth;
bool recording;
std::string failFrom, currentPath, sourceStore, targetStore, targetCache;
std::vector<std::string> events;
const std::string source = "/book.epub", destination = "/read/book.epub", cache = "/cache";
XpAppText wire(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
std::string text(XpAppText s) { return {reinterpret_cast<const char*>(s.data), s.len}; }
std::string label(const std::string& path) {
  const std::map<std::string, std::string> names{
      {source, "book"},
      {destination, "read-book"},
      {cache, "cache"},
      {targetCache, "new-cache"},
      {sourceStore, "store"},
      {targetStore, "read-store"},
      {sourceStore + ".deleted", "journal"},
      {targetStore + ".deleted", "read-journal"},
      {source + ".key", "key"},
      {destination + ".key", "read-key"},
      {source + ".rights", "rights"},
      {destination + ".rights", "read-rights"},
  };
  assert(names.count(path));
  return names.at(path);
}
std::vector<uint8_t> bytes(const std::string& path) {
  assert(fake::files.count(path));
  return fake::files.at(path)->bytes;
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  assert(mode == "success" || mode == "clip" || mode == "book" || mode == "key" || mode == "rights" ||
         mode == "cache" || mode == "collision" || mode == "other-state");
  auto leaves = retirement_fixture::leavesDefaults();
  auto runtime = retirement_fixture::modelRuntime();
  auto resources = retirement_fixture::resourcesDefaults();
  runtime.render_lock = []() -> void* {
    assert(!renderDepth);
    ++renderDepth;
    return reinterpret_cast<void*>(1);
  };
  runtime.render_unlock = [](void*) {
    assert(renderDepth == 1);
    --renderDepth;
  };
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  clipping_fixture::StorageAdapter storage(leaves, resources);
  storage.quietLog = true;
  storage.io = [] {
    assert(!retirement_fixture::modelDepth);
    if (recording) assert(!renderDepth);
  };
  resources.path_exists = [](XpAppText path) -> uint8_t {
    clipping_fixture::StorageAdapter::check();
    return ClippingTestStorage::getInstance().exists(text(path).c_str());
  };
  leaves.path_mkdir = [](XpAppText path) -> uint8_t {
    clipping_fixture::StorageAdapter::check();
    return ClippingTestStorage::getInstance().mkdir(text(path).c_str());
  };
  leaves.path_rename = [](XpAppText from, XpAppText to) -> uint8_t {
    clipping_fixture::StorageAdapter::check();
    const auto a = text(from), b = text(to);
    const bool ok = a != failFrom && ClippingTestStorage::getInstance().rename(a.c_str(), b.c_str());
    if (recording) events.push_back("rename " + label(a) + " " + label(b) + (ok ? " 1" : " 0"));
    return ok;
  };
  leaves.recents_update_path = [](XpAppText from, XpAppText to, XpAppText oldCache, XpAppText newCache) {
    assert(recording && !renderDepth && !retirement_fixture::modelDepth);
    assert(text(from) == source && text(to) == destination && text(oldCache) == cache);
    events.push_back("recent " + label(text(newCache)));
  };
  leaves.state_open_path = [](XpAppTextSink sink, void* context) -> uint8_t {
    assert(recording && !renderDepth);
    events.push_back("state-read");
    return sink(context, wire(currentPath));
  };
  leaves.state_set_open_path = [](XpAppText path) {
    assert(recording && !renderDepth && text(path) == destination);
    events.push_back("state-set");
    currentPath = text(path);
  };
  leaves.state_save = [] {
    assert(recording && !renderDepth);
    events.push_back("state-save");
  };
  XpAppLegacy legacy{};
  XpAppPlatform platform{&legacy, &resources, &runtime, &leaves};
  XpAppCompatibility compatibility{};
  assert(xp_app_init(&platform, &compatibility) == XP_APP_OK);
  void* owner = xp_epub_test_read_folder_create(&leaves, &runtime, &resources);
  assert(owner);
  // No legacy move adapter or session-owned clipping fixture is installed.
  fake::reset();
  fake::add(source, "epub");
  assert(xp_clippings_p18_load(wire(source), wire("Title"), wire("Author"), wire("epub")));
  XpClippingRecord row{};
  row.page_count = 1;
  row.id[0] = 'a';
  const std::string annotation = "annotation";
  assert(xp_epub_test_clipping_insert(owner, &row, wire("Chapter"), {wire(annotation), &annotation}) == 0);
  for (const auto& [path, node] : fake::files)
    if (path.ends_with(".bin")) sourceStore = path;
  assert(!sourceStore.empty());
  targetStore = "/.crosspoint/clippings/epub_" + std::to_string(std::hash<std::string>{}(destination)) + ".bin";
  targetCache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(destination));
  const auto storeBytes = bytes(sourceStore);
  xp_clippings_p18_unload();
  fake::add(sourceStore + ".deleted", "pending-id\n");
  const auto journal = bytes(sourceStore + ".deleted");
  fake::add(source + ".key", "key");
  fake::add(source + ".rights", "rights");
  fake::add(cache, "");
  fake::files.at(cache)->directory = true;
  fake::add(cache + "/page", "cached");
  if (mode == "collision") fake::add(targetStore, "occupied");
  failFrom = mode == "clip"     ? sourceStore + ".deleted"
             : mode == "book"   ? source
             : mode == "key"    ? source + ".key"
             : mode == "rights" ? source + ".rights"
             : mode == "cache"  ? cache
                                : "";
  currentPath = mode == "other-state" ? "/other.epub" : source;
  recording = true;
  assert(xp_epub_test_read_folder_move(owner, wire(source), wire(destination), wire(cache)));
  recording = false;
  std::vector<std::string> expected;
  if (mode != "collision") {
    expected = {"rename store read-store 1",
                std::string("rename journal read-journal ") + (mode == "clip" ? "0" : "1")};
    if (mode == "clip")
      expected.push_back("rename read-store store 1");
    else {
      expected.push_back(std::string("rename book read-book ") + (mode == "book" ? "0" : "1"));
      if (mode == "book") {
        expected.push_back("rename read-store store 1");
        expected.push_back("rename read-journal journal 1");
      } else {
        expected.push_back(std::string("rename key read-key ") + (mode == "key" ? "0" : "1"));
        if (mode != "key")
          expected.push_back(std::string("rename rights read-rights ") + (mode == "rights" ? "0" : "1"));
        if (mode == "key" || mode == "rights") {
          if (mode == "rights") expected.push_back("rename read-key key 1");
          expected.push_back("rename read-book book 1");
        } else {
          expected.push_back(std::string("rename cache new-cache ") + (mode == "cache" ? "0" : "1"));
          expected.push_back(mode == "cache" ? "recent cache" : "recent new-cache");
          expected.push_back("state-read");
          if (mode != "other-state") {
            expected.push_back("state-set");
            expected.push_back("state-save");
          }
        }
      }
    }
  }
  if (events != expected) {
    for (const auto& event : events) std::fprintf(stderr, "%s\n", event.c_str());
  }
  assert(events == expected);
  const bool storeMoved = mode != "clip" && mode != "book" && mode != "collision";
  const bool bookMoved = storeMoved && mode != "key" && mode != "rights";
  assert(bytes(storeMoved ? targetStore : sourceStore) == storeBytes);
  assert(bytes((storeMoved ? targetStore : sourceStore) + ".deleted") == journal);
  assert(fake::files.count(source) == unsigned(!bookMoved) && fake::files.count(destination) == unsigned(bookMoved));
  assert(bytes((bookMoved ? destination : source) + ".key") == std::vector<uint8_t>({'k', 'e', 'y'}));
  assert(bytes((bookMoved ? destination : source) + ".rights") == std::vector<uint8_t>({'r', 'i', 'g', 'h', 't', 's'}));
  assert(currentPath == (mode == "other-state" ? "/other.epub" : bookMoved ? destination : source));
  assert(bytes((bookMoved && mode != "cache" ? targetCache : cache) + "/page") ==
         std::vector<uint8_t>({'c', 'a', 'c', 'h', 'e', 'd'}));
  // Native sidecar failure restores the book but leaves the moved clipping state.
  // Preserve and expose this inherited path association; never discard its bytes.
  if (mode == "key" || mode == "rights") assert(!fake::files.count(sourceStore) && fake::files.count(targetStore));
  xp_epub_test_read_folder_destroy(owner);
  assert(!renderDepth && !retirement_fixture::modelDepth && storage.files.empty());
  std::printf("read-folder physical %s: exact transaction and reader ordering\n", mode.c_str());
}

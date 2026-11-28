#include <CrossPointState.h>
#include <HalStorage.h>
#include <Logging.h>
#include <RecentBooksStore.h>
#include <xp_app.h>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

struct Leaves {
  void (*rows)(void*, XpStoreRows*);
  void (*rowsClose)(void*);
  void (*move)(size_t, size_t);
  void (*document)(uint8_t, void*, XpStoreJsonOps*, XpStorePersistOps*, XpStoreState*);
  void (*log)(uint8_t, XpAppText, XpAppText);
};
struct Resources {
  uint8_t (*exists)(XpAppText);
};
extern "C" bool r3_store_call(const Leaves*, const Resources*, uint8_t, const XpAppBook*);
XpAppText wire(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.c_str()), s.size()}; }
std::string native(XpAppText s) { return {reinterpret_cast<const char*>(s.data), s.len}; }

// This initializer is extracted from the shipping EPUB table, not reconstructed here.
#include "store_document.inc"
const Leaves leaves{[](void* storage, XpStoreRows* out) { RECENT_BOOKS.readerRows(storage, out); },
                    RecentBooksStore::releaseReaderRows,
                    [](size_t dst, size_t src) { RECENT_BOOKS.moveReaderRow(dst, src); }, document,
                    [](uint8_t severity, XpAppText origin, XpAppText message) {
                      assert(severity == 1);
                      LOG_ERR(native(origin).c_str(), "%s", native(message).c_str());
                    }};
const Resources resources{[](XpAppText path) -> uint8_t { return Storage.exists(native(path).c_str()); }};

struct Result {
  std::vector<std::string> events, snapshots;
  bool operator==(const Result&) const = default;
};
Result run(bool rust, int failAt, bool failMkdir) {
  JsonDocument empty;
  RECENT_BOOKS.fromJson(empty.as<JsonVariantConst>());
  APP_STATE.fromJson(empty.as<JsonVariantConst>());
  APP_STATE.openEpubPath = "/old.epub";
  APP_STATE.readerActivityLoadCount = 7;
  APP_STATE.sleepCycle.state = 65535;
  APP_STATE.overlayCycle.count = 32768;
  APP_STATE.showBootScreen = false;
  store_test::files = {{"/책/읽기 📖.epub", "book"}, {"/b.epub", "book"}};
  store_test::writes = 0;
  store_test::failAt = failAt;
  store_test::failMkdir = failMkdir;
  store_test::io.clear();
  store_test::logs.clear();
  Result out;
  auto capture = [&](bool result) {
    JsonDocument state, recent;
    APP_STATE.toJson(state);
    RECENT_BOOKS.toJson(recent);
    std::string a, b;
    serializeJson(state, a);
    serializeJson(recent, b);
    out.snapshots.push_back(std::to_string(result) + "|" + a + "|" + b + "|" +
                            store_test::files[CrossPointState::getFilePath()] + "|" +
                            store_test::files[RecentBooksStore::getFilePath()]);
  };
  auto save = [&] {
    if (rust)
      r3_store_call(&leaves, &resources, 0, nullptr);
    else
      APP_STATE.saveToFile();
    capture(true);
  };
  auto add = [&](const std::string& path, const std::string& title, const std::string& author,
                 const std::string& cover) {
    XpAppBook value{wire(path), wire(title), wire(author), wire(cover)};
    if (rust)
      r3_store_call(&leaves, &resources, 1, &value);
    else
      RECENT_BOOKS.addBook(path, title, author, cover);
    capture(true);
  };
  auto remove = [&](const std::string& path) {
    XpAppBook value{wire(path), {}, {}, {}};
    capture(rust ? r3_store_call(&leaves, &resources, 2, &value) : RECENT_BOOKS.removeByPath(path));
  };
  auto move = [&](const std::string& from, const std::string& to, const std::string& oldCache,
                  const std::string& newCache) {
    XpAppBook value{wire(from), wire(to), wire(oldCache), wire(newCache)};
    if (rust)
      r3_store_call(&leaves, &resources, 3, &value);
    else
      RECENT_BOOKS.updatePath(from, to, oldCache, newCache);
    capture(true);
  };
  APP_STATE.openEpubPath.clear();
  save();
  APP_STATE.openEpubPath = "/책/읽기 📖.epub";
  save();
  add("/missing.epub", "missing", "", "");
  add("/책/읽기 📖.epub", "한글 \"quoted\"\n", "author", "/cache/a/thumb.bmp");
  add("/b.epub", "B", "author", "/external.bmp");
  add("/책/읽기 📖.epub", std::string(8192, 'x') + " 한글 📖 \"quoted\"\n", "new author", "/cache/a/thumb.bmp");
  remove("/not-listed.epub");
  remove("/b.epub");
  add("/b.epub", "B again", "author", "/external.bmp");
  move("/책/읽기 📖.epub", "/read/책/읽기 📖.epub", "/cache/a", "/cache/read-a");
  APP_STATE.openEpubPath = "/read/책/읽기 📖.epub";
  save();
  move("/b.epub", "/read/b.epub", "/cache/b", "/cache/read-b");
  move("/missing.epub", "/read/missing.epub", "", "/other");
  for (int i = 0; i < 12; ++i) {
    const std::string path = "/book-" + std::to_string(i);
    store_test::files[path] = "book";
    add(path, std::to_string(i), "", "");
  }
  APP_STATE.readerActivityLoadCount = 0;
  save();
  out.events = store_test::io;
  return out;
}
long random(long upper) {
  static uint32_t state = 0x12345u;
  state = 1664525u * state + 1013904223u;
  return (state >> 8) % upper;
}

void testSleepCycles(const char* upstreamFile) {
  std::ifstream input(upstreamFile, std::ios::binary);
  assert(input);
  const std::string bytes{std::istreambuf_iterator<char>(input), {}};
  auto& file = store_test::files[CrossPointState::getFilePath()];
  file = bytes;
  assert(APP_STATE.loadFromFile());
  assert(APP_STATE.sleepCycle.count == 131075 && APP_STATE.sleepCycle.state == UINT32_MAX);
  assert(APP_STATE.overlayCycle.count == 65538 && APP_STATE.overlayCycle.state == 2147483648u);
  assert(APP_STATE.saveToFile() && file == bytes);

  file = R"({"openEpubPath":"/old.epub","recentSleepImages":[2,1],"lastSleepImage":3,"recentSleepFill":2})";
  assert(APP_STATE.loadFromFile() && APP_STATE.openEpubPath == "/old.epub");
  assert(APP_STATE.sleepCycle.count == 0 && APP_STATE.sleepCycle.state == 0);
  assert(APP_STATE.overlayCycle.count == 0 && APP_STATE.overlayCycle.state == 0);

  // Recorded from d663dcc6f with the random() above. The count changes partway through cycle three.
  SleepImageCycle upstreamSequence;
  constexpr uint16_t indexes[] = {6, 5, 0, 2, 1, 4, 3, 6, 1, 0, 3, 2, 5, 4, 3, 0, 2, 1, 0};
  for (size_t i = 0; i < std::size(indexes); ++i) {
    assert(upstreamSequence.next(i < 15 ? 7 : 3) == indexes[i]);
  }
  assert(upstreamSequence.count == 131075 && upstreamSequence.state == 131072);

  for (const uint16_t count : {1, 2, 3, 16, 37, 65535}) {
    SleepImageCycle cycle;
    uint16_t last = 0;
    for (int round = 0; round < 2; ++round) {
      std::set<uint16_t> seen;
      for (unsigned i = 0; i < count; ++i) {
        const auto index = cycle.next(count);
        assert(index < count && seen.insert(index).second);
        if (round && i == 0 && count > 1) assert(index != last);
        last = index;
        if (i == count / 2) {
          APP_STATE.sleepCycle = cycle;
          assert(APP_STATE.saveToFile());
          APP_STATE.sleepCycle = {};
          assert(APP_STATE.loadFromFile());
          assert(APP_STATE.sleepCycle.count == cycle.count && APP_STATE.sleepCycle.state == cycle.state);
          cycle = APP_STATE.sleepCycle;
        }
      }
    }
  }
  auto& normal = APP_STATE.sleepCycle;
  normal.next(13);
  normal.next(7);
  assert((normal.count >> 16) == 6 && (normal.count & 0xffff) == 7);
  assert(APP_STATE.overlayCycle.count == 0);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  testSleepCycles(argv[1]);
  for (bool failMkdir : {false, true}) {
    for (int failAt = 0; failAt <= 26; ++failAt) {
      const auto before = run(false, failAt, failMkdir);
      const auto after = run(true, failAt, failMkdir);
      if (before != after) {
        std::fprintf(stderr, "store parity failed at write %d mkdir=%d\n", failAt, failMkdir);
        for (const auto& e : before.events) std::fprintf(stderr, "before: %s\n", e.c_str());
        for (const auto& e : after.events) std::fprintf(stderr, "after: %s\n", e.c_str());
        return 1;
      }
    }
  }
  std::puts("54 store sequences: persisted bytes, in-memory rows/state, errors and I/O order match");
}

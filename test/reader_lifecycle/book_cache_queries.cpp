#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace {
std::atomic<int> seekCalls{0}, interleave{0};
int firstAttempt = 0;
bool checkLock = false, failReads = false, failSeeks = false;
uint32_t lut = 0;
std::string chapter(int i) { return "OPS/chapter" + std::to_string(i) + ".xhtml"; }
uint32_t size(int i) { return (i % 37 == 5 || (i >= 30 && i < 35)) ? 0 : 1500 + uint32_t(i) * 7919 % 30000; }
void pod(std::string& bytes, uint32_t value, size_t width) {
  for (size_t i = 0; i < width; ++i) bytes.push_back(static_cast<char>(value >> (8 * i)));
}
void field(std::string& bytes, const std::string& value) {
  pod(bytes, value.size(), 4);
  bytes += value;
}
std::string fixture(std::vector<uint32_t>& totals) {
  std::string bytes;
  pod(bytes, 10, 1);
  pod(bytes, 0, 4);
  pod(bytes, 100, 2);
  pod(bytes, 100, 2);
  for (int i = 0; i < 5; ++i) field(bytes, "");
  lut = bytes.size();
  for (size_t i = 0; i < 4; ++i) bytes[1 + i] = static_cast<char>(lut >> (i * 8));
  bytes.resize(bytes.size() + 800);
  auto offset = [&](int row) {
    for (size_t i = 0; i < 4; ++i) bytes[lut + row * 4 + i] = static_cast<char>(bytes.size() >> (i * 8));
  };
  uint32_t total = 0;
  for (int i = 0; i < 100; ++i) {
    offset(i);
    field(bytes, chapter(i));
    total += size(i);
    totals.push_back(total);
    pod(bytes, total, 4);
    pod(bytes, i, 2);
  }
  for (int i = 0; i < 100; ++i) {
    offset(100 + i);
    field(bytes, "Title " + std::to_string(i));
    field(bytes, chapter(i));
    field(bytes, "anchor");
    pod(bytes, 2, 1);
    pod(bytes, i, 2);
  }
  return bytes;
}
bool seek(const char*, size_t position) {
  if (checkLock) assert(cacheDepth == 1);
  ++seekCalls;
  if (position == lut + 7 * 4 && interleave == 1) {
    interleave = 2;
    while (cacheAttempts < firstAttempt + 2) std::this_thread::yield();
    assert(seekCalls == 1 && "second row must wait for first row transaction");
    interleave = 3;
  }
  return !failSeeks;
}
size_t readLimit(const char*, size_t, size_t count) {
  if (checkLock) assert(cacheDepth == 1);
  return failReads ? 0 : count;
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string root = argv[1];
  std::filesystem::create_directories(root);
  std::vector<uint32_t> totals;
  const auto bytes = fixture(totals);
  std::ofstream(root + "/book.bin", std::ios::binary).write(bytes.data(), bytes.size());
  HalFileTest::allowSeek = seek;
  HalFileTest::readLimit = readLimit;
  {
    BookMetadataCache cache(root);
    assert(cache.load());
    checkLock = true;
    const int opens = Storage.readOpens;
    for (int i = 0; i < 100; ++i) {
      seekCalls = 0;
      const auto row = cache.getSpineEntry(i);
      assert(row.href == chapter(i) && row.cumulativeSize == totals[i] && row.tocIndex == i);
      assert(seekCalls == 2);
      seekCalls = 0;
      const auto toc = cache.getTocEntry(i);
      assert(toc.title == "Title " + std::to_string(i) && toc.href == chapter(i));
      assert(toc.anchor == "anchor" && toc.level == 2 && toc.spineIndex == i && seekCalls == 2);
      assert(cache.getCumulativeSize(i) == totals[i]);
    }
    for (uint32_t total : totals) {
      for (uint32_t value : {total - 1, total, total + 1}) {
        const auto found = std::lower_bound(totals.begin(), totals.end(), value);
        const int expected = found == totals.end() ? -1 : found - totals.begin();
        assert(cache.getSpineIndexForSize(value) == expected);
      }
    }
    assert(Storage.readOpens == opens && "queries reuse one file identity");

    seekCalls = 0;
    firstAttempt = cacheAttempts;
    interleave = 1;
    std::thread render([&] { assert(cache.getSpineEntry(7).href == chapter(7)); });
    while (interleave != 2) std::this_thread::yield();
    std::thread loop([&] { assert(cache.getTocEntry(13).href == chapter(13)); });
    render.join();
    loop.join();
    assert(interleave == 3 && seekCalls == 4);
    interleave = 0;

    auto windows = [&](int offset) {
      for (int step = 0; step < 1000; ++step) {
        int index = (step * 37 + offset) % 100;
        assert(cache.getCumulativeSize(index) == totals[index]);
      }
    };
    std::thread first(windows, 0), second(windows, 31);
    for (int step = 0; step < 1000; ++step) assert(cache.getSpineEntry(step % 100).href == chapter(step % 100));
    first.join();
    second.join();

    bool reentered = false;
    struct Nested {
      BookMetadataCache& cache;
      bool& called;
    } nested{cache, reentered};
    xp_epub_test_cache_query(
        cache.testHandle(), 1, 2,
        [](void* raw, uint32_t field, const uint8_t*, size_t, int64_t) {
          assert(cacheDepth == 0);
          if (field != 0) return;
          auto& ctx = *static_cast<Nested*>(raw);
          assert(ctx.cache.getTocEntry(8).href == chapter(8));
          ctx.called = true;
        },
        &nested);
    assert(reentered);

    const size_t live = xp_test_live_allocations();
    xp_test_fail_allocation_after(0);
    assert(cache.getSpineEntry(0).href.empty());
    assert(cacheDepth == 0 && xp_test_live_allocations() == live);
    for (size_t fault = 0; fault < 3; ++fault) {
      xp_test_fail_allocation_after(fault);
      assert(cache.getTocEntry(0).title.empty());
      assert(cacheDepth == 0 && xp_test_live_allocations() == live);
    }
    xp_test_fail_allocation_after(SIZE_MAX);

    assert(cache.getCumulativeSize(0) == totals[0]);
    failReads = true;
    assert(cache.getCumulativeSize(64) == totals[63]);
    assert(cache.getSpineIndexForSize(totals[70]) == 95);
    failReads = false;
    failSeeks = true;
    assert(cache.getCumulativeSize(64) == totals[63]);
    assert(cache.getSpineIndexForSize(totals[70]) == 95);
    failSeeks = false;
    assert(cache.getCumulativeSize(64) == totals[64]);
    assert(Storage.readOpens == opens);
    checkLock = false;
  }
  assert(filesBorn == filesDied && xp_test_live_allocations() == 0 && cacheDepth == 0);
  std::filesystem::remove_all(root);
}

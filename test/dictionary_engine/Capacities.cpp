#include <DictionaryIo.h>
#include <HalStorage.h>

#include <array>
#include <cassert>
#include <cstring>
#include <string>

int main() {
  // Tiny buffers force boundaries inside words and integer suffixes.
  std::string source;
  for (unsigned i = 0; i < 600; ++i) source += std::string("word\0", 5) + std::string(8, 'x');
  files["input"].bytes.assign(source.begin(), source.end());
  std::vector<uint8_t> expected;
  for (size_t capacity : {1, 3, 17, 1024, 4096}) {
    HalFile input = Storage.open("input");
    HalFile output = Storage.open("output", O_WRITE | O_CREAT | O_TRUNC);
    auto src = dictionaryFile(input), dst = dictionaryFile(output);
    std::array<uint8_t, 4098> guard;
    guard.fill(0xa5);
    XpDictionaryScan scan{};
    assert(xp_dictionary_scan(&src, &dst, 0x58444951, 8, guard.data() + 1, capacity, nullptr, nullptr, &scan) ==
           XP_DICTIONARY_SCAN_OK);
    assert(scan.entryCount == 600 && scan.sampleCount == 3 && scan.position == source.size());
    assert(guard.front() == 0xa5 && guard[capacity + 1] == 0xa5);
    if (expected.empty()) expected = files["output"].bytes;
    assert(files["output"].bytes == expected);
  }
  for (size_t capacity : {1, 7, 1024, 2048, 3000}) {
    HalFile input = Storage.open("input");
    auto src = dictionaryFile(input);
    std::array<uint8_t, 3002> guard;
    guard.fill(0xa5);
    uint32_t remaining = source.size();
    bool failed = false;
    const uint8_t* data = nullptr;
    std::string read;
    while (remaining) {
      const size_t n = xp_dictzip_refill(&src, &remaining, &failed, guard.data() + 1, capacity, &data);
      assert(n > 0 && n <= capacity && !failed && data == guard.data() + 1);
      read.append(reinterpret_cast<const char*>(data), n);
      assert(guard.front() == 0xa5 && guard[capacity + 1] == 0xa5);
    }
    assert(read == source);
    assert(xp_dictzip_refill(&src, &remaining, &failed, guard.data() + 1, capacity, &data) == 0);
  }
  struct Entries {
    std::array<XpDictionaryLocation, 3> locations{};
    size_t count = 0, capacity = 0, allocations = 0;
    uint32_t offset = 0;
  } entries;
  for (size_t capacity : {0, 1, 3}) {
    entries = {};
    entries.capacity = capacity;
    XpDictionaryAppend io{&entries,
                          0,
                          capacity,
                          0,
                          2,
                          100000,
                          100000,
                          [](void* ctx, size_t i) {
                            const auto& e = *static_cast<Entries*>(ctx);
                            assert(i < e.count);
                            return e.locations[i];
                          },
                          [](void* ctx, size_t bytes) {
                            auto& e = *static_cast<Entries*>(ctx);
                            assert(e.count < e.capacity && bytes == 2);
                            ++e.allocations;
                            return true;
                          },
                          [](void* ctx) {
                            auto& e = *static_cast<Entries*>(ctx);
                            assert(e.count < e.capacity);
                            e.locations[e.count++] = {e.offset, 1, true, false};
                          }};
    for (size_t i = 0; i < capacity; ++i) {
      entries.offset = i;
      io.count = entries.count;
      assert(xp_dictionary_append(&io, i, 1) == XP_DICTIONARY_APPEND_OK);
    }
    io.count = entries.count;
    assert(xp_dictionary_append(&io, 10, 1) == XP_DICTIONARY_APPEND_FULL);
    assert(entries.count == capacity && entries.allocations == capacity);
    if (capacity) assert(xp_dictionary_append(&io, 0, 1) == XP_DICTIONARY_APPEND_OK);
    assert(entries.count == capacity && entries.allocations == capacity);
  }
}

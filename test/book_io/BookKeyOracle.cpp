#include <BookKey.h>
#include <HalStorage.h>
#include <wolfssl/wolfcrypt/aes.h>

void dump(const uint8_t* bytes, size_t len) {
  for (size_t i = 0; i < len; ++i) std::printf("%02x", bytes[i]);
  std::puts("");
}
size_t writeCase(size_t len, int64_t expires, size_t fail) {
  booktest::files.clear(); booktest::calls = 0; booktest::fail = fail;
  booktest::files["book.key"] = {1, 2, 3};
  uint8_t key[32]; for (size_t i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 9);
  std::printf("writecase %zu %lld %zu\n", len, static_cast<long long>(expires), fail);
  std::printf("result %d\n", bookkey::write("book", key, len, expires));
  for (const auto& [path, bytes] : booktest::files) {
    std::printf("file %s\n", path.c_str()); dump(bytes.data(), bytes.size());
  }
  return booktest::calls;
}
size_t readCase(const std::vector<uint8_t>& bytes, size_t fail) {
  booktest::files.clear(); booktest::files["book.key"] = bytes;
  booktest::calls = 0; booktest::fail = fail;
  std::printf("readcase %zu %zu\n", bytes.size(), fail); dump(bytes.data(), bytes.size());
  uint8_t key[32]; std::memset(key, 0xa5, 32);
  int64_t expires = 123;
  const size_t count = bookkey::read("book", key, &expires);
  std::printf("result %zu %lld\n", count, static_cast<long long>(expires)); dump(key, sizeof(key));
  return booktest::calls;
}
int main() {
  for (size_t len : {size_t{0}, size_t{15}, size_t{16}, size_t{32}, size_t{33}}) {
    for (int64_t expiry : {int64_t{0}, int64_t{-1}, INT64_MAX, int64_t{100000}}) {
      const size_t steps = writeCase(len, expiry, 0);
      for (size_t fail = 1; fail <= steps; ++fail) writeCase(len, expiry, fail);
    }
  }
  for (size_t len : {size_t{16}, size_t{32}}) {
    writeCase(len, 987654321, 0);
    std::vector<std::vector<uint8_t>> records{booktest::files["book.key"]};
    if (len == 16) {
      auto legacy = records.front(); legacy[4] = 1; legacy.erase(legacy.begin() + 5);
      fakeTag(legacy.data() + 25, 16, legacy.data(), 13, legacy.data() + 41);
      records.push_back(legacy);
    }
    for (const auto& record : records) {
      const size_t steps = readCase(record, 0);
      for (size_t fail = 1; fail <= steps; ++fail) readCase(record, fail);
      for (size_t n = 0; n < record.size(); ++n) readCase({record.begin(), record.begin() + n}, 0);
      auto extra = record; extra.push_back(0); readCase(extra, 0);
      for (size_t at = 0; at < record.size(); ++at) {
        auto corrupt = record; corrupt[at] ^= 0x80; readCase(corrupt, 0);
      }
    }
  }
}

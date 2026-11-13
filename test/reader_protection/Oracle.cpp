#include <Arduino.h>
#include <BookKey.h>
#include <BuildScratch.h>
#include <ContentProtection.h>
#include <DeviceSecret.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>
#include <ReaderProtection.h>
#include <TrustedTime.h>
#include <xp_epub_session.h>

#include <cstring>
#include <fstream>
#include <iterator>

extern "C" bool xp_app_test_protection_key_wiped();
extern "C" size_t cssTestAllocationAttempts();
extern "C" bool xp_app_test_protection_load(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                            XpAppTextSink, void*);
size_t rustAttempts = 0;
extern "C" void* xp_app_test_protection_open(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppTextSink,
                                             void*);
extern "C" bool xp_app_test_protection_stream(const XpAppResources*, const XpEpubLeaves*, void*, XpAppText, XpBookSink,
                                              void*);
extern "C" size_t heap_caps_get_largest_free_block(uint32_t) { return protection_test::largest; }
XpAppText wire(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
std::string owned(XpAppText s) { return {reinterpret_cast<const char*>(s.data), s.len}; }
const char* pathText(XpAppText s) { return reinterpret_cast<const char*>(s.data); }
[[noreturn]] void unused() { std::abort(); }
template <class T>
void poison(T& value) {
  static_assert(sizeof(T) % sizeof(void*) == 0);
  auto trap = &unused;
  for (size_t i = 0; i < sizeof(T); i += sizeof(trap)) memcpy(reinterpret_cast<char*>(&value) + i, &trap, sizeof(trap));
}
bool rejectSink = false;
XpEpubLeaves leaves;
XpAppResources resources;
void setup() {
  poison(leaves);
  poison(resources);
  xp_font_registry_init(&protection_test::fonts, &protection_test::generation, nullptr);
  resources.font_owner = &protection_test::fontSlot;
  resources.buffer_allocate = [](XpAppMemoryDomain, size_t size) {
    return protection_test::failBuffer() ? nullptr : static_cast<uint8_t*>(std::malloc(size));
  };
  resources.buffer_release = [](XpAppMemoryDomain, uint8_t* bytes) { std::free(bytes); };
  // ACTUAL_LEAVES
  leaves.protection_index = freeink::content::readerProtectionIndex;
  leaves.protection_finish = freeink::content::readerProtectionFinish;
  leaves.protection_stream = freeink::content::readerProtectionStream;
  leaves.key_platform = bookkey::readerPlatform;
  leaves.heap_snapshot = [](XpEpubHeap* out) { *out = {ESP.getFreeHeap(), ESP.getMaxAllocHeap()}; };
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 1; };
  leaves.log = [](uint8_t level, XpAppText origin, XpAppText text) {
    testLog(level == 0 ? "DBG" : level == 1 ? "ERR" : "INF", owned(origin).c_str(), "%.*s", int(text.len), text.data);
  };
  leaves.time_services = []() { return &protection_test::timeServices; };
  leaves.cache_lock = []() {};
  leaves.cache_unlock = []() {};
  leaves.scratch_claim = [](uint32_t) -> uint8_t* { return nullptr; };
  leaves.scratch_release = [](const uint8_t*) { std::abort(); };
  leaves.inflate_oom_log = [](XpAppText, uint32_t) { std::abort(); };
}
struct Result {
  std::vector<std::string> trace;
  std::string error, bytes;
  bool opened = false, streamed = false;
  size_t steps = 0;
  bool operator==(const Result&) const = default;
};
Result run(bool rust, const std::string& path, size_t fail, int keyKind, int64_t expiry, int64_t now,
           bool reader = false) {
  cssTestFailAllocation(SIZE_MAX);
  protection_test::failAt = 0;
  protection_test::steps = 0;
  std::remove((path + ".key").c_str());
  if (keyKind) {
    uint8_t key[16];
    for (size_t i = 0; i < 16; ++i) key[i] = uint8_t(i + (keyKind == 2 ? 31 : 0));
    assert(bookkey::write(path, key, 16, expiry));
    if (keyKind == 3) {
      std::fstream f(path + ".key", std::ios::in | std::ios::out | std::ios::binary);
      f.seekp(30);
      f.put('\xff');
    }
  }
  uint32_t handle = 0;
  XpHalSecretServices secret;
  auto* state = borrowDeviceSecret(&handle, &secret);
  *state = {};
  releaseDeviceSecret();
  protection_test::trace.clear();
  protection_test::steps = 0;
  protection_test::failAt = fail;
  protection_test::now = now;
  protection_test::bufferAttempts = 0;
  Result result;
  protection_test::armRustFailure = rust;
  if (rust && reader) {
    result.opened = xp_app_test_protection_load(
        &resources, &leaves, wire(path), wire(path + ".cache"),
        [](void* raw, XpAppText text) -> uint8_t {
          static_cast<Result*>(raw)->error = owned(text);
          return 1;
        },
        &result);
  } else if (rust) {
    void* book = xp_app_test_protection_open(
        &resources, &leaves, wire(path),
        [](void* raw, XpAppText text) -> uint8_t {
          static_cast<Result*>(raw)->error = owned(text);
          return 1;
        },
        &result);
    result.opened = book;
    if (book) {
      std::string entry = "OPS/chapter.xhtml";
      assert(leaves.protection_encrypted(book, wire(entry)));
      assert(leaves.protection_size(book, wire(entry)) > 0);
      result.streamed = xp_app_test_protection_stream(
          &resources, &leaves, book, wire(entry),
          [](void* raw, const uint8_t* bytes, size_t length) -> uint8_t {
            static_cast<Result*>(raw)->bytes.append(reinterpret_cast<const char*>(bytes), length);
            return !rejectSink;
          },
          &result);
      leaves.protection_close(book);
    }
  } else {
    auto book = freeink::content::openProtectedBook(path, result.error);
    result.opened = bool(book);
    if (book) {
      assert(book->isEncrypted("OPS/chapter.xhtml"));
      assert(book->decryptedSize("OPS/chapter.xhtml") > 0);
      result.streamed = book->decryptToSink(
          "OPS/chapter.xhtml",
          [](void* raw, const uint8_t* bytes, size_t length) {
            static_cast<Result*>(raw)->bytes.append(reinterpret_cast<const char*>(bytes), length);
            return !rejectSink;
          },
          &result);
    }
  }
  rustAttempts = cssTestAllocationAttempts();
  cssTestFailAllocation(SIZE_MAX);
  result.trace = protection_test::trace;
  result.steps = protection_test::steps;
  return result;
}
void offsetOverflow(const std::string& root) {
  const std::string name = "META-INF/encryption.xml";
  auto put = [](std::vector<uint8_t>& data, size_t offset, uint32_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) data.at(offset + i) = value >> (8 * i);
  };
  std::vector<uint8_t> local(30), central(46 + name.size()), end(22);
  put(local, 0, 0x04034b50, 4);
  put(local, 26, 0xffff, 2);
  put(local, 28, 0xffff, 2);
  put(central, 0, 0x02014b50, 4);
  put(central, 20, 1, 4);
  put(central, 24, 1, 4);
  put(central, 28, name.size(), 2);
  put(central, 42, 0xffff0000, 4);
  memcpy(central.data() + 46, name.data(), name.size());
  put(end, 0, 0x06054b50, 4);
  put(end, 8, 1, 2);
  put(end, 10, 1, 2);
  put(end, 12, central.size(), 4);
  put(end, 16, 0xffff8000, 4);
  protection_test::segments = {{0xffff0000, local}, {0xffff8000, central}, {0xffff8100, end}};
  const auto path = root + "/offset-overflow.epub";
  std::ofstream(path).close();
  const auto result = run(true, path, 0, 0, 0, 1800002000);
  assert(!result.opened && result.error == "cannot read encryption manifest");
  const uint64_t offset = uint64_t(0xffff0000) + 30 + 0xffff + 0xffff;
  assert(offset == 0x10001001c && uint32_t(offset) == 0x1001c);
  for (const auto& step : result.trace) assert(step != "seek " + path + " " + std::to_string(uint32_t(offset)));
  printf("Near-4-GiB local offset rejected before wrapped data seek; 120 bytes of fake segments\n");
}
int main(int argc, char** argv) {
  assert(argc == 2);
  assert(xp_app_test_protection_key_wiped());
  setup();
  std::string root = argv[1];
  // Prove the generated cache is usable with the full reader owner.
  assert(run(true, root + "/plain.epub", 0, 0, 0, 1800002000, true).opened);
  size_t allocationCases = 0;
  for (const char* name : {"protected.epub", "plain-spine.epub"})
    for (bool reader : {false, true})
      for (int key : {0, 1}) {
        const std::string path = root + "/" + name;
        protection_test::allocationFailure = 3;
        auto wrapper = run(true, path, 0, key, 1800001000, 1800002000, reader);
        assert(!wrapper.opened && !wrapper.error.empty());
        assert(protection_test::allocationFailure == 0);
        const auto baseline = run(true, path, 0, key, 1800001000, 1800002000, reader);
        const size_t count = rustAttempts;
        assert(count > 0 && !baseline.opened && !baseline.error.empty());
        for (size_t fail = 0; fail < count; ++fail) {
          protection_test::rustFailure = fail;
          const auto result = run(true, path, 0, key, 1800001000, 1800002000, reader);
          assert(!result.opened && !result.error.empty());
          for (const auto& step : result.trace) assert(step.find("book.bin") == std::string::npos);
          ++allocationCases;
        }
        protection_test::rustFailure = SIZE_MAX;
      }
  printf(
      "%zu one-shot Rust allocation failures plus 8 wrapper failures refuse missing/expired keys before cache access\n",
      allocationCases);
  assert(run(true, root + "/plain-spine.epub", 0, 1, 0, 1800002000, true).opened);
  offsetOverflow(root);
  protection_test::bufferFailure = 1;
  const auto nativeEocd = run(false, root + "/protected.epub", 0, 0, 0, 1800002000);
  const auto rustEocd = run(true, root + "/protected.epub", 0, 0, 0, 1800002000);
  assert(nativeEocd == rustEocd && !rustEocd.opened && rustEocd.error.empty());
  protection_test::bufferFailure = 0;
  printf("Inherited EOCD scan-buffer allocation failure remains Plain in both gates\n");
  protection_test::shortRead = true;
  size_t shortCases = 0;
  for (int key : {0, 1}) {
    const auto path = root + "/protected.epub";
    const auto baseline = run(false, path, 0, key, 1800001000, 1800002000);
    for (size_t fail = 1; fail <= baseline.steps; ++fail) {
      const auto before = run(false, path, fail, key, 1800001000, 1800002000);
      const auto after = run(true, path, fail, key, 1800001000, 1800002000);
      assert(before == after);
      ++shortCases;
    }
  }
  protection_test::shortRead = false;
  printf("%zu paired short-read/physical failure cases retain native decisions and trace order\n", shortCases);
  size_t cases = 0;
  std::ifstream plaintext(root + "/chapter.xhtml", std::ios::binary);
  const std::string expected((std::istreambuf_iterator<char>(plaintext)), {});
  for (size_t largest : {size_t(100000), size_t(65535)})
    for (const char* name : {"protected.epub", "plain.epub", "unsupported.epub", "bad-xml.epub", "truncated.epub",
                             "corrupt-content.epub", "sample.txt"})
      for (int key : {0, 1, 2, 3})
        for (int64_t expiry : {int64_t(0), int64_t(1800003000), int64_t(1800001000)})
          for (int64_t now : {int64_t(0), int64_t(1800002000)}) {
            const std::string path = root + "/" + name;
            protection_test::largest = largest;
            rejectSink = false;
            protection_test::allocationFailure = 0;
            const auto baseline = run(false, path, 0, key, expiry, now);
            if (std::string(name) == "protected.epub" && key == 1 && (expiry == 0 || (now != 0 && expiry >= now))) {
              assert(baseline.opened && baseline.streamed && baseline.bytes == expected);
            }
            for (bool reject : {false, true})
              for (int allocation : {0, 1, 2})
                for (size_t fail = 0; fail <= (allocation ? 0 : baseline.steps); ++fail) {
                  rejectSink = reject;
                  protection_test::allocationFailure = allocation;
                  auto before = run(false, path, fail, key, expiry, now),
                       after = run(true, path, fail, key, expiry, now);
                  ++cases;
                  if (before != after) {
                    fprintf(stderr, "Mismatch %s key=%d expiry=%lld now=%lld failure=%zu allocation=%d\n", name, key,
                            (long long)expiry, (long long)now, fail, allocation);
                    fprintf(stderr,
                            "before open=%d stream=%d error=%s steps=%zu; after open=%d stream=%d error=%s steps=%zu\n",
                            before.opened, before.streamed, before.error.c_str(), before.steps, after.opened,
                            after.streamed, after.error.c_str(), after.steps);
                    for (auto& row : before.trace) fprintf(stderr, "before %s\n", row.c_str());
                    for (auto& row : after.trace) fprintf(stderr, "after %s\n", row.c_str());
                    return 1;
                  }
                }
          }
  xp_font_registry_drop(&protection_test::fonts);
  printf("%zu differential protection cases: SD/crypto order, errors, lifetime and decrypted bytes match\n", cases);
}

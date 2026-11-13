#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <esp_rom_md5.h>
#include <xp_epub_session.h>

#include <cstring>
#include <iostream>
#include <new>
#include <utility>

#include "BookFixture.h"
#include "RustReaderDocument.h"
extern "C" bool xp_app_test_document_id(const XpEpubLeaves*, XpAppText, uint8_t*);
[[noreturn]] void unused() { std::abort(); }
XpEpubLeaves leaves;
XpAppRuntime runtime;
XpAppResources resources;
extern "C" void xp_app_test_document_session(const XpEpubLeaves*, const XpAppRuntime*, const XpAppResources*, XpAppText,
                                             bool, bool);
bool subscribed = true;
std::string emitted;
int locks = 0;

namespace pluginevents {
enum class Event { ReaderSession };
struct Var {
  const char* name;
  const char* value;
};
void emit(Event event, const Var* vars, size_t count) {
  assert(event == Event::ReaderSession && count == 8 && locks == 0);
  assert(std::string(vars[0].name) == "book" && std::string(vars[0].value) == "/session.epub");
  const char* values[] = {"100", "110", "10", "123", "456", "10000"};
  for (size_t i = 2; i < count; ++i) assert(std::string(vars[i].value) == values[i - 2]);
  emitted = vars[1].value;
  assert(emitted.size() == 32);
  document_test::event("emit " + emitted);
}
}  // namespace pluginevents
const char* pathText(XpAppText text) { return reinterpret_cast<const char*>(text.data); }
// ACTUAL_EMIT_SESSION
std::string owned(XpAppText text) { return {reinterpret_cast<const char*>(text.data), text.len}; }
struct Result {
  std::string digest;
  std::vector<std::string> trace;
  bool operator==(const Result&) const = default;
};
Result run(bool rust, const std::string& path, uint32_t size, int seek, int shortRead, bool failOpen = false,
           int readError = -1, int zeroRead = -1) {
  using namespace document_test;
  trace.clear();
  document_test::size = size;
  offset = 0;
  sample = -1;
  seekFail = seek;
  readShort = shortRead;
  openFail = failOpen;
  readFail = readError;
  readZero = zeroRead;
  std::string digest;
  if (rust) {
    uint8_t output[32]{};
    if (xp_app_test_document_id(&leaves, {reinterpret_cast<const uint8_t*>(path.c_str()), path.size()}, output))
      digest.assign(reinterpret_cast<const char*>(output), 32);
  } else {
    assert(readError == -1);
    digest = KOReaderDocumentId::calculate(path);
  }
  return {digest, trace};
}
int main() {
  auto trap = &unused;
  for (size_t i = 0; i < sizeof(leaves); i += sizeof(trap))
    memcpy(reinterpret_cast<char*>(&leaves) + i, &trap, sizeof(trap));
  leaves.document_io = readerDocumentIo;
  leaves.log_enabled = [](uint8_t severity) -> uint8_t {
    assert(severity <= 2);
    return document_test::logs;
  };
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    assert(severity == 0);
    document_test::log(owned(origin).c_str(), "%.*s", int(message.len), message.data);
  };
  size_t cases = 0, healthyCases = 0, rejectedCases = 0;
  std::vector<uint32_t> sizes{0, 1, 1023, 1024, 1025, 2048, 0x7fffffff, 0x80000000, 0x80000001, UINT32_MAX};
  auto sampleOffset = [](int sample) -> uint32_t { return sample == 0 ? 0 : 1024u << (2 * (sample - 1)); };
  for (int sample = 1; sample < 12; ++sample) {
    const auto offset = sampleOffset(sample);
    for (uint32_t size : {offset - 1, offset, offset + 1, offset + 1023, offset + 1024, offset + 1025})
      sizes.push_back(size);
  }
  std::sort(sizes.begin(), sizes.end());
  sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
  for (bool logging : {false, true}) {
    document_test::logs = logging;
    for (const std::string& path : {std::string("/book.epub"), std::string("/책/읽기 📖.epub"),
                                    std::string("/\xff.epub"), "/" + std::string(400, 'a') + ".epub"})
      for (uint32_t size : sizes) {
        const auto native = run(false, path, size, -1, -1);
        assert(native == run(true, path, size, -1, -1));
        assert(run(false, path, size, -1, -1, true) == run(true, path, size, -1, -1, true));
        cases += 2;
        ++healthyCases;
        // Native negative reads access out of bounds (bug 7); never execute that oracle branch.
        for (int sample = 0; sample < 12; ++sample)
          for (int fault = 0; fault < 4; ++fault) {
            const auto result = run(true, path, size, fault == 0 ? sample : -1, fault == 1 ? sample : -1, false,
                                    fault == 2 ? sample : -1, fault == 3 ? sample : -1);
            if (sampleOffset(sample) < size) {
              assert(result.digest.empty());
              assert(std::count(result.trace.begin(), result.trace.end(), "size") == 1);
              assert(std::count(result.trace.begin(), result.trace.end(), "digest.finish") == 0);
              assert(result.trace[result.trace.size() - 2] == "close" && result.trace.back() == "destroy");
              assert(std::count_if(result.trace.begin(), result.trace.end(),
                                   [](const auto& step) { return step.starts_with("seek "); }) == sample + 1);
              ++rejectedCases;
            } else {
              assert(result == native);  // An offset at/beyond EOF is not a failed sample.
            }
            assert(run(true, path, size, -1, -1) == native);  // No failed ID is retained.
            cases += 2;
          }
      }
  }
  assert(run(true, "/empty", 0, -1, -1).digest == "d41d8cd98f00b204e9800998ecf8427e");

  // Real shipping flush_session owns the file/digest lifetime and reset order.
  for (size_t i = 0; i < sizeof(runtime); i += sizeof(trap))
    memcpy(reinterpret_cast<char*>(&runtime) + i, &trap, sizeof(trap));
  for (size_t i = 0; i < sizeof(resources); i += sizeof(trap))
    memcpy(reinterpret_cast<char*>(&resources) + i, &trap, sizeof(trap));
  runtime.model_lock = []() {
    assert(locks == 0);
    ++locks;
    document_test::event("lock");
  };
  runtime.model_unlock = []() {
    assert(locks == 1);
    --locks;
    document_test::event("unlock");
  };
  leaves.subscribers = []() -> const XpPluginSubscriber* {
    document_test::event("subscribed");
    static XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS]{};
    table[0].name[0] = subscribed ? 's' : 0;
    table[0].mask = 1 << 2;  // reader.session in xp_plugin_contract.def.
    return table;
  };
  leaves.emit_session = emitSession;
  const std::string sessionPath = "/session.epub";
  for (bool worthy : {false, true})
    for (bool subscriber : {false, true})
      for (bool failOpen : {false, true}) {
        auto expected = run(false, sessionPath, 16385, -1, -1, failOpen);
        document_test::trace.clear();
        document_test::sample = -1;
        document_test::offset = 0;
        subscribed = subscriber;
        emitted.clear();
        xp_app_test_document_session(&leaves, &runtime, &resources,
                                     {reinterpret_cast<const uint8_t*>(sessionPath.c_str()), sessionPath.size()},
                                     worthy, false);
        std::vector<std::string> trace{"lock", "unlock"};
        if (worthy) trace.push_back("subscribed");
        if (worthy && subscriber) {
          trace.insert(trace.end(), expected.trace.begin(), expected.trace.end());
          if (!failOpen) trace.push_back("emit " + expected.digest);
        }
        trace.push_back("lock");
        trace.push_back("unlock");
        assert(document_test::trace == trace);
        assert(emitted == ((worthy && subscriber && !failOpen) ? expected.digest : ""));
      }
  book_fixture::install(leaves, resources);
  size_t sessionCases = 8;
  auto sessionCase = [&](uint32_t size, int sample, int fault) {
    document_test::logs = false;
    const auto healthy = run(false, sessionPath, size, -1, -1);
    run(true, sessionPath, size, fault == 0 ? sample : -1, fault == 1 ? sample : -1, false, fault == 2 ? sample : -1,
        fault == 3 ? sample : -1);
    document_test::trace.clear();
    document_test::sample = -1;
    document_test::offset = 0;
    subscribed = true;
    emitted.clear();
    const auto before = book_fixture::opened;
    xp_app_test_document_session(&leaves, &runtime, &resources,
                                 {reinterpret_cast<const uint8_t*>(sessionPath.c_str()), sessionPath.size()}, true,
                                 true);
    assert(book_fixture::opened == before + 1 && book_fixture::closed == book_fixture::opened);
    assert(book_fixture::liveFiles == 0 && book_fixture::cacheLocks == 0);
    const bool failed = sample >= 0 && sampleOffset(sample) < size;
    assert(emitted == (failed ? "" : healthy.digest));
    // Four lock pairs: attach, snapshot, one reset, and detach; no extra reset.
    assert(std::count(document_test::trace.begin(), document_test::trace.end(), "lock") == 4);
    assert(std::count(document_test::trace.begin(), document_test::trace.end(), "unlock") == 4);
    assert(document_test::trace[document_test::trace.size() - 2] == "lock");
    assert(document_test::trace.back() == "unlock");
    ++sessionCases;
  };
  for (int sample = 0; sample < 12; ++sample)
    for (int fault = 0; fault < 4; ++fault) {
      sessionCase(UINT32_MAX, sample, fault);
      sessionCase(UINT32_MAX, -1, -1);  // A fresh open retries after the fault clears.
    }
  for (uint32_t size : {0u, 1u, 1023u, 1024u, 1025u, 16385u}) sessionCase(size, -1, -1);
  std::cout << cases << " differential/failure/recovery cases (" << healthyCases << " healthy boundaries, "
            << rejectedCases << " active read/seek faults) plus " << sessionCases << " shipping sessions PASS\n";
}

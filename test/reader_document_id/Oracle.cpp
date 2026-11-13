#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <esp_rom_md5.h>
#include <xp_epub_session.h>

#include <cstring>
#include <iostream>
#include <new>
#include <utility>

#include "RustReaderDocument.h"
extern "C" bool xp_app_test_document_id(const XpEpubLeaves*, XpAppText, uint8_t*);
[[noreturn]] void unused() { std::abort(); }
XpEpubLeaves leaves;
XpAppRuntime runtime;
XpAppResources resources;
extern "C" void xp_app_test_document_session(const XpEpubLeaves*, const XpAppRuntime*, const XpAppResources*, XpAppText,
                                             bool);
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
    assert(severity == 0);
    return document_test::logs;
  };
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    assert(severity == 0);
    document_test::log(owned(origin).c_str(), "%.*s", int(message.len), message.data);
  };
  size_t cases = 0;
  for (bool logging : {false, true}) {
    document_test::logs = logging;
    for (const std::string& path : {std::string("/book.epub"), std::string("/책/읽기 📖.epub"),
                                    std::string("/\xff.epub"), "/" + std::string(400, 'a') + ".epub"})
      for (uint32_t size : {0u, 1u, 1023u, 1024u, 1025u, 2048u, 4097u, 16385u, 1073741825u, UINT32_MAX}) {
        auto compare = [&](int seek, int shortRead, bool failOpen) {
          auto native = run(false, path, size, seek, shortRead, failOpen);
          auto rust = run(true, path, size, seek, shortRead, failOpen);
          if (!(native == rust)) {
            std::cerr << "case size=" << size << " seek=" << seek << " short=" << shortRead << " open=" << failOpen
                      << '\n';
            for (auto& x : native.trace) std::cerr << "native " << x << '\n';
            for (auto& x : rust.trace) std::cerr << "rust " << x << '\n';
            std::abort();
          }
          ++cases;
        };
        compare(-1, -1, false);
        compare(-1, -1, true);
        for (int sample = 0; sample < 12; ++sample) {
          compare(sample, -1, false);
          compare(-1, sample, false);
        }
        for (int sample = 0; sample < 12; ++sample) {
          assert(run(false, path, size, -1, -1, false, -1, sample) == run(true, path, size, -1, -1, false, -1, sample));
          ++cases;
        }
        // Negative native reads are UB (upstream bug 7), so check only the Rust-defined result.
        for (int sample = 0; sample < 12; ++sample) {
          auto failed = run(true, path, size, -1, -1, false, sample);
          auto skipped = run(false, path, size, sample, -1);
          assert(failed.digest == skipped.digest);
          ++cases;
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
                                     worthy);
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
  std::cout << cases << " document-ID differential/failure cases plus 8 shipping session cases PASS\n";
}

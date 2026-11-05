#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "BlePageTurnerJson.h"

struct TraceAllocator : ArduinoJson::Allocator {
  unsigned calls = 0;
  unsigned failAt = 0;
  std::vector<std::string> trace;
  void* allocate(size_t size) override {
    trace.push_back("alloc " + std::to_string(size));
    return ++calls == failAt ? nullptr : std::malloc(size);
  }
  void deallocate(void* ptr) override {
    trace.push_back(ptr ? "free" : "free null");
    std::free(ptr);
  }
  void* reallocate(void* ptr, size_t size) override {
    trace.push_back("realloc " + std::to_string(size));
    return ++calls == failAt ? nullptr : std::realloc(ptr, size);
  }
};

int main() {
  for (unsigned prefill = 235; prefill <= 260; ++prefill) {
    for (unsigned fail = 0; fail <= 3; ++fail) {
      TraceAllocator allocator;
      allocator.failAt = fail;
      {
        JsonDocument doc(&allocator);
        auto padding = doc["padding"].to<JsonArray>();
        for (unsigned i = 0; i < prefill; ++i) padding.add(i);
        bleturner::Config c;
        c.enabled = 1;
        std::snprintf(c.peerAddr, sizeof(c.peerAddr), "12:34:56:78:90:AB");
        std::snprintf(c.peerName, sizeof(c.peerName), "Free3-R");
        c.remoteCount = 4;
        for (unsigned i = 0; i < 4; ++i) {
          std::snprintf(c.remotes[i].addr, sizeof(c.remotes[i].addr), "12:34:56:78:90:%02X", i);
          c.remotes[i].count = 8;
          for (unsigned j = 0; j < 8; ++j)
            c.remotes[i].bindings[j] = bleturner::makeBinding(0x030001 + j, false, bleturner::Action::NextPage);
        }
        bleturner::writeJson(c, doc);
        std::string text;
        serializeJson(doc, text);
        std::printf("case %u/%u overflow %d %s\n", prefill, fail, doc.overflowed(), text.c_str());
        // Config strings may be borrowed by the document; the reader must tolerate this alias.
        bool loaded = bleturner::readJson(c, doc.as<JsonVariantConst>());
        JsonDocument again;
        bleturner::writeJson(c, again);
        text.clear();
        serializeJson(again, text);
        std::printf("reload %d %s\n", loaded, text.c_str());
      }
      for (const auto& event : allocator.trace) std::puts(event.c_str());
    }
  }
}

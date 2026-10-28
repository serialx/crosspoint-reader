#include <cstdio>
#include <string>

#include "BlePageTurnerJson.h"
#include "Fakes.h"
#include "Runtime.h"

using namespace bleturner;

static uint64_t digest = 14695981039346656037ULL;
static void add(uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    digest = (digest ^ (value & 255)) * 1099511628211ULL;
    value >>= 8;
  }
}

static void snapshot() {
  const auto s = status();
  std::printf("state %d%d%d%d%d%d%d%d%d why %u note %u heap %d clock %u calls %u/%u/%u/%u\n", s.compiledIn, s.running,
              s.stopping, s.starting, s.scanning, s.connected, s.connecting, s.idleStopped, s.readerDeferred,
              unsigned(why()), unsigned(linkNote()), holdsHeap(), fake::radio().now, fake::radio().beginCalls,
              fake::radio().endCalls, fake::radio().armCalls, fake::radio().polls);
  for (auto a : fake::host().delivered) std::printf("action %u\n", unsigned(a));
  for (const auto& log : fake::host().logs) std::printf("%s", log.c_str());
  fake::host().logs.clear();
  fake::host().delivered.clear();
}

int main() {
  for (const auto& p : {kX3Profile, kS3Profile}) {
    for (unsigned phase = 0; phase < 4; ++phase) {
      for (unsigned where = 0; where < 4; ++where) {
        for (unsigned bits = 0; bits < 1024; ++bits) {
          for (const size_t n : {0u, 24575u, 24576u, 32767u, 32768u, 65535u, 65536u, 75179u, 75180u, 81919u, 81920u}) {
            RadioInputs in{};
            in.phase = Phase(phase);
            in.where = Where(where);
            in.enabled = bits & 1;
            in.pageShown = bits & 2;
            in.bookIndexing = bits & 4;
            in.storageBusy = bits & 8;
            in.heldForBuild = bits & 16;
            in.idleStopped = bits & 32;
            in.linked = bits & 64;
            in.connectionBusy = bits & 128;
            in.hasBond = bits & 256;
            in.idleMs = bits & 512 ? 30000 : 29999;
            in.heap = {n, n};
            add(unsigned(radioVerdict(in, p)));
          }
        }
      }
    }
  }
  Config c;
  c.enabled = 1;
  for (unsigned prev = 0; prev < 256; ++prev) {
    c.prevKeyUsage = prev;
    c.nextKeyUsage = 255 - prev;
    for (unsigned key = 0; key < 256; ++key) {
      add(unsigned(pageActionFor(c, key, 0)));
      add(unsigned(pageActionFor(c, key, 1)));
    }
  }
  for (unsigned note = 0; note < 5; ++note) {
    for (unsigned bits = 0; bits < 256; ++bits) {
      for (uint32_t ms : {0u, 19999u, 20000u, 0xFFFFFFFFu}) {
        NoteInputs in{
            bool(bits & 1),  bool(bits & 2),  bool(bits & 4), bool(bits & 8), bool(bits & 16), bool(bits & 32), ms,
            bool(bits & 64), bool(bits & 128)};
        add(unsigned(nextLinkNote(LinkNote(note), in)));
      }
    }
  }
  std::printf("policy %016llx\n", static_cast<unsigned long long>(digest));
  for (const char* value :
       {"null", "false", "true", "-1", "0", "1", "255", "256", "4294967295", "1.5", "\"1\"", "[]", "{}"}) {
    std::string text = std::string("{\"blePageTurnerEnabled\":") + value + ",\"blePick\":" + value +
                       ",\"blePrevKeyUsage\":" + value + ",\"bleNextKeyUsage\":" + value + ",\"blePeerAddr\":" + value +
                       ",\"blePeerName\":" + value + ",\"bleRemotes\":[{\"addr\":\"AA\",\"binds\":[" + value +
                       ",268632065,553844744]}]}";
    JsonDocument doc;
    if (deserializeJson(doc, text)) return 1;
    Config config;
    bool loaded = readJson(config, doc.as<JsonVariantConst>());
    JsonDocument out;
    writeJson(config, out);
    std::string serialized;
    serializeJson(out, serialized);
    std::printf("json %d %s\n", loaded, serialized.c_str());
    readJson(config, out.as<JsonVariantConst>());
    JsonDocument again;
    writeJson(config, again);
    std::string roundtrip;
    serializeJson(again, roundtrip);
    if (serialized != roundtrip) return 2;
  }
  for (unsigned scenario = 0; scenario < 8; ++scenario) {
    fake::reset();
    Config config;
    config.enabled = 1;
    std::snprintf(config.peerAddr, sizeof(config.peerAddr), "AA");
    begin(fake::hostFns(), config);
    fake::radio().queueStarts = true;
    auto scene = fake::reading();
    if (scenario == 1) fake::host().heap = {87308, 23540};
    if (scenario == 2) fake::host().heap = {60000, 32768};
    if (scenario == 3) fake::radio().createSucceeds = false;
    if (scenario == 4) fake::radio().beginResult = false;
    if (scenario == 5) scene.where = Where::Home;
    if (scenario == 6) fake::radio().armResult = false;
    if (scenario == 7) fake::host().transfer = true;
    std::printf("scenario %u\n", scenario);
    for (unsigned step = 0; step < 20; ++step) {
      fake::radio().now += 5001;
      if (step == 2) requestConnect();
      if (step == 4) {
        fake::radio().connected = fake::radio().running;
        fake::radio().addr = "AA";
        fake::radio().name = "Free3";
      }
      if (step == 5) {
        fake::radio().raw.push_back({3, 1, 2, true, 0, 0, false, fake::radio().now});
        fake::radio().keys.push_back({0x4F, 0, true});
      }
      if (step == 6) {
        fake::radio().connected = false;
        acknowledgeLinkNote();
      }
      if (step == 8) {
        scene.storageBusy = true;
      }
      if (step == 9) {
        scene.storageBusy = false;
        ++scene.visit;
      }
      if (step == 10) {
        std::printf("build %u\n", unsigned(beforeChapterBuild()));
        afterPaint();
      }
      if (step == 12) {
        std::printf("pair %d\n", pair("BB", "Free3-B"));
      }
      if (step == 13) {
        fake::radio().connected = true;
        fake::radio().addr = "BB";
        std::printf("service %d\n", service());
      }
      if (step == 14) {
        std::printf("forget %d\n", forget("BB"));
        disconnect();
      }
      if (step == 15) {
        std::printf("screen %d\n", beforeScreenChange(0, true));
      }
      if (step == 17) config.enabled = 0;
      std::printf("tick %d\n", tick(scene));
      if (fake::radio().startQueued) fake::radio().runStart();
      snapshot();
    }
    std::printf("sleep %d\n", beforeSleep(100));
  }
}

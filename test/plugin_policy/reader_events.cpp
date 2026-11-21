#include <HalStorage.h>
#include <esp_random.h>

#include <limits>

#include "util/PluginEvents.h"
#include "xp_epub_session.h"

extern "C" time_t time(time_t* p) {
  if (p) *p = 1734212345;
  return 1734212345;
}
#ifndef PLUGIN_READER_NATIVE_ONLY
struct EventPorts {
  const XpPluginSubscriber* (*subscribers)();
  XpPluginSequence* (*codec)(XpPluginEventStorage*, XpPluginEmitOps*);
  void (*append)(XpPluginAppendStorage*, void*, const XpPluginSubscriber*, XpPluginOps*);
};
extern "C" void xp_epub_test_events(const EventPorts*, uint8_t, XpAppText, XpAppText, const XpEpubSessionEvent*,
                                    int32_t, int32_t);
#endif
void file(const std::string& path, const std::string& bytes) { oracle::files[path] = {bytes.begin(), bytes.end()}; }
void nativeEvent(uint8_t kind, const char* path, const char* document, const XpEpubSessionEvent& e, int percent,
                 int endPercent) {
  if (kind == 0) {
    const pluginevents::Var vars[] = {{"book", path}};
    pluginevents::emit(pluginevents::Event::ReaderOpen, vars, 1);
  } else if (kind == 2) {
    char value[8], endValue[8];
    snprintf(value, sizeof(value), "%d", percent);
    snprintf(endValue, sizeof(endValue), "%d", endPercent);
    const pluginevents::Var vars[] = {{"book", path}, {"percent", value}, {"end_percent", endValue}};
    pluginevents::emit(pluginevents::Event::ReaderExit, vars, 3);
  } else {
    char start[24], end[24], duration[16], from[8], to[8];
    snprintf(start, sizeof(start), "%lld", static_cast<long long>(e.start_time));
    snprintf(end, sizeof(end), "%lld", static_cast<long long>(e.end_time));
    snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(e.duration_seconds));
    snprintf(from, sizeof(from), "%u", e.start_progress);
    snprintf(to, sizeof(to), "%u", e.end_progress);
    const pluginevents::Var vars[] = {
        {"book", path},          {"document", document},         {"start_time", start},
        {"end_time", end},       {"duration_seconds", duration}, {"start_progress_bp", from},
        {"end_progress_bp", to}, {"progress_scale", "10000"}};
    pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8);
  }
}
int main(int argc, char** argv) {
  const int scenario = argc > 1 ? atoi(argv[1]) : 0;
  const std::string manifest =
      scenario == 8
          ? R"({"events":{"reader.open":{"require_meta":"missing"},"reader.exit":{"require_meta":"id"},"sleep.enter":{}}})"
          : R"({"events":{"reader.open":{},"reader.session":{},"reader.exit":{},"sleep.enter":{}}})";
  oracle::dirs = {"/plugins"};
  for (int i = 0; i < (scenario == 6 ? 0 : scenario == 7 ? 8 : 1); ++i) {
    const std::string dir = "/plugins/p" + std::to_string(i);
    oracle::dirs.insert(dir);
    file(dir + "/device.json", manifest);
    if (scenario == 3) file(dir + "/events.jsonl", std::string(4097, 'q'));
  }
  pluginevents::refreshSubscriptions();
  if (scenario == 1) oracle::fault = "open";
  if (scenario == 2) oracle::fault = "write";
  const std::string path = scenario == 4   ? std::string(600, 'x')
                           : scenario == 8 ? "/books/Filtered.epub"
                                           : "/books/한글 \"quote\"\\line\n.epub";
  if (scenario == 8) file(path + ".meta.json", R"({"id":"42"})");
  const char* document = "0123456789abcdef0123456789abcdef";
  XpEpubSessionEvent event{1734212000, 1734212010, 10, 0, 10000};
  if (scenario == 5)
    event = {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(),
             std::numeric_limits<uint32_t>::max(), 65535, 65535};
#ifndef PLUGIN_READER_NATIVE_ONLY
  const EventPorts ports{pluginevents::subscriptionView, pluginevents::eventCodec, pluginevents::eventAppend};
#endif
  // Two cycles prove continuation across native sleep emission and subscriber fan-out.
  for (int cycle = 0; cycle < 2; ++cycle) {
    for (uint8_t kind = 0; kind < 3; ++kind) {
#ifdef PLUGIN_READER_NATIVE_ONLY
      nativeEvent(kind, path.c_str(), document, event, cycle ? 99 : 0, cycle ? 100 : 12);
#else
      xp_epub_test_events(&ports, kind, {reinterpret_cast<const uint8_t*>(path.c_str()), path.size()},
                          {reinterpret_cast<const uint8_t*>(document), strlen(document)}, &event, cycle ? 99 : 0,
                          cycle ? 100 : 12);
#endif
    }
    pluginevents::emit(pluginevents::Event::SleepEnter, nullptr, 0);
  }
  printf("NONCE_CALLS %u\n", pluginRandomCalls);
  for (const auto& line : oracle::trace) puts(line.c_str());
  for (const auto& [path, bytes] : oracle::files) {
    printf("FILE %s ", path.c_str());
    for (auto c : bytes) printf("%02x", c);
    puts("");
  }
}

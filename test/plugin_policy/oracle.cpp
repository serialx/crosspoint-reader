#include <CrossPointSettings.h>
#include <HalStorage.h>
#include <SecureHttpClient.h>
#include <components/UITheme.h>

#include <cassert>

#include "util/PluginEvents.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"

extern "C" time_t time(time_t* p) {
  if (p) *p = 1734212345;
  return 1734212345;
}
void file(const std::string& p, const std::string& s) { oracle::files[p] = {s.begin(), s.end()}; }
void response(int status, const std::string& text = "ok", bool complete = true) {
  oracle::Response r;
  r.status = status;
  r.chunks = {text};
  r.complete = complete;
  oracle::responses.push_back(r);
}
void dump() {
  for (const auto& s : oracle::trace) puts(s.c_str());
  for (const auto& [p, b] : oracle::files) {
    printf("FILE %s %zu ", p.c_str(), b.size());
    for (auto c : b) printf("%02x", c);
    puts("");
  }
}
int main(int argc, char** argv) {
  int c = argc > 1 ? atoi(argv[1]) : 0;
#ifdef PLUGIN_JSON_FAULTS
  json_fault::failAt = argc > 2 ? static_cast<size_t>(atoi(argv[2])) : 0;
#endif
  oracle::dirs = {"/plugins", "/plugins/demo"};
  const std::string dir = "/plugins/demo", queue = dir + "/events.jsonl";
  if (c == 50) {
    const std::string raw = "a b/%20?x=#&é\x7f\r\n";
    assert(pluginhttp::urlEncodeQuery(raw) == "a%20b%2F%2520%3Fx%3D%23%26%C3%A9%7F%0D%0A");
    for (unsigned byte = 0; byte <= 255; ++byte) {
      const std::string input(1, static_cast<char>(byte));
      const std::string url = pluginhttp::escapeUrl(input);
      const std::string encoded = pluginhttp::urlEncodeQuery(input);
      if (byte <= 0x20 || byte >= 0x7f) {
        assert(url == encoded);
      } else {
        assert(url.empty());
        const bool unreserved = (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') ||
                                (byte >= 'a' && byte <= 'z') || std::string("-_.~").find(input) != std::string::npos;
        assert(unreserved ? encoded == input : encoded.starts_with('%') && encoded.size() == 3);
      }
    }
    String out;
    response(200);
    assert(pluginhttp::request(nullptr, "https://example/" + raw, "POST", raw, {{"X-Test", raw}}, out, 16) == 200);
    const auto seen = [](const std::string& line) {
      return std::find(oracle::trace.begin(), oracle::trace.end(), line) != oracle::trace.end();
    };
    assert(seen("begin https://example/a%20b/%20?x=#&%C3%A9%7F%0D%0A"));
    assert(seen("header X-Test " + raw));
    assert(seen("request POST " + std::to_string(raw.size()) + " " +
                std::to_string(oracle::hash(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()))));
    return 0;
  }
  if (c == 49) {
    const std::string other = "/plugins/other";
    oracle::dirs.insert(other);
    file(
        dir + "/device.json",
        R"({"events":{"reader.exit":{"require_meta":42},"reader.open":{"require_meta":true},"sleep.enter":{"require_meta":"id","download":{"url":"https://image","dest":"/sleep.bmp"}},"book.downloaded":{"require_meta":"id","request":{"url":"https://send/{meta.id}"}}}})");
    file(other + "/device.json", R"({"events":{"sleep.enter":{"require_meta":"external"}}})");
    pluginevents::refreshSubscriptions();
    assert(!pluginevents::anySubscriber(pluginevents::Event::ReaderExit));
    assert(!pluginevents::anySubscriber(pluginevents::Event::ReaderOpen));
    const pluginevents::Var book = {"book", "/book.epub"};
    const pluginevents::Var path = {"path", "/book.epub"};
    pluginevents::emit(pluginevents::Event::SleepEnter, &book, 1);
    file("/book.epub.meta.json", R"({"id":""})");
    pluginevents::emit(pluginevents::Event::SleepEnter, &book, 1);
    file("/book.epub.meta.json", "{");
    pluginevents::emit(pluginevents::Event::SleepEnter, &book, 1);
    assert(!oracle::files.contains(queue));
    assert(!pluginevents::wantsConnectAny());
    file("/book.epub.meta.json", R"({"id":42})" + std::string(2048, ' '));
    pluginevents::emit(pluginevents::Event::SleepEnter, &book, 1);
    assert(!oracle::files.contains(queue));
    file("/book.epub.meta.json", R"({"id":42})");
    pluginevents::emit(pluginevents::Event::BookDownloaded, &path, 1);
    assert(oracle::files.contains(queue));
    assert(!pluginevents::wantsConnectAny());
    response(204);
    pluginevents::drain(nullptr);
    assert(std::find(oracle::trace.begin(), oracle::trace.end(), "begin https://send/42") != oracle::trace.end());
    oracle::trace.clear();
    const pluginevents::Var protectedBook = {"book", "/.crosspoint/wifi.json"};
    file("/.crosspoint/wifi.json.meta.json", R"({"id":"protected"})");
    pluginevents::emit(pluginevents::Event::SleepEnter, &protectedBook, 1);
    assert(!oracle::files.contains(queue));
    assert(oracle::trace.empty());
    file("/book.epub.meta.json", R"({"id":true,"external":[]})");
    pluginevents::emit(pluginevents::Event::SleepEnter, &book, 1);
    assert(!oracle::files.contains(queue));
    file("/book.epub.meta.json", R"({"id":"123"})");
    oracle::trace.clear();
    pluginevents::emit(pluginevents::Event::SleepEnter, &book, 1);
    assert(oracle::files.contains(queue));
    assert(!oracle::files.contains(other + "/events.jsonl"));
    assert(std::count(oracle::trace.begin(), oracle::trace.end(), "open /book.epub.meta.json 0 1") == 1);
    assert(pluginevents::wantsConnectAny());
    return 0;
  }
  std::string manifest =
      R"({"token":{"file":"token.json","path":"auth.token"},"config":{"file":"config.json"},"auth":{"type":"password","token_path":"access_token","request":{"url":"https://auth","body":"{cfg.secret}:{event.book}","headers":{"Auth":"{token}"}}},"events":{"unknown":{},"reader.exit":{"connect":true,"request":{"url":"https://send/{event.id}","headers":{"Auth":"{token}"},"body":"{event.book}|{event.percent}|{event.ts}|{cfg.secret}|{meta.id}"},"toast":"Sent {event.book}"},"sleep.enter":{"download":{"url":"https://image","dest":"/sleep.bmp"}}}})";
#ifdef PLUGIN_TOKEN_LIFETIME
  if (c == 48)
    manifest =
        R"({"token":{"file":"token.json","path":"auth.token"},"auth":{"type":"password","token_path":"access_token","request":{"url":"https://auth"}},"events":{"reader.exit":{"request":{"url":"https://send","headers":{"Auth":"{token}"}},"toast":"{token}"}}})";
#endif
  if (c == 20) manifest = R"({"events":{"reader.exit":{}}})";
  if (c == 21) manifest = "{";
  if (c == 22)
    manifest = R"({"events":{"reader.exit":{"download":{"url":"https://image","dest":"/.crosspoint/wifi.json"}}}})";
  if (c == 23) manifest = R"({"events":{"reader.exit":{"download":{"url":"https://image","dest":"/book.epub"}}}})";
  if (c == 38)
    manifest =
        R"({"events":{"reader.exit":{"download":{"url":"","dest":"/empty.bmp"},"request":{"url":"https://ignored"}}}})";
  if (c == 24)
    manifest = R"({"events":{"reader.exit":{"download":{"url":"https://image"},"request":{"url":"https://ignored"}}}})";
  file(dir + "/device.json", manifest);
  file(dir + "/token.json", R"({"auth":{"token":"old"}})");
  file(dir + "/config.json", R"({"secret":"value","n":42,"b":true,"nested":{},"null":null})");
  file("/book.epub.meta.json", R"({"id":"123"})");
  if (c == 25) SETTINGS.pluginsEnabled = false;
  if (c == 26) file(dir + "/.installing", "");
  if (c == 27) {
    oracle::dirs.insert("/.crosspoint/plugins");
    oracle::dirs.insert("/.crosspoint/plugins/demo");
  }
  if (c == 34) {
    for (int i = 0; i < 10; i++) {
      const auto p = "/plugins/a" + std::to_string(i);
      oracle::dirs.insert(p);
      file(p + "/device.json", manifest);
    }
  }
  if (c == 35) {
    const std::string p = "/plugins/abcdefghijklmnopqrstuvwxyz";
    oracle::dirs.insert(p);
    file(p + "/device.json", manifest);
  }
  if (c == 36) {
    const std::string p = "/plugins/abcdefghijklmnopqrstuvw";
    oracle::dirs.insert(p);
    file(p + "/device.json", manifest);
  }
  if (c == 41) file(dir + "/token.json", R"({"auth":{"token":123456789}})");
  if (c == 42) file(dir + "/config.json", std::string(2049, 'x'));
  pluginevents::refreshSubscriptions();
  printf("MASK %u ANY %d\n", pluginevents::subscriptionMask("demo"),
         pluginevents::anySubscriber(pluginevents::Event::ReaderExit));
  const pluginevents::Var vars[] = {{"book", "/book.epub"}, {"percent", "74"}};
  if (c == 1) file(queue, std::string(4097, 'x'));
  if (c == 2) file(queue, std::string(4096, 'x'));
  if (c == 3) oracle::fault = "open";
  if (c == 4) oracle::fault = "write";
  if (c == 37) {
    const pluginevents::Var duplicates[] = {{"book", "first"}, {"book", "second\"\n"}, {"percent", ""}};
    pluginevents::emit(pluginevents::Event::ReaderExit, duplicates, 3);
  } else if (c == 5) {
    std::string big(550, 'x');
    pluginevents::Var v = {"book", big.c_str()};
    pluginevents::emit(pluginevents::Event::ReaderExit, &v, 1);
  } else
    pluginevents::emit(pluginevents::Event::ReaderExit, vars, 2);
  oracle::fault.clear();
  pluginevents::emit(pluginevents::Event::SleepEnter, vars, 2);
  if (c == 6) file(queue, "\ninvalid\n{\"e\":\"removed\"}\n{\"e\":\"reader.exit\",\"vars\":{\"book\":\"/book.epub\"}}");
  if (c == 46) file(queue, std::string(4609, 'x'));
  printf("CONNECT %d\n", pluginevents::wantsConnectAny());
#ifdef PLUGIN_TOKEN_LIFETIME
  if (c == 48) {
    response(401);
    response(200, "{\"access_token\":\"" + std::string(8173, 't') + "\"}");
    response(204);
  } else
#endif
      if (c == 7 || c == 40) {
    response(401);
    response(200, R"({"access_token":"fresh"})");
    response(204);
  } else if (c == 8) {
    response(403);
    response(200, R"({"access_token":"fresh"})");
    response(500);
  } else if (c == 9) {
    response(401);
    response(200, "{}");
  } else if (c == 10)
    response(500);
  else if (c == 11)
    response(200, "partial", false);
  else
    response(200);
  response(c == 12 ? 404 : 200, "image");
  response(200);
  file("/sleep.bmp", "old-image");
  if (c == 13) oracle::fault = "backup";
  if (c == 14) oracle::fault = "swap";
  if (c == 15) oracle::fault = "rename";
  if (c == 39) oracle::fault = "restore";
  if (c == 16) oracle::fault = "rewrite";
  if (c == 17) oracle::fault = "read";
  if (c == 18) oracle::fault = "read-oom";
  if (c == 19 || c == 40) oracle::fault = "write";
  if (c == 44) file(dir + "/device.json", "{");
  if (c == 45) file(dir + "/device.json", R"({"events":{}})");
  if (c == 47) SETTINGS.pluginsEnabled = false;
  GfxRenderer renderer;
  pluginevents::drain(&renderer, (c == 16 || c == 43) ? 1 : c == 28 ? 0 : 4);
  if (c >= 29 && c <= 33) {
    JsonDocument doc;
    deserializeJson(doc, R"({"a":[{"n":123}],"s":"x","b":true,"f":1.5,"null":null})");
    for (const auto* path :
         {"a.0.n", "..a..0x.n.", "a.42949672960.n", "a.184467440737095516160.n", "b", "f", "null", "missing", "s"})
      printf("VALUE %s %s\n", path, pluginhttp::variantToString(pluginhttp::resolvePath(doc, path)).c_str());
    std::string s = "{x}{x}-{y}";
    pluginhttp::substituteAll(s, "{x}", "{x}!");
    printf("SUB %s\n", s.c_str());
    printf("URL %s\n", pluginhttp::urlEncodeQuery("a /+_~é").c_str());
    for (const auto* path : {"a..b.", "", "0.1"})
      printf("TOKEN %s %d\n", path, pluginhttp::saveTokenToFile(dir + "/save.json", path, "q\"\n"));
    if (c == 30) oracle::fault = "write";
    if (c == 31) oracle::fault = "reserve";
    if (c == 32) oracle::fault = "concat";
    String out = "prior";
    response(400, "error");
    oracle::responseIndex = oracle::responses.size() - 1;
    printf("HTTP %d\n", pluginhttp::request(nullptr, "https://example", "GET", "", {}, out, c == 33 ? 2 : 16));
    printf("BODY %s\n", out.c_str());
    printf("SAVE %d\n", pluginhttp::saveTokenToFile(dir + "/save.json", "a.b", "new"));
  }
  dump();
#ifdef PLUGIN_TOKEN_LIFETIME
  printf("MEM toast_peak=%zu toast_live=%zu toast_large=%zu\n", allocation_probe::peak, allocation_probe::toastLive,
         allocation_probe::toastLarge);
#endif
#ifdef PLUGIN_JSON_FAULTS
  printf("MEM json_attempts=%zu peak_bytes=%zu live_bytes=%zu\n", json_fault::attempts, json_fault::peak,
         json_fault::live);
#endif
}

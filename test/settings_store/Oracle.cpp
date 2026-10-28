#include <BufferedFile.h>
#include <CredentialIntegrity.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>
#include "CrossPointState.h"
#include "CrossPointSettings.h"
#include <StoreTestSettings.h>
#include "OpdsServerStore.h"
#include "OpdsTokenStore.h"
#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"
#include <iostream>

extern "C" { volatile unsigned char mbedtls_ct_zero = 0; }

namespace PluginLocations { bool anyPluginInstalledOnDisk() { return false; } }

namespace {
template <typename T> void roundTrip(const std::string& input, JsonDocument& result) {
  auto& store = T::getInstance();
  JsonDocument empty;
  deserializeJson(empty, "{}");
  store.fromJson(empty.as<JsonVariantConst>());
  store_test::logs.clear();
  store_test::io.clear();
  store_test::files.clear();
  store_test::files[T::getFilePath()] = input;
  result["ok"] = store.loadFromFile();
  JsonDocument state;
  store.toJson(state);
  std::string bytes;
  serializeJson(state, bytes);
  result["state"] = bytes;
  result["file"] = static_cast<std::string>(store_test::files[T::getFilePath()]);
}
void actions(JsonDocument& result, bool failWrite) {
  JsonDocument empty;
  deserializeJson(empty, "{}");
  auto& recent = RecentBooksStore::getInstance();
  auto& wifi = WifiCredentialStore::getInstance();
  auto& opds = OpdsServerStore::getInstance();
  auto& tokens = OpdsTokenStore::getInstance();
  recent.fromJson(empty.as<JsonVariantConst>()); wifi.fromJson(empty.as<JsonVariantConst>());
  opds.fromJson(empty.as<JsonVariantConst>()); tokens.fromJson(empty.as<JsonVariantConst>());
  store_test::logs.clear(); store_test::io.clear(); store_test::files.clear();
  store_test::failWrite = failWrite;
  JsonArray steps = result["steps"].to<JsonArray>();
  auto record = [&](const char* operation, bool ok, const auto& store) {
    JsonObject entry = steps.add<JsonObject>(); entry["op"] = operation; entry["ok"] = ok;
    JsonDocument doc; store.toJson(doc);
    std::string text; serializeJson(doc, text); entry["state"] = text;
    entry["file"] = static_cast<std::string>(store_test::files[store.getFilePath()]);
  };
  for (int i = 0; i < 12; ++i) {
    std::string path = "/book" + std::to_string(i);
    store_test::files[path] = "exists";
    recent.addBook(path, "title", "author", "/old/cover"); record("recent-add", true, recent);
  }
  recent.addBook("/book5", "new", "writer", "/old/cover"); record("recent-reorder", true, recent);
  recent.updateBook("/book6", "newer", "a", "c"); record("recent-update", true, recent);
  recent.updateBook("/absent", "unused", "a", "c"); record("recent-absent", true, recent);
  recent.updatePath("/book5", "/renamed", "/old", "/new"); record("recent-path", true, recent);
  record("recent-prune", recent.pruneMissing(), recent);
  record("recent-remove", recent.removeByPath("/book6"), recent);
  record("recent-missing-remove", recent.removeByPath("/absent"), recent);
  for (int i = 0; i < 10; ++i) record("wifi-add", wifi.addCredential("ssid" + std::to_string(i), "password"), wifi);
  record("wifi-update", wifi.addCredential("ssid0", "changed"), wifi);
  wifi.setLastConnectedSsid("ssid0"); record("wifi-last", true, wifi);
  wifi.setLastConnectedSsid("ssid0"); record("wifi-same-last", true, wifi);
  record("wifi-remove", wifi.removeCredential("ssid0"), wifi);
  record("wifi-missing", wifi.removeCredential("absent"), wifi);
  result["wifi-found"] = wifi.findCredential("ssid1").has_value();
  result["wifi-missing"] = wifi.findCredential("absent").has_value();
  for (int i = 0; i < 10; ++i) {
    OpdsServer server; server.name = std::to_string(i); server.url = "https://server/" + server.name;
    server.password = "secret";
    record("opds-add", opds.addServer(server), opds);
  }
  OpdsServer changed; changed.name = "changed"; changed.customHeaders[0] = {"X", "value"};
  record("opds-update", opds.updateServer(0, changed), opds);
  record("opds-bounds", opds.updateServer(99, changed), opds);
  record("opds-remove", opds.removeServer(0), opds);
  for (int i = 0; i < 10; ++i) record("token-add", tokens.put(std::to_string(i), {"access", "refresh", "url"}), tokens);
  record("token-same", tokens.put("9", {"access", "refresh", "url"}), tokens);
  record("token-update", tokens.put("9", {"access2", "refresh", "url"}), tokens);
  record("token-clear", tokens.put("9", {}), tokens);
  record("token-missing-clear", tokens.put("missing", {}), tokens);
  result["token-found"] = tokens.get("8").accessToken;
  result["token-missing"] = tokens.get("0").accessToken;
  store_test::failWrite = false;
}

void buffered(JsonDocument& result, size_t capacity, bool failAlloc, bool shortIo) {
  store_test::failAlloc = failAlloc;
  HalFile file;
  file.writeLimit = shortIo ? 3 : SIZE_MAX;
  {
    serialization::BufferedFileWriter out(file, capacity);
    out.write("abcdef", 6);
    out.write("g", 1);
    out.write("hijk", 4);
    result["writePos"] = out.position();
    result["writeOk"] = out.flush();
  }
  file.pos = 0;
  file.readLimit = shortIo ? 2 : -1;
  serialization::BufferedFileReader in(file, capacity);
  JsonArray actions = result["actions"].to<JsonArray>();
  for (size_t target : {size_t(0), size_t(2), size_t(8), size_t(1), size_t(11)}) {
    actions.add(in.seek(target));
    char data[8] = {};
    const auto got = in.read(data, sizeof(data));
    actions.add(std::string(data, got));
    actions.add(in.position());
  }
  file.failSeek = true;
  actions.add(in.seek(100));
  actions.add(in.position());
  result["bytes"] = std::string(file.bytes.begin(), file.bytes.end());
}
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string kind = argv[1];
  std::string input;
  while (std::getline(std::cin, input)) {
    store_test::logs.clear(); store_test::io.clear();
    JsonDocument result;
    if (kind == "settings" || kind == "settings-home") {
      store_test::hasHome = kind == "settings-home";
      roundTrip<CrossPointSettings>(input, result);
    } else if (kind == "state") roundTrip<CrossPointState>(input, result);
    else if (kind == "recent") roundTrip<RecentBooksStore>(input, result);
    else if (kind == "wifi") roundTrip<WifiCredentialStore>(input, result);
    else if (kind == "opds") roundTrip<OpdsServerStore>(input, result);
    else if (kind == "tokens") roundTrip<OpdsTokenStore>(input, result);
    else if (kind == "obf") {
      bool ok = false, tooLong = false;
      auto data = obfuscation::deobfuscateFromBase64(input.c_str(), 64, &ok, &tooLong);
      result["ok"] = ok; result["tooLong"] = tooLong;
      result["encoded"] = static_cast<std::string>(obfuscation::obfuscateToBase64(data));
      result["crc"] = credential_integrity::crc32(data);
    } else if (kind == "actions") {
      actions(result, input == "1");
    } else if (kind == "buffer") {
      const auto n = std::stoul(input);
      buffered(result, n / 4, n & 1, n & 2);
    } else return 2;
    JsonArray logs = result["logs"].to<JsonArray>();
    for (const auto& log : store_test::logs) logs.add(log);
    JsonArray io = result["io"].to<JsonArray>();
    for (const auto& op : store_test::io) io.add(op);
    std::string out; serializeJson(result, out); std::cout << out << '\n';
  }
}

#include <BufferedFile.h>
#include <CredentialIntegrity.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>
#include "CrossPointState.h"
#include "OpdsServerStore.h"
#include "OpdsTokenStore.h"
#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"
#include <iostream>

extern "C" { volatile unsigned char mbedtls_ct_zero = 0; }

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
    if (kind == "state") roundTrip<CrossPointState>(input, result);
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

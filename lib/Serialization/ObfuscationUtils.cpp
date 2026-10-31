#include "ObfuscationUtils.h"

#include <Logging.h>
#include <base64.h>
#include <esp_mac.h>
#include <mbedtls/base64.h>
#include <xp_settings.h>

#include <array>
#include <cstring>
#include <limits>

namespace obfuscation {

namespace {
constexpr size_t HW_KEY_LEN = 6;

const uint8_t* getHwKey() {
  // Function-local static initialization is synchronized by C++, including
  // on dual-core targets. The previous hand-rolled boolean could expose a
  // partially initialized key to another task.
  static const std::array<uint8_t, HW_KEY_LEN> key = [] {
    std::array<uint8_t, HW_KEY_LEN> value{};
    esp_efuse_mac_get_default(value.data());
    return value;
  }();
  return key.data();
}
}  // namespace

void xorTransform(std::string& data) {
  xp_store_xor(reinterpret_cast<uint8_t*>(data.data()), data.size(), getHwKey(), HW_KEY_LEN);
}

void xorTransform(std::string& data, const uint8_t* key, size_t keyLen) {
  xp_store_xor(reinterpret_cast<uint8_t*>(data.data()), data.size(), key, keyLen);
}

String obfuscateToBase64(const std::string& plaintext) {
  if (plaintext.empty()) return "";
  std::string temp = plaintext;
  xorTransform(temp);
  return base64::encode(reinterpret_cast<const uint8_t*>(temp.data()), temp.size());
}

std::string deobfuscateFromBase64(const char* encoded, bool* ok) {
  return deobfuscateFromBase64(encoded, std::numeric_limits<size_t>::max(), ok, nullptr);
}

std::string deobfuscateFromBase64(const char* encoded, const size_t maxDecodedLength, bool* ok, bool* tooLong) {
  std::string result;
  const XpStoreDecodeOps ops{
      &result, getHwKey,
      [](void* ctx, size_t size) {
        auto& value = *static_cast<std::string*>(ctx);
        value.resize(size);
        return reinterpret_cast<uint8_t*>(value.data());
      },
      [](uint8_t* dst, size_t capacity, size_t* size, const uint8_t* src, size_t len) -> int32_t {
        return mbedtls_base64_decode(dst, capacity, size, src, len);
      }};
  int32_t error = 0;
  const auto status = xp_store_decode(reinterpret_cast<const uint8_t*>(encoded), encoded ? strlen(encoded) : 0,
                                      maxDecodedLength, &ops, &error);
  if (ok) *ok = status == 0;
  if (tooLong) *tooLong = status == 3;
  if (status == 2) LOG_ERR("OBF", "Base64 decode size query failed (ret=%d)", static_cast<int>(error));
  if (status == 4) LOG_ERR("OBF", "Base64 decode failed (ret=%d)", static_cast<int>(error));
  if (status != 0) return {};
  return result;
}

void selfTest() {
  const char* testInputs[] = {"", "hello", "WiFi P@ssw0rd!", "a"};
  bool allPassed = true;
  for (const char* input : testInputs) {
    String encoded = obfuscateToBase64(std::string(input));
    std::string decoded = deobfuscateFromBase64(encoded.c_str());
    if (decoded != input) {
      LOG_ERR("OBF", "FAIL: \"%s\" -> \"%s\" -> \"%s\"", input, encoded.c_str(), decoded.c_str());
      allPassed = false;
    }
  }
  // Verify obfuscated form differs from plaintext
  String enc = obfuscateToBase64("test123");
  if (enc == "test123") {
    LOG_ERR("OBF", "FAIL: obfuscated output identical to plaintext");
    allPassed = false;
  }
  if (allPassed) {
    LOG_DBG("OBF", "Obfuscation self-test PASSED");
  }
}

}  // namespace obfuscation

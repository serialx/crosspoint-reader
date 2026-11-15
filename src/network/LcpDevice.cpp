#include "LcpDevice.h"

#include <ArduinoJson.h>
#include <LcpLicense.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <PersistableStore.h>
#include <StreamingJsonParser.h>
#include <Util.h>
#include <WolfsslCrypto.h>
#include <base64.h>
#include <xp_ffi.h>

#include <cstring>

#include "network/HttpDownloader.h"

// Injected from CI secrets into official release builds; self-compiled
// firmware carries the empty default and does everything except LCP.
// Deliberately a plain constant: this is effort asymmetry with per-release
// rotation, not a trust boundary (see /enroll on the service side).
#ifndef FREEINK_LCP_RELEASE_KEY
#define FREEINK_LCP_RELEASE_KEY ""
#endif

namespace lcpdevice {

namespace {

constexpr char RELEASE_KEY[] = FREEINK_LCP_RELEASE_KEY;
constexpr const char* LCP_ENROLL_URL = "https://lcp.freeink.org/enroll";

// One enrolled identity per device, persisted like the other credential
// stores: the private key obfuscated with the device key, the id plain.
class LcpDeviceStore : public PersistableStore<LcpDeviceStore> {
 private:
  LcpDeviceStore() = default;
  friend class PersistableStore<LcpDeviceStore>;

 public:
  std::string deviceId;
  std::string privHex;  // 64 hex chars

  static const char* getFilePath() { return "/.crosspoint/lcp_device.json"; }
  void toJson(JsonDocument& doc) const {
    doc["device_id"] = deviceId;
    doc["priv_obf"] = obfuscation::obfuscateToBase64(privHex);
  }
  bool fromJson(JsonVariantConst doc) {
    deviceId = doc["device_id"] | "";
    bool ok = false;
    bool tooLong = false;
    privHex = obfuscation::deobfuscateFromBase64(doc["priv_obf"] | "", 64, &ok, &tooLong);
    return true;
  }
};

#define LCP_DEVICE LcpDeviceStore::getInstance()

bool loaded = false;
struct Context {
  std::string* id = nullptr;
  const std::string* encoded[4]{};
  std::string request;
  std::string response;
  int status = 0;
};
size_t operation(void* opaque, uint32_t op, size_t n, uint8_t* bytes) {
  auto& c = *static_cast<Context*>(opaque);
  switch (op) {
    case XP_LCP_LOAD:
      return LCP_DEVICE.loadFromFile();
    case XP_LCP_ID_LEN:
      return LCP_DEVICE.deviceId.size();
    case XP_LCP_PRIVATE_LEN:
      return LCP_DEVICE.privHex.size();
    case XP_LCP_EXPORT_ID:
      *c.id = LCP_DEVICE.deviceId;
      return 0;
    case XP_LCP_KEYGEN: {
      if constexpr (sizeof(RELEASE_KEY) == 1) return 0;
      freeink::content::WolfsslCrypto crypto;
      return crypto.x25519MakeKey(bytes, bytes + 32);
    }
    case XP_LCP_APPEND_REQUEST:
      if constexpr (sizeof(RELEASE_KEY) == 1) return 0;
      c.request.append(reinterpret_cast<const char*>(bytes), n);
      return 0;
    case XP_LCP_APPEND_PUBLIC:
      if constexpr (sizeof(RELEASE_KEY) == 1) return 0;
      c.request += base64::encode(bytes, n).c_str();
      return 0;
    case XP_LCP_POST:
      if constexpr (sizeof(RELEASE_KEY) == 1) return 0;
      return HttpDownloader::postForm(LCP_ENROLL_URL, c.request, c.response, &c.status);
    case XP_LCP_SET_ID:
      if constexpr (sizeof(RELEASE_KEY) == 1) return 0;
      c.id->assign(reinterpret_cast<const char*>(bytes), n);
      LCP_DEVICE.deviceId = *c.id;
      return 0;
    case XP_LCP_SET_PRIVATE:
      if constexpr (sizeof(RELEASE_KEY) == 1) return 0;
      LCP_DEVICE.privHex.assign(reinterpret_cast<const char*>(bytes), n);
      return 0;
    case XP_LCP_SAVE:
      return LCP_DEVICE.saveToFile();
    case XP_LCP_FORGET:
      LCP_DEVICE.deviceId.clear();
      LCP_DEVICE.privHex.clear();
      return 0;
    case XP_LCP_PRIVATE:
      memcpy(bytes, LCP_DEVICE.privHex.data(), n);
      return 0;
    case XP_LCP_DECODE: {
      constexpr size_t capacities[] = {32, 12, 32, 16};
      return freeink::content::base64Decode(c.encoded[n]->data(), c.encoded[n]->size(), bytes, capacities[n]);
    }
    case XP_LCP_UNWRAP: {
      auto** args = reinterpret_cast<uint8_t**>(bytes);
      freeink::content::WolfsslCrypto crypto;
      return freeink::content::lcpUnwrapContentKey(crypto, args[0], args[1], args[1] + 32, args[1] + 44, args[1] + 76,
                                                   args[2]);
    }
  }
  return 0;
}
bool parse(void* opaque, XpLcpParse* state) {
  if constexpr (sizeof(RELEASE_KEY) == 1) return false;
  auto& c = *static_cast<Context*>(opaque);
  JsonCallbacks callbacks{};
  callbacks.ctx = state;
  callbacks.onKey = [](void* p, const char* s, size_t n) {
    xp_lcp_json(static_cast<XpLcpParse*>(p), true, reinterpret_cast<const uint8_t*>(s), n);
  };
  callbacks.onString = [](void* p, const char* s, size_t n) {
    xp_lcp_json(static_cast<XpLcpParse*>(p), false, reinterpret_cast<const uint8_t*>(s), n);
  };
  callbacks.onNumber = [](void*, const char*, size_t) {};
  callbacks.onBool = [](void*, bool) {};
  callbacks.onNull = [](void*) {};
  callbacks.onObjectStart = [](void*) {};
  callbacks.onObjectEnd = [](void*) {};
  callbacks.onArrayStart = [](void*) {};
  callbacks.onArrayEnd = [](void*) {};
  StreamingJsonParser parser(callbacks);
  parser.feed(c.response.data(), c.response.size());
  return !parser.hasError();
}
void diagnostic(void* opaque, uint32_t event) {
  auto& c = *static_cast<Context*>(opaque);
  switch (event) {
    case 0:
      LOG_ERR("LCPDEV", "Keygen failed");
      break;
    case 1:
      LOG_ERR("LCPDEV", "Enroll failed (status %d)", c.status);
      break;
    case 2:
      LOG_ERR("LCPDEV", "Cannot persist device identity");
      break;
    case 3:
      LOG_INF("LCPDEV", "Enrolled as %s", c.id->c_str());
      break;
  }
}
XpLcpServices services(Context& c) {
  return {&c,
          operation,
          parse,
          sizeof(RELEASE_KEY) == 1 ? nullptr : diagnostic,
          &loaded,
          reinterpret_cast<const uint8_t*>(RELEASE_KEY),
          strlen(RELEASE_KEY)};
}
}  // namespace
bool ensureEnrolled(std::string& deviceId, bool& notEnrollable) {
  Context c;
  c.id = &deviceId;
  const auto api = services(c);
  if constexpr (sizeof(RELEASE_KEY) == 1) return xp_lcp_existing(&api, &notEnrollable);
  return xp_lcp_ensure(&api, &notEnrollable);
}
void forget() {
  Context c;
  const auto api = services(c);
  xp_lcp_forget(&api);
}
bool unwrapContentKey(const std::string& epk, const std::string& iv, const std::string& ct, const std::string& tag,
                      uint8_t out[32]) {
  Context c;
  c.encoded[0] = &epk;
  c.encoded[1] = &iv;
  c.encoded[2] = &ct;
  c.encoded[3] = &tag;
  const auto api = services(c);
  return xp_lcp_unwrap(&api, out);
}
}  // namespace lcpdevice

#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <StoreBridge.h>
#include <xp_settings.h>

#include <cstring>
#include <limits>

namespace {
struct DocumentIo {
  const char* path;
  const JsonDocument* output = nullptr;
  JsonDocument* input = nullptr;
  String json;
  DeserializationError error;

  XpStorePersistOps ops() {
    return {this, [](void* ctx, uint32_t op) {
              auto& self = *static_cast<DocumentIo*>(ctx);
              switch (op) {
                case XP_PERSISTLEAF_EXISTS:
                  return Storage.exists(self.path);
                case XP_PERSISTLEAF_READ:
                  self.json = Storage.readFile(self.path);
                  return !self.json.isEmpty();
                case XP_PERSISTLEAF_PARSE:
                  self.error = deserializeJson(*self.input, self.json);
                  return !self.error;
                case XP_PERSISTLEAF_MKDIR:
                  Storage.mkdir("/.crosspoint");
                  return true;
                case XP_PERSISTLEAF_SERIALIZE:
                  serializeJson(*self.output, self.json);
                  return true;
                case XP_PERSISTLEAF_WRITE:
                  return Storage.writeFile(self.path, self.json);
                default:
                  return false;
              }
            }};
  }
};
struct ReaderSave {
  std::unique_lock<std::mutex> lock;
  JsonDocument document;
  store_bridge::Json json{document};
  DocumentIo file;
  ReaderSave(std::mutex& mutex, const char* path) : lock(mutex), file{path, &document} {}
};
}  // namespace

void PersistableStoreBase::beginReaderSave(const char* path, void* storage, XpStoreJsonOps* json,
                                           XpStorePersistOps* file) const {
  static_assert(sizeof(ReaderSave) <= 48 * sizeof(uintptr_t) && alignof(ReaderSave) <= alignof(uintptr_t));
  auto* save = std::construct_at(static_cast<ReaderSave*>(storage), storeMutex, path);
  *json = save->json.ops();
  *file = save->file.ops();
}
void PersistableStoreBase::endReaderSave(void* storage) { std::destroy_at(static_cast<ReaderSave*>(storage)); }

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  DocumentIo io{path, &doc};
  const auto ops = io.ops();
  if (xp_store_document(&ops, true) != 0) {
    LOG_ERR("PERSIST", "Failed to write %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  DocumentIo io{path, nullptr, &doc};
  const auto ops = io.ops();
  const auto status = xp_store_document(&ops, false);
  if (status == 2) LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
  if (status == 3) LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, io.error.c_str());
  return status == 0;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  const char* legacyPassword = doc["password"] | "";
  const size_t legacyLength = strlen(legacyPassword);
  const auto action = xp_store_password_action(ok, tooLong, legacyLength, maxLength);
  valid = action != 0;
  if (!valid) return "";
  if (action == 2) {
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  return pass;
}

#include "DictionaryRegistry.h"

#include <HalStorage.h>
#include <Logging.h>
#include <xp_ffi.h>

#include <algorithm>
#include <cstring>

#include "DictionaryIo.h"
#include "StringUtils.h"

namespace DictionaryRegistry {
namespace {
struct Context {
  HalFile directories[2];
  HalFile entries[2];
  std::vector<DictionaryEntry>* output = nullptr;
  std::string* resolved = nullptr;
};

XpDictionaryRegistry services(Context& context) {
  return {&context,
          [](void* ctx, uint8_t slot, const uint8_t* path) {
            auto& directory = static_cast<Context*>(ctx)->directories[slot];
            directory = Storage.open(reinterpret_cast<const char*>(path));
            if (!directory || !directory.isDirectory()) return false;
            directory.rewindDirectory();
            return true;
          },
          [](void* ctx, uint8_t slot, uint8_t* name) -> uint8_t {
            auto& c = *static_cast<Context*>(ctx);
            c.entries[slot] = c.directories[slot].openNextFile();
            if (!c.entries[slot]) return XP_DICTIONARY_REGISTRY_KIND_END;
            c.entries[slot].getName(reinterpret_cast<char*>(name), XP_DICTIONARY_REGISTRY_NAME_BYTES);
            return c.entries[slot].isDirectory() ? XP_DICTIONARY_REGISTRY_KIND_DIRECTORY
                                                 : XP_DICTIONARY_REGISTRY_KIND_FILE;
          },
          [](void* ctx, uint8_t slot) {
            auto& c = *static_cast<Context*>(ctx);
            c.entries[slot] = HalFile{};
            c.directories[slot] = HalFile{};
          },
          [](void*, const uint8_t* path) { return Storage.exists(reinterpret_cast<const char*>(path)); },
          [](void* ctx, const uint8_t* name, size_t nameLen, const uint8_t* stem, size_t stemLen) {
            auto& c = *static_cast<Context*>(ctx);
            if (c.resolved) {
              c.resolved->assign(reinterpret_cast<const char*>(name), nameLen);
              *c.resolved += '/';
              c.resolved->append(reinterpret_cast<const char*>(stem), stemLen);
            } else {
              DictionaryEntry entry;
              entry.stem.assign(reinterpret_cast<const char*>(stem), stemLen);
              entry.name.assign(reinterpret_cast<const char*>(name), nameLen);
              c.output->push_back(std::move(entry));
            }
          },
          [](void*, uint8_t event, const uint8_t* text) {
            const auto* path = reinterpret_cast<const char*>(text);
            switch (static_cast<DictionaryRegistryEvent>(event)) {
              case DictionaryRegistryEvent::Ambiguous:
                LOG_DBG("DREG", "Skipping %s: multiple index stems found", path);
                break;
              case DictionaryRegistryEvent::MissingData:
                LOG_DBG("DREG", "Skipping %s: no .dict or .dict.dz", path);
                break;
              case DictionaryRegistryEvent::MissingRoot:
                LOG_DBG("DREG", "No %s directory on SD card", path);
                break;
              case DictionaryRegistryEvent::Found:
                LOG_DBG("DREG", "Found dictionary: %s", path);
                break;
            }
          }};
}
}  // namespace

void discover(std::vector<DictionaryEntry>& out) {
  out.clear();
  out.reserve(8);
  Context context;
  context.output = &out;
  const auto io = services(context);
  xp_dictionary_registry(&io, nullptr, 0, true);
  std::sort(out.begin(), out.end(), [](const DictionaryEntry& a, const DictionaryEntry& b) {
    return StringUtils::asciiCaseCmp(a.name.c_str(), b.name.c_str()) < 0;
  });
}

bool resolveBasePath(const char* folderName, std::string& basePathOut) {
  Context context;
  context.resolved = &basePathOut;
  const auto io = services(context);
  return xp_dictionary_registry(&io, reinterpret_cast<const uint8_t*>(folderName), folderName ? strlen(folderName) : 0,
                                false);
}
}  // namespace DictionaryRegistry

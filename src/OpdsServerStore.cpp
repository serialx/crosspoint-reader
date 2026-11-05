#include "OpdsServerStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>
#include <StoreBridge.h>

#include <algorithm>
#include <cstring>

namespace {
template <typename T>
std::string& rowField(T& row, uint32_t field) {
  switch (field) {
    case 0:
      return row.name;
    case 1:
      return row.url;
    case 2:
      return row.username;
    case 3:
      return row.password;
    case 4:
      return row.customHeaders[0].name;
    case 5:
      return row.customHeaders[0].value;
    case 6:
      return row.customHeaders[1].name;
    case 7:
      return row.customHeaders[1].value;
    default:
      return row.name;
  }
}

}  // namespace

void OpdsServerStore::toJson(JsonDocument& doc) const {
  store_bridge::Rows<OpdsServer, XP_STOREKIND_OPDS> rows{const_cast<std::vector<OpdsServer>&>(servers), nullptr,
                                                         rowField<OpdsServer>, nullptr};
  store_bridge::save(XP_STOREKIND_OPDS, doc, rows);
}

bool OpdsServerStore::fromJson(JsonVariantConst doc) {
  OpdsServer scratch;
  store_bridge::Rows<OpdsServer, XP_STOREKIND_OPDS> rows{servers, &scratch, rowField<OpdsServer>, nullptr};
  const auto diagnostic = +[](void* ctx, uint32_t event, size_t a, size_t b, size_t) {
    (void)ctx;
    (void)b;
    switch (event) {
      case XP_ROWEVENT_LOADED:
        LOG_DBG("OPS", "Loaded %zu OPDS servers from file", a);
        break;
      case XP_ROWEVENT_RESAVE:
        LOG_DBG("OPS", "Resaving JSON with obfuscated passwords");
        break;
      default:
        break;
    }
  };
  if (store_bridge::load(XP_STOREKIND_OPDS, doc, rows, diagnostic)) requestResave();
  return true;
}

bool OpdsServerStore::addServer(const OpdsServer& server) {
  if (!xp_store_index_action(servers.size(), 0, true)) {
    LOG_DBG("OPS", "Cannot add more servers, limit of %zu reached", MAX_SERVERS);
    return false;
  }

  servers.push_back(server);
  LOG_DBG("OPS", "Added server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::updateServer(size_t index, const OpdsServer& server) {
  if (!xp_store_index_action(servers.size(), index, false)) {
    return false;
  }

  servers[index] = server;
  LOG_DBG("OPS", "Updated server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::removeServer(size_t index) {
  if (!xp_store_index_action(servers.size(), index, false)) {
    return false;
  }

  LOG_DBG("OPS", "Removed server: %s", servers[index].name.c_str());
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const OpdsServer* OpdsServerStore::getServer(size_t index) const {
  if (!xp_store_index_action(servers.size(), index, false)) {
    return nullptr;
  }
  return &servers[index];
}

std::vector<HttpDownloader::Header> OpdsServer::activeCustomHeaders() const {
  std::vector<HttpDownloader::Header> active;
  active.reserve(MAX_CUSTOM_HEADERS);
  for (const auto& header : customHeaders) {
    if (!header.name.empty()) active.emplace_back(header.name, header.value);
  }
  return active;
}

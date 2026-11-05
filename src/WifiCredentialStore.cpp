#include "WifiCredentialStore.h"

#include <CredentialIntegrity.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <StoreBridge.h>

#include <algorithm>

namespace {
template <typename T>
std::string& rowField(T& row, uint32_t field) {
  switch (field) {
    case 0:
      return row.ssid;
    case 1:
      return row.password;
    default:
      return row.ssid;
  }
}

}  // namespace

void WifiCredentialStore::toJson(JsonDocument& doc) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI> rows{const_cast<std::vector<WifiCredential>&>(credentials),
                                                             nullptr, rowField<WifiCredential>,
                                                             const_cast<std::string*>(&lastConnectedSsid)};
  store_bridge::save(XP_STOREKIND_WIFI, doc, rows);
}

bool WifiCredentialStore::fromJson(JsonVariantConst doc) {
  std::lock_guard<std::mutex> lock(credentialMutex);
  WifiCredential scratch;
  store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI> rows{credentials, &scratch, rowField<WifiCredential>,
                                                             &lastConnectedSsid};
  const auto diagnostic = +[](void* ctx, uint32_t event, size_t a, size_t b, size_t) {
    const auto& ssid =
        static_cast<store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI>*>(static_cast<store_bridge::RowStrings*>(ctx))
            ->scratch->ssid;
    switch (event) {
      case XP_ROWEVENT_INVALIDLENGTH:
        LOG_ERR("WCS", "Discarding corrupted password for %s (invalid length)", ssid.c_str());
        break;
      case XP_ROWEVENT_OVERSIZELENGTH:
        LOG_ERR("WCS", "Discarding oversized password for %s (%zu bytes)", ssid.c_str(), a);
        break;
      case XP_ROWEVENT_OVERSIZEPASSWORD:
        LOG_ERR("WCS", "Discarding oversized password for %s", ssid.c_str());
        break;
      case XP_ROWEVENT_LENGTHMISMATCH:
        LOG_ERR("WCS", "Discarding corrupted password for %s (expected %zu bytes, decoded %zu)", ssid.c_str(), a, b);
        break;
      case XP_ROWEVENT_CHECKSUMMISMATCH:
        LOG_ERR("WCS", "Discarding corrupted password for %s (checksum mismatch)", ssid.c_str());
        break;
      case XP_ROWEVENT_INVALIDCHECKSUM:
        LOG_ERR("WCS", "Discarding corrupted password for %s (invalid checksum)", ssid.c_str());
        break;
      case XP_ROWEVENT_LOADED:
        LOG_DBG("WCS", "Loaded %zu WiFi credentials from file", a);
        break;
      case XP_ROWEVENT_RESAVE:
        LOG_DBG("WCS", "Resaving JSON with obfuscated passwords");
        break;
      default:
        break;
    }
  };
  if (store_bridge::load(XP_STOREKIND_WIFI, doc, rows, diagnostic)) requestResave();
  return true;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    WifiCredential scratch;
    store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI> rows{credentials, &scratch, rowField<WifiCredential>};
    const auto ops = rows.ops(nullptr);
    const XpStoreBytes args[] = {store_bridge::bytes(ssid), store_bridge::bytes(password)};
    const auto action = xp_store_mutate(&ops, XP_MUTATION_WIFIUPSERT, args);
    if (action == XP_MUTATIONRESULT_REJECTED) {
      LOG_DBG("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
      return false;
    }
    if (action == XP_MUTATIONRESULT_UPDATED) LOG_DBG("WCS", "Updated credentials for: %s", ssid.c_str());
    if (action == XP_MUTATIONRESULT_INSERTED) LOG_DBG("WCS", "Added credentials for: %s", ssid.c_str());
  }
  return saveToFile();
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI> rows{credentials, nullptr, rowField<WifiCredential>,
                                                               &lastConnectedSsid};
    const auto ops = rows.ops(nullptr);
    const auto key = store_bridge::bytes(ssid);
    if (xp_store_mutate(&ops, XP_MUTATION_WIFIREMOVE, &key) == XP_MUTATIONRESULT_REJECTED) return false;
    LOG_DBG("WCS", "Removed credentials for: %s", ssid.c_str());
  }
  return saveToFile();
}

std::optional<WifiCredential> WifiCredentialStore::findCredential(const std::string& ssid) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI> rows{const_cast<std::vector<WifiCredential>&>(credentials),
                                                             nullptr, rowField<WifiCredential>};
  const auto ops = rows.ops(nullptr);
  const auto index = xp_store_find(&ops, 0, store_bridge::bytes(ssid));
  return index == SIZE_MAX ? std::nullopt : std::optional<WifiCredential>(credentials[index]);
}

std::optional<WifiCredential> WifiCredentialStore::getCredentialAt(const size_t index) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  if (index >= credentials.size()) return std::nullopt;
  return credentials[index];
}

std::optional<std::string> WifiCredentialStore::getSsidAt(const size_t index) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  if (index >= credentials.size()) return std::nullopt;
  return credentials[index].ssid;
}

size_t WifiCredentialStore::getCredentialCount() const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  return credentials.size();
}

std::vector<WifiCredentialSummary> WifiCredentialStore::getCredentialSummaries() const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  std::vector<WifiCredentialSummary> summaries;
  summaries.reserve(credentials.size());
  for (const auto& credential : credentials) {
    summaries.push_back({credential.ssid, !credential.password.empty(), credential.ssid == lastConnectedSsid});
  }
  return summaries;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI> rows{const_cast<std::vector<WifiCredential>&>(credentials),
                                                             nullptr, rowField<WifiCredential>};
  const auto ops = rows.ops(nullptr);
  return xp_store_find(&ops, 0, store_bridge::bytes(ssid)) != SIZE_MAX;
}

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) {
  {
    std::lock_guard<std::mutex> lock(credentialMutex);
    store_bridge::Rows<WifiCredential, XP_STOREKIND_WIFI> rows{credentials, nullptr, rowField<WifiCredential>,
                                                               &lastConnectedSsid};
    const auto ops = rows.ops(nullptr);
    if (!xp_store_last_connected(&ops, store_bridge::bytes(ssid))) return;
  }
  saveToFile();
}

std::string WifiCredentialStore::getLastConnectedSsid() const {
  std::lock_guard<std::mutex> lock(credentialMutex);
  return lastConnectedSsid;
}

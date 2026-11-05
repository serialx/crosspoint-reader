#include "OpdsTokenStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>
#include <StoreBridge.h>

#include <algorithm>

namespace {
template <typename T>
std::string& rowField(T& row, uint32_t field) {
  switch (field) {
    case 0:
      return row.url;
    case 1:
      return row.tokens.accessToken;
    case 2:
      return row.tokens.refreshToken;
    case 3:
      return row.tokens.refreshUrl;
    default:
      return row.url;
  }
}

}  // namespace

void OpdsTokenStore::toJson(JsonDocument& doc) const {
  store_bridge::Rows<Entry, XP_STOREKIND_TOKENS> rows{const_cast<std::vector<Entry>&>(entries), nullptr,
                                                      rowField<Entry>, nullptr};
  store_bridge::save(XP_STOREKIND_TOKENS, doc, rows);
}

bool OpdsTokenStore::fromJson(JsonVariantConst doc) {
  Entry scratch;
  store_bridge::Rows<Entry, XP_STOREKIND_TOKENS> rows{entries, &scratch, rowField<Entry>, nullptr};
  const auto diagnostic = +[](void* ctx, uint32_t event, size_t a, size_t b, size_t) {
    (void)ctx;
    (void)b;
    switch (event) {
      case XP_ROWEVENT_LOADED:
        LOG_DBG("OPDSTOK", "Loaded %zu OPDS token entries", a);
        break;
      default:
        break;
    }
  };
  if (store_bridge::load(XP_STOREKIND_TOKENS, doc, rows, diagnostic)) requestResave();
  return true;
}

OpdsTokens OpdsTokenStore::get(const std::string& url) const {
  store_bridge::Rows<Entry, XP_STOREKIND_TOKENS> rows{const_cast<std::vector<Entry>&>(entries), nullptr,
                                                      rowField<Entry>};
  const auto ops = rows.ops(nullptr);
  const auto index = xp_store_find(&ops, 0, store_bridge::bytes(url));
  return index == SIZE_MAX ? OpdsTokens{} : entries[index].tokens;
}

bool OpdsTokenStore::put(const std::string& url, const OpdsTokens& tokens) {
  Entry scratch;
  store_bridge::Rows<Entry, XP_STOREKIND_TOKENS> rows{entries, &scratch, rowField<Entry>};
  const auto ops = rows.ops(nullptr);
  const XpStoreBytes args[] = {store_bridge::bytes(url), store_bridge::bytes(tokens.accessToken),
                               store_bridge::bytes(tokens.refreshToken), store_bridge::bytes(tokens.refreshUrl)};
  return xp_store_mutate(&ops, XP_MUTATION_TOKENPUT, args) == XP_MUTATIONRESULT_UNCHANGED || saveToFile();
}

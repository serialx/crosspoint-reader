#include "PluginCatalogActivity.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JsonListParser.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <XmlListParser.h>
#include <strings.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <type_traits>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PluginCatalogTemplate.h"
#include "activities/reader/DefinitionPayload.h"
#include "activities/settings/PluginHubInstallActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/DetailsActivity.h"
#include "components/UITheme.h"
#include "network/HttpDownloader.h"
#include "network/ProtectedPaths.h"
#include "util/BookCacheUtils.h"
#include "util/FirmwareIdentity.h"
#include "util/PluginEvents.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"
#include "util/QrUtils.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace fui = freeink::ui;

// Template/JSON/transport primitives shared with the plugin event drain.
using pluginhttp::resolvePath;
using pluginhttp::variantToString;

namespace {
constexpr size_t MAX_MANIFEST_SIZE = 8 * 1024;
// In-DRAM responses (auth, download-url hops) are small; the cap bounds a
// misbehaving server, not normal use.
constexpr size_t MAX_API_RESPONSE = 48 * 1024;
// Browse responses stream to this SD temp file instead of DRAM: one page of
// raw catalog JSON can run 60+ KB (BookFusion inlines heavy per-book
// metadata), and buffering that in a std::string aborts on low heap. Keep the
// response until leaving the catalog so downloads can release the parsed rows.
constexpr char BROWSE_TMP_PATH[] = "/.pcat_tmp.json";
constexpr size_t MAX_BROWSE_RESPONSE = 1024 * 1024;
constexpr size_t MAX_BROWSE_LIST_INDEX_RESPONSE = 16 * 1024;
constexpr size_t MAX_BROWSE_LISTS = 64;
constexpr size_t MAX_ITEM_DETAILS_RESPONSE = 24 * 1024;
constexpr int MAX_PAGE_SIZE = 16;

// JsonListParser has six generic field slots. The compatibility and details
// passes store other fields in the slots they do not need.
constexpr auto COMPAT_FAMILY = JsonListParser::F_AUTHOR;
constexpr auto COMPAT_API_MIN = JsonListParser::F_ID;
constexpr auto COMPAT_REQUIRES = JsonListParser::F_URL;
constexpr auto DETAIL_DESCRIPTION = JsonListParser::F_URL;
constexpr auto DETAIL_REPOSITORY = JsonListParser::F_BASE;
constexpr auto DETAIL_PAGE_URL = JsonListParser::F_URL;
constexpr auto DETAIL_PAGE_FORMAT = JsonListParser::F_BASE;

std::string md5Hex(const std::string& text) {
  MD5Builder md5;
  md5.begin();
  md5.add(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  md5.calculate();
  return md5.toString().c_str();
}

// Returns `override` unless it's empty, in which case `fallback` applies.
const std::string& pick(const std::string& override, const std::string& fallback) {
  return override.empty() ? fallback : override;
}

std::string_view trimCsvToken(std::string_view token) {
  while (!token.empty() && (token.front() == ' ' || token.front() == '	')) token.remove_prefix(1);
  while (!token.empty() && (token.back() == ' ' || token.back() == '	')) token.remove_suffix(1);
  return token;
}

// Calls fn(token) for each trimmed comma-separated token; stops early when fn returns false.
template <typename Fn>
bool allCsvTokens(std::string_view csv, Fn fn) {
  size_t start = 0;
  while (true) {
    size_t end = csv.find(',', start);
    if (end == std::string_view::npos) end = csv.size();
    if (!fn(trimCsvToken(csv.substr(start, end - start)))) return false;
    if (end == csv.size()) return true;
    start = end + 1;
  }
}

bool csvContainsToken(std::string_view csv, const std::string_view wanted) {
  return !allCsvTokens(csv, [wanted](const std::string_view token) { return token != wanted; });
}

bool matchesCsvRestriction(const std::string& restriction, const std::string_view current) {
  return restriction.empty() || csvContainsToken(restriction, current);
}

bool matchesPluginApiMinimum(const std::string& minimum) {
  if (minimum.empty()) return true;
  char* end = nullptr;
  const long parsed = strtol(minimum.c_str(), &end, 10);
  return end != minimum.c_str() && *end == '\0' && parsed >= 0 && parsed <= firmwareidentity::PLUGIN_API_VERSION;
}

bool matchesCapabilities(const std::string& required) {
  if (required.empty()) return true;
  return allCsvTokens(required, [](const std::string_view capability) {
    return !capability.empty() && csvContainsToken(firmwareidentity::PLUGIN_CAPABILITIES, capability);
  });
}

bool matchesListRequirements(JsonVariantConst requirements) {
  if (requirements.isUnbound()) return true;
  const auto required = requirements.as<JsonArrayConst>();
  if (required.isNull() || required.size() > 32) return false;
  for (JsonVariantConst value : required) {
    const char* capability = value.as<const char*>();
    if (!capability || !*capability || !csvContainsToken(firmwareidentity::PLUGIN_CAPABILITIES, capability)) {
      return false;
    }
  }
  return true;
}

bool catalogEntryCompatible(const std::string& family, const std::string& apiMin,
                            const std::string& requiredCapabilities) {
  return matchesCsvRestriction(family, firmwareidentity::FAMILY) && matchesPluginApiMinimum(apiMin) &&
         matchesCapabilities(requiredCapabilities);
}

// Plugin folder name from its "<root>/<name>/device.json" manifest path.
std::string pluginNameFromManifestPath(const std::string& manifestPath) {
  const size_t slash = manifestPath.rfind('/');
  if (slash == std::string::npos || slash == 0) return "";
  const size_t parent = manifestPath.rfind('/', slash - 1);
  if (parent == std::string::npos) return "";
  return manifestPath.substr(parent + 1, slash - parent - 1);
}

// book.downloaded plugin event, fired after a catalog download lands on SD.
void emitBookDownloaded(const std::string& manifestPath, const std::string& path, const std::string& title) {
  if (!pluginevents::anySubscriber(pluginevents::Event::BookDownloaded)) return;
  const std::string plugin = pluginNameFromManifestPath(manifestPath);
  const pluginevents::Var vars[] = {{"path", path.c_str()}, {"title", title.c_str()}, {"plugin", plugin.c_str()}};
  pluginevents::emit(pluginevents::Event::BookDownloaded, vars, 3);
}

// Streams the browse temp file through a catalog list parser (null on OOM).
// False when the reader cannot start or the document is malformed.
template <typename Parser>
bool streamBrowseFile(Parser* parser) {
  HalFile file;
  if (!parser || !Storage.openFileForRead("PCAT", BROWSE_TMP_PATH, file)) {
    LOG_ERR("PCAT", "Catalog list reader unavailable");
    return false;
  }
  if (parser->parse([](void* f, char* buf, size_t len) { return static_cast<HalFile*>(f)->read(buf, len); }, &file)) {
    return true;
  }
  LOG_ERR("PCAT", "Catalog list parse error");
  return false;
}

}  // namespace

namespace {
// Reads picker metadata, and classifies device manifests without making an
// events-only plugin look like an invalid catalog.
void readPluginMetadata(const std::string& path, PluginRef& ref, const bool classifyDevice = false) {
  std::string raw;
  if (!Storage.readFileToString("PCAT", path, MAX_MANIFEST_SIZE, raw)) return;
  JsonDocument filter;
  filter["title"] = true;
  filter["description"] = true;
  if (classifyDevice) {
    filter["browse"]["url"] = true;
    filter["events"] = true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return;
  if (doc["title"].is<const char*>()) ref.title = doc["title"].as<const char*>();
  if (doc["description"].is<const char*>()) ref.description = doc["description"].as<const char*>();
  if (classifyDevice) {
    const char* browseUrl = doc["browse"]["url"] | "";
    const bool hasEvents = !doc["events"].as<JsonObjectConst>().isNull() && doc["events"].size() > 0;
    ref.deviceKind = PluginLocations::classifyDeviceManifest(browseUrl[0] != '\0', hasEvents);
  }
}
}  // namespace

std::vector<PluginRef> discoverPlugins() {
  const auto entries = PluginLocations::scanPlugins();
  std::vector<PluginRef> plugins;
  plugins.reserve(entries.size());
  for (const auto& e : entries) {
    PluginRef ref;
    ref.name = e.name;
    ref.dir = e.dir;
    ref.title = e.name;
    // Browser-only plugins (no device.json) stay listed so an install is
    // visibly installed, but carry no manifest to open (empty manifestPath).
    if (e.hasDevice) ref.manifestPath = e.dir + "/device.json";
    const std::string readmePath = e.dir + "/README.md";
    if (Storage.exists(readmePath.c_str())) ref.readmePath = readmePath;
    // manifest.json first, then device.json overrides (on-device authority).
    if (e.hasManifest) readPluginMetadata(e.dir + "/manifest.json", ref);
    if (e.hasDevice) readPluginMetadata(ref.manifestPath, ref, true);
    plugins.push_back(std::move(ref));
  }
  std::sort(plugins.begin(), plugins.end(), [](const PluginRef& lhs, const PluginRef& rhs) {
    const bool lhsHub = lhs.name == "pluginhub";
    const bool rhsHub = rhs.name == "pluginhub";
    if (lhsHub != rhsHub) return lhsHub;
    const int titleOrder = strcasecmp(lhs.title.c_str(), rhs.title.c_str());
    return titleOrder != 0 ? titleOrder < 0 : strcasecmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
  });
  return plugins;
}

bool PluginCatalogActivity::loadManifest() {
  std::string raw;
  if (!Storage.readFileToString("PCAT", manifestPath, MAX_MANIFEST_SIZE, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;

  const std::string pluginDir = manifestPath.substr(0, manifestPath.rfind('/'));
  manifest.tokenFile = pluginhttp::inPluginDir(pluginDir, doc["token"]["file"] | "");
  manifest.tokenPath = doc["token"]["path"] | "token";
  manifest.configFile = pluginhttp::inPluginDir(pluginDir, doc["config"]["file"] | "");

  JsonVariantConst browse = doc["browse"];
  manifest.browseFormat = browse["format"] | "json";
  pluginhttp::readRequest(browse, "GET", manifest.browseReq);
  manifest.itemsPath = browse["items"] | "";
  manifest.titlePath = browse["fields"]["title"] | (manifest.isXmlList() ? "" : "title");
  manifest.authorPath = browse["fields"]["author"] | "";
  manifest.idPath = browse["fields"]["id"] | "";
  manifest.urlPath = browse["fields"]["url"] | "";
  manifest.versionPath = browse["fields"]["version"] | "";
  manifest.descriptionPath = browse["fields"]["description"] | "";
  manifest.detailAuthorPath = browse["fields"]["detail_author"] | "";
  manifest.repositoryPath = browse["fields"]["repository"] | "";
  if (manifest.detailAuthorPath.empty()) manifest.detailAuthorPath = manifest.authorPath;
  manifest.detailsUrlPath = browse["fields"]["details_url"] | "";
  manifest.detailsFormatPath = browse["fields"]["details_format"] | "";
  // Version-tracked catalogs predate item details. Conventional field names
  // keep those manifests working while newer catalogs can map the fields explicitly.
  if (manifest.tracksInstalls()) {
    if (manifest.descriptionPath.empty()) manifest.descriptionPath = "description";
    if (manifest.repositoryPath.empty()) manifest.repositoryPath = "repository";
    if (manifest.detailsUrlPath.empty()) manifest.detailsUrlPath = "details.url";
    if (manifest.detailsFormatPath.empty()) manifest.detailsFormatPath = "details.format";
    if (manifest.detailAuthorPath.empty() || manifest.detailAuthorPath == manifest.descriptionPath) {
      manifest.detailAuthorPath = "author";
    }
  }
  JsonVariantConst compatibility = browse["compatibility"];
  manifest.compatFamilyPath = compatibility["firmware_family"] | "";
  manifest.compatApiMinPath = compatibility["plugin_api_min"] | "";
  manifest.compatRequiresPath = compatibility["requires"] | "";
  manifest.pageSize = browse["page_size"] | 8;
  // Documented bounds: each row costs an Item (strings) and a screen slot.
  manifest.pageSize = std::clamp(manifest.pageSize, 1, MAX_PAGE_SIZE);
  manifest.browseListsUrl = browse["lists_url"] | "";
  pluginhttp::readHeaders(browse["lists_headers"], manifest.listsHeaders);
  manifest.listsItemsPath = browse["lists_items"] | "lists";
  JsonVariantConst listFields = browse["lists_fields"];
  manifest.listsTitlePath = listFields["title"] | "title";
  manifest.listsUrlPath = listFields["url"] | "url";
  manifest.listsIdPath = listFields["id"] | "id";
  manifest.listsRequiresPath = listFields["requires"] | "requires";
  manifest.browseLists.reserve(browse["lists"].size());
  for (JsonVariantConst l : browse["lists"].as<JsonArrayConst>()) {
    if (!matchesListRequirements(l["requires"])) continue;
    Manifest::BrowseList entry;
    entry.title = l["title"] | "";
    entry.url = l["url"] | "";
    entry.id = variantToString(l["id"]);
    entry.body = l["body"] | "";
    JsonVariantConst notice = l["notice"];
    entry.noticeTitle = notice["title"] | "";
    entry.noticeMessage = notice["message"] | "";
    entry.noticeConfirm = notice["confirm"] | "";
    entry.noticeCancel = notice["cancel"] | "";
    if (!entry.title.empty()) manifest.browseLists.push_back(std::move(entry));
  }
  manifest.fixedListCount = manifest.browseLists.size();
  manifest.searchUrl = browse["search"]["url"] | "";
  manifest.searchBody = browse["search"]["body"] | "";
  manifest.xmlItem = browse["item"] | "";
  manifest.xmlContainer = browse["container_element"] | "";
  manifest.xmlSkipSelf = browse["skip_self"] | false;
  manifest.xmlResolveUrls = browse["resolve_urls"] | false;
  manifest.xmlExtensions.reserve(browse["extensions"].size());
  for (JsonVariantConst ext : browse["extensions"].as<JsonArrayConst>()) {
    if (ext.is<const char*>()) manifest.xmlExtensions.emplace_back(ext.as<const char*>());
  }

  JsonVariantConst dl = doc["download"];
  pluginhttp::readRequest(dl, "GET", manifest.downloadReq);
  manifest.dlUrlPath = dl["url_path"] | "";
  manifest.dlUser = dl["username"] | "";
  manifest.dlPass = dl["password"] | "";
  manifest.destDir = pluginhttp::inPluginDir(pluginDir, dl["dest_dir"] | "");
  if (!manifest.destDir.empty() && !protectedpaths::isPluginPath(manifest.destDir)) {
    LOG_ERR("PCAT", "dest_dir outside plugin space: %s", manifest.destDir.c_str());
    return false;
  }
  manifest.filenameTpl = dl["filename"] | "{title}.epub";
  // Multi-file bundle install (generic): base URL + a files array per item.
  manifest.bundleBasePath = dl["bundle"]["base"] | "";
  manifest.bundleFilesPath = dl["bundle"]["files"] | "";
  manifest.bundleSubdir = dl["bundle"]["subdir"] | "{id}";
  // XML-list items already carry the file URL; default the template to it.
  if (manifest.isXmlList() && manifest.downloadReq.url.empty()) manifest.downloadReq.url = "{url}";
  manifest.sidecarPath = dl["sidecar"]["path"] | "";
  manifest.sidecarBody = dl["sidecar"]["body"] | "";

  JsonVariantConst auth = doc["auth"];
  manifest.authType = auth["type"] | "device_code";
  pluginhttp::readRequest(auth["request"], "POST", manifest.authReq);
  pluginhttp::readRequest(auth["poll"], "POST", manifest.pollReq);
  manifest.authCodePath = auth["code_path"] | "user_code";
  manifest.authVerifyPath = auth["verify_url_path"] | "verification_uri";
  manifest.authDeviceCodePath = auth["device_code_path"] | "device_code";
  manifest.authIntervalPath = auth["interval_path"] | "interval";
  manifest.authExpiresPath = auth["expires_path"] | "expires_in";
  manifest.authTokenPath = auth["token_path"] | "access_token";
  manifest.authErrorPath = auth["error_path"] | "error";

  return !manifest.browseReq.url.empty();
}

bool PluginCatalogActivity::loadBrowseListIndex(int& status) {
  status = 200;
  if (manifest.browseListsUrl.empty()) return true;

  std::string url, origin;
  JsonDocument doc;
  const auto fetch = [&] {
    String response;
    pluginhttp::RequestSpec req;
    origin.clear();
    if (!buildRequest(req, manifest.browseListsUrl, "", manifest.listsHeaders, nullptr, {}, false, false, nullptr,
                      &origin)) {
      status = -1;
      return false;
    }
    url = std::move(req.url);
    status = pluginhttp::request(session.get(), url, "GET", "", req.headers, response, MAX_BROWSE_LIST_INDEX_RESPONSE);
    if (status < 200 || status >= 300) {
      LOG_ERR("PCAT", "browse list index fetch failed: status=%d %s", status, url.c_str());
      return false;
    }
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
      LOG_ERR("PCAT", "browse list index parse failed: %s", url.c_str());
      return false;
    }
    return true;
  };
  if (!fetch() &&
      !((status == 401 || status == 403) && manifest.hasPasswordGrant() && refreshCredentialToken() && fetch())) {
    return false;
  }

  const auto lists = resolvePath(doc.as<JsonVariantConst>(), manifest.listsItemsPath).as<JsonArrayConst>();
  if (lists.isNull() || lists.size() == 0) {
    LOG_ERR("PCAT", "browse list index has no lists: %s", url.c_str());
    return false;
  }

  ResourceLock resources(*this);
  manifest.listOrigin = std::move(origin);
  manifest.browseLists.resize(manifest.fixedListCount);
  manifest.browseLists.reserve(manifest.fixedListCount + std::min(lists.size(), MAX_BROWSE_LISTS));
  for (JsonVariantConst l : lists) {
    if (manifest.browseLists.size() - manifest.fixedListCount >= MAX_BROWSE_LISTS) {
      LOG_ERR("PCAT", "browse list index truncated to %u lists", static_cast<unsigned>(MAX_BROWSE_LISTS));
      break;
    }
    if (!matchesListRequirements(resolvePath(l, manifest.listsRequiresPath))) continue;
    Manifest::BrowseList entry;
    entry.title = variantToString(resolvePath(l, manifest.listsTitlePath));
    entry.url = variantToString(resolvePath(l, manifest.listsUrlPath));
    entry.id = variantToString(resolvePath(l, manifest.listsIdPath));
    entry.body = l["body"] | "";
    JsonVariantConst notice = l["notice"];
    entry.noticeTitle = notice["title"] | "";
    entry.noticeMessage = notice["message"] | "";
    entry.noticeConfirm = notice["confirm"] | "";
    entry.noticeCancel = notice["cancel"] | "";
    if (!entry.title.empty() && (!entry.url.empty() || !entry.id.empty()))
      manifest.browseLists.push_back(std::move(entry));
  }
  if (manifest.browseLists.size() == manifest.fixedListCount) {
    LOG_ERR("PCAT", "browse list index has no valid lists: %s", url.c_str());
    return false;
  }
  return true;
}

bool PluginCatalogActivity::saveToken(const std::string& value) {
  return pluginhttp::saveTokenToFile(manifest.tokenFile, manifest.tokenPath, value);
}

bool PluginCatalogActivity::loadToken() {
  return pluginhttp::loadTokenFromFile(manifest.tokenFile, manifest.tokenPath, token);
}

void PluginCatalogActivity::loadConfig() { pluginhttp::loadConfigFile(manifest.configFile, config); }

struct PluginCatalogActivity::Variables : plugincatalog::Template {
  char api[12], page[12], limit[12], offset[16];
  Variables(const PluginCatalogActivity& catalog, const Item* item, size_t extra)
      : Template(catalog.config.size() + (item ? 20 : 12) + extra) {
    if (!allocated()) return;
    snprintf(api, sizeof(api), "%d", firmwareidentity::PLUGIN_API_VERSION);
    snprintf(page, sizeof(page), "%d", static_cast<int>(catalog.ui.page));
    snprintf(limit, sizeof(limit), "%d",
             plugincatalogpaging::limitFor(catalog.browsePaging(), catalog.manifest.pageSize));
    snprintf(offset, sizeof(offset), "%llu",
             static_cast<unsigned long long>(std::max<int32_t>(1, catalog.ui.page) - 1) * catalog.manifest.pageSize);
    const bool inList =
        catalog.ui.current_list >= 0 && catalog.ui.current_list < static_cast<int>(catalog.manifest.browseLists.size());
    const std::string_view listId =
        inList ? std::string_view(catalog.manifest.browseLists[catalog.ui.current_list].id) : std::string_view{};
    add("{token}", catalog.token);
    for (const auto& kv : catalog.config) add(kv.first, kv.second, false, true);
    add("{firmware_family}", firmwareidentity::FAMILY);
    add("{firmware_version}", CROSSPOINT_VERSION);
    add("{plugin_capabilities}", firmwareidentity::PLUGIN_CAPABILITIES);
    add("{plugin_api}", api);
    add("{page}", page);
    add("{limit}", limit);
    add("{offset}", offset);
    add("{list_id}", listId);
    add("{list_id_enc}", listId, true);
    add("{query}", catalog.searchQuery, true);
    add("{query_raw}", catalog.searchQuery);
    if (item) {
      add("{id}", item->id);
      add("{title}", item->title);
      add("{author}", item->author);
      add("{url}", item->url);
      add("{id_enc}", item->id, true);
      add("{title_enc}", item->title, true);
      add("{author_enc}", item->author, true);
      add("{url_enc}", item->url, true);
    }
  }
};

bool PluginCatalogActivity::buildRequest(pluginhttp::RequestSpec& out, const std::string& url, const std::string& body,
                                         const pluginhttp::Headers& headers, const Item* item, std::string_view prefix,
                                         bool poll, bool literalUrl, DownloadCredentials* credentials,
                                         std::string* origin) const {
  Variables vars(*this, item, poll);
  if (!vars.allocated()) return false;
  // Poll substitutes device_code only in the URL and body, after other variables.
  auto sizes = makeUniqueNoThrow<size_t[]>(headers.size() + 2);
  if (!sizes) return false;
  plugincatalog::Template literal(0);
  plugincatalog::Origin rawOrigin;
  size_t remaining = plugincatalog::MAX_REQUEST_BYTES;
  const auto field = [&](size_t index, std::string_view lead, const std::string& text, bool escape, std::string* dest) {
    auto& resolver = escape && literalUrl ? literal : vars;
    size_t before = remaining;
    if (dest) dest->reserve(sizes[index]);
    if (!resolver.expand(lead, text, escape, remaining, dest, escape && origin ? &rawOrigin : nullptr, origin))
      return false;
    sizes[index] = before - remaining;
    return true;
  };
  for (bool write : {false, true}) {
    remaining = plugincatalog::MAX_REQUEST_BYTES;
    if (credentials) {
      size_t user = plugincatalog::MAX_REQUEST_BYTES, password = plugincatalog::MAX_REQUEST_BYTES;
      // An incomplete Basic pair is omitted by the HTTP transfer owner.
      if (!vars.expand({}, manifest.dlUser, false, user) || !vars.expand({}, manifest.dlPass, false, password))
        return false;
      user = plugincatalog::MAX_REQUEST_BYTES - user;
      password = plugincatalog::MAX_REQUEST_BYTES - password;
      if (user && password) {
        if (user > remaining || password > remaining) return false;
        const size_t auth = sizeof("AuthorizationBasic ") - 1 + 4 * ((user + password + 3) / 3);
        if (auth > remaining) return false;
        remaining -= auth;
        if (write) {
          credentials->user.reserve(user);
          credentials->password.reserve(password);
          vars.expand({}, manifest.dlUser, false, user, &credentials->user);
          vars.expand({}, manifest.dlPass, false, password, &credentials->password);
        }
      }
    }
    if (write && origin) origin->reserve(rawOrigin.length());
    if (write) out.headers.reserve(headers.size());
    for (size_t i = 0; i < headers.size(); ++i) {
      const auto& h = headers[i];
      if (h.first.size() > remaining) return false;
      remaining -= h.first.size();
      if (write) out.headers.emplace_back(h.first, "");
      if (!field(i + 2, {}, h.second, false, write ? &out.headers.back().second : nullptr)) return false;
    }
    if (poll) vars.add("{device_code}", authDeviceCode);
    if (!field(0, prefix, url, true, write ? &out.url : nullptr) ||
        !field(1, {}, body, false, write ? &out.body : nullptr))
      return false;
    if (poll) vars.removeLast();
  }
  return true;
}

bool PluginCatalogActivity::substituted(const std::string& tpl, std::string& out, const Item* item,
                                        std::string_view md5, std::string_view dest) const {
  Variables vars(*this, item, md5.empty() ? 0 : 2);
  if (!vars.allocated()) return false;
  if (!md5.empty()) {
    vars.add("{md5}", md5);
    vars.add("{dest}", dest);
  }
  size_t remaining = plugincatalog::MAX_REQUEST_BYTES;
  if (!vars.expand({}, tpl, false, remaining)) return false;
  size_t size = plugincatalog::MAX_REQUEST_BYTES - remaining;
  out.clear();
  out.reserve(size);
  return vars.expand({}, tpl, false, size, &out);
}

std::string PluginCatalogActivity::downloadDir() const {
  for (const auto& kv : config) {
    if (kv.first != "dest_dir" || kv.second.empty()) continue;
    std::string dir = kv.second.front() == '/' ? kv.second : "/" + kv.second;
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    if (dir == "/") return "";  // SD root
    if (protectedpaths::isPluginPath(dir)) return dir;
    LOG_ERR("PCAT", "config dest_dir rejected: %s", kv.second.c_str());
  }
  return manifest.destDir;
}

PluginCatalogActivity::PluginCatalogActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const bool showOpds, const bool rootMode)
    : CatalogActivity("PluginCatalog", renderer, mappedInput, XP_UI_CATALOG_PLUGINS),
      showOpds(showOpds),
      rootMode(rootMode) {}

PluginCatalogActivity::~PluginCatalogActivity() = default;

int PluginCatalogActivity::apiRequest(const pluginhttp::RequestSpec& req, String& out) {
  return pluginhttp::request(session.get(), req.url, req.method, req.body, req.headers, out, MAX_API_RESPONSE);
}

void PluginCatalogActivity::onEnter() {
  CatalogActivity::onEnter();
  if (!hasUiStorage()) return;
#ifdef CROSSPOINT_UI_ORACLE
  if (uiOraclePluginEnter(*this)) return;
#endif
  enterPluginPicker();
}

void PluginCatalogActivity::render(RenderLock&& lock) {
  CatalogActivity::render(std::move(lock));
  // The formatted title is borrowed only while painting, not between frames.
  std::string().swap(renderedTitle);
}

void PluginCatalogActivity::enterPluginPicker() {
  ResourceLock resources(*this);
  Storage.remove(BROWSE_TMP_PATH);
  installedPlugins = discoverPlugins();
  pluginHubAvailable = PluginHubInstallActivity::isAvailable();
  showPluginHubInstallRow = !pluginHubAvailable && !SETTINGS.pluginHubPromptHidden;
  // Discovery just re-read the plugin folders; keep the event subscription
  // table in step so a plugin installed since boot starts receiving events
  // (and a removed one stops) without a restart.
  pluginevents::refreshSubscriptions();
  // Disclose which device events each plugin receives (and so what reading
  // activity it can send off the device). Events precede the optional summary.
  static constexpr StrId EVENT_LABELS[] = {StrId::STR_EVENT_BOOK_OPEN, StrId::STR_EVENT_BOOK_CLOSE,
                                           StrId::STR_EVENT_READING_SESSION, StrId::STR_EVENT_DOWNLOAD,
                                           StrId::STR_EVENT_SLEEP};
  static_assert(sizeof(EVENT_LABELS) / sizeof(EVENT_LABELS[0]) == static_cast<size_t>(pluginevents::Event::COUNT));
  for (auto& plugin : installedPlugins) {
    const uint8_t mask = pluginevents::subscriptionMask(plugin.name.c_str());
    if (mask == 0) continue;
    std::string events;
    for (size_t i = 0; i < std::size(EVENT_LABELS); i++) {
      if (!(mask & (1u << i))) continue;
      if (!events.empty()) events += ", ";
      events += I18N.get(EVENT_LABELS[i]);
    }
    char line[192];
    snprintf(line, sizeof(line), tr(STR_PLUGIN_RECEIVES_EVENTS), events.c_str());
    plugin.description = plugin.description.empty() ? line : std::string(line) + ". " + plugin.description;
  }

  manifestPath.clear();
  manifest = Manifest{};
  catalogTitle = tr(STR_PLUGINS);
  token.clear();
  config.clear();
  resetBrowse();
  browseHistory.clear();
  browseCurrentUrl.clear();
  errorMessage.clear();
  session.reset();  // no TLS while only picking
  setState(State::PLUGIN_PICKER);
  for (size_t i = 0; i < installedPlugins.size(); ++i) {
    if (installedPlugins[i].name != pickerReturnName) continue;
    const int prefixRows = (showOpds ? 1 : 0) + (showPluginHubInstallRow ? 1 : 0);
    selectIndex(prefixRows + static_cast<int>(i));
    break;
  }
  requestUpdate();
}

void PluginCatalogActivity::confirmUninstallPlugin(const int pickerIndex) {
  if (getState() != State::PLUGIN_PICKER) return;
  const int offset = (showOpds ? 1 : 0) + (showPluginHubInstallRow ? 1 : 0);
  const int pluginIndex = pickerIndex - offset;
  if (pluginIndex < 0 || pluginIndex >= static_cast<int>(installedPlugins.size())) return;

  const PluginRef plugin = installedPlugins[pluginIndex];
  if (plugin.name.empty() || plugin.name == "." || plugin.name == ".." ||
      plugin.name.find_first_of("/\\") != std::string::npos) {
    LOG_ERR("PCAT", "Refusing unsafe plugin name: %s", plugin.name.c_str());
    return;
  }
  bool safeDir = false;
  for (const char* root : PluginLocations::ROOTS) {
    if (plugin.dir == std::string(root) + "/" + plugin.name) {
      safeDir = true;
      break;
    }
  }
  if (!safeDir) {
    LOG_ERR("PCAT", "Refusing unsafe plugin uninstall path: %s", plugin.dir.c_str());
    return;
  }

  char message[256];
  snprintf(message, sizeof(message), tr(STR_PLUGIN_UNINSTALL_CONFIRM), plugin.title.c_str());
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_PLUGIN_UNINSTALL_TITLE),
                                                              message, tr(STR_CANCEL), tr(STR_PLUGIN_UNINSTALL));
  if (!confirmation) {
    LOG_ERR("PCAT", "OOM: plugin uninstall confirmation");
    return;
  }

  std::string returnPluginName;
  if (pluginIndex + 1 < static_cast<int>(installedPlugins.size()))
    returnPluginName = installedPlugins[pluginIndex + 1].name;
  else if (pluginIndex > 0)
    returnPluginName = installedPlugins[pluginIndex - 1].name;
  startActivityForResult(std::move(confirmation), [this, plugin, returnPluginName](const ActivityResult& result) {
    if (result.isCancelled) return;
    const bool removed = !Storage.exists(plugin.dir.c_str()) || Storage.removeDir(plugin.dir.c_str());
    if (!removed) LOG_ERR("PCAT", "Failed to recursively uninstall plugin: %s", plugin.dir.c_str());
    if (removed) pickerReturnName = returnPluginName;
    enterPluginPicker();
  });
}

void PluginCatalogActivity::enterCatalog() {
  {
    ResourceLock resources(*this);
    setState(State::CHECK_WIFI);
    resetBrowse();
    errorMessage.clear();
    statusMessage = tr(STR_CHECKING_WIFI);
    session.reset(new (std::nothrow) freeink::SecureHttpClient());
    if (session) session->setReuse(true);

    listIndexLoaded = false;
    if (!loadManifest()) {
      fail(StrId::STR_PLUGIN_MANIFEST_INVALID);
      return;
    }
  }
  const uint8_t flags =
      (manifest.isXmlList() ? XP_UI_CATALOG_XML : 0) | (manifest.hasDeviceCode() ? XP_UI_CATALOG_DEVICE_CODE : 0);
  {
    ModelLock lock(*this);
    ui.plugin_flags = flags;
  }
  requestUpdate();
  checkAndConnectWifi();
}

// Leaving the open catalog (Back at its root, or Back out of an error /
// sign-in screen): return to the plugin picker. Wi-Fi stays up so the next
// pick connects instantly; onExit tears it down when the activity ends.
void PluginCatalogActivity::exitCatalog() { enterPluginPicker(); }

void PluginCatalogActivity::resetBrowse() {
  ResourceLock resources(*this);
  items.clear();
  {
    ModelLock lock(*this);
    ui.page = 1;
    ui.plugin_flags &= ~XP_UI_CATALOG_MORE;
    ui.current_list = -1;
  }
  searchActive = false;
  searchQuery.clear();
  releaseRows();
  resetSelection();
}

// Shows the list's notice first when it has one. Declining stays on the
// picker, or leaves the catalog when this list is the only one.
void PluginCatalogActivity::openList(const int index) {
  const auto& list = manifest.browseLists[index];
  if (!list.hasNotice()) {
    {
      ModelLock lock(*this);
      ui.current_list = index;
    }
    startBrowse();
    return;
  }
  std::string heading, message, cancel, confirm;
  if (!substituted(list.noticeTitle.empty() ? list.title : list.noticeTitle, heading) ||
      !substituted(list.noticeMessage, message) || !substituted(list.noticeCancel, cancel) ||
      !substituted(list.noticeConfirm, confirm)) {
    fail(StrId::STR_FETCH_FEED_FAILED);
    return;
  }
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, message, cancel, confirm);
  if (!confirmation) {
    LOG_ERR("PCAT", "OOM: browse-list notice");
    if (manifest.browseLists.size() == 1) exitCatalog();
    return;
  }
  startActivityForResult(std::move(confirmation), [this, index](const ActivityResult& result) {
    if (result.isCancelled) {
      if (manifest.browseLists.size() == 1) exitCatalog();
      return;
    }
    {
      ModelLock lock(*this);
      ui.current_list = index;
    }
    startBrowse();
  });
}

bool PluginCatalogActivity::wantsListPicker() const {
  return !manifest.browseLists.empty() && !manifest.isXmlList() && ui.current_list < 0;
}

void PluginCatalogActivity::onExit() {
  items.clear();
  session.reset();  // drop browse TLS before Wi-Fi teardown
  Storage.remove(BROWSE_TMP_PATH);
  CatalogActivity::onExit();
}

void PluginCatalogActivity::startBrowse() {
  const bool needsListIndex = !manifest.browseListsUrl.empty() && !listIndexLoaded;
  const bool needsListAuth = needsListIndex || wantsListPicker();
  if (needsListAuth) {
    // Dynamic list indexes and list pickers may both use {token}/{cfg.KEY}, so
    // resolve credentials before either path. Token-less catalogs skip the gate.
    loadConfig();
    if (!loadToken() && !(manifest.hasPasswordGrant() && refreshCredentialToken())) {
      if (manifest.hasDeviceCode()) {
        beginAuth();  // straight to the QR/code sign-in, no interstitial
      } else {
        setState(State::NO_TOKEN);
        requestUpdate();
      }
      return;
    }
  }
  if (needsListIndex) {
    int status;
    if (!loadBrowseListIndex(status)) {
      if (status == 401 || status == 403) {
        setState(State::NO_TOKEN);
        requestUpdate();
        return;
      }
      if (manifest.fixedListCount == 0) {
        fail(StrId::STR_FETCH_FEED_FAILED);
        return;
      }
    }
    listIndexLoaded = true;
  }
  // Browse lists apply to JSON catalogs; XML lists navigate by folder instead.
  if (wantsListPicker()) {
    if (manifest.browseLists.size() == 1) {
      openList(0);  // a one-entry picker is just an extra click
      return;
    }
    {
      ResourceLock resources(*this);
      items.clear();
      {
        ModelLock lock(*this);
        ui.page = 1;
        ui.plugin_flags &= ~XP_UI_CATALOG_MORE;
      }
      releaseRows();
      resetSelection();
      setState(State::LIST_PICKER);
    }
    requestUpdate();
    return;
  }
  beginLoading();
  fetchPage(1);
}

void PluginCatalogActivity::performSearch(const std::string& query) {
  if (query.empty()) {
    requestUpdate();
    return;
  }
  {
    ResourceLock resources(*this);
    searchQuery = query;
    searchActive = true;
    {
      ModelLock lock(*this);
      ui.current_list = -1;
    }  // Search spans every list.
    releaseRows();
    resetSelection();
  }
  beginLoading();
  fetchPage(1);
}

bool PluginCatalogActivity::refreshCredentialToken() {
  loadConfig();
  std::string minted;
  pluginhttp::RequestSpec req;
  if (!buildRequest(req, manifest.authReq.url, manifest.authReq.body, manifest.authReq.headers) ||
      !pluginhttp::mintPasswordToken(session.get(), req.url, manifest.authReq.method, req.body, req.headers,
                                     manifest.authTokenPath, minted)) {
    return false;
  }
  if (!saveToken(minted)) return false;
  token = minted;  // usable immediately, without re-reading the file
  return true;
}

bool PluginCatalogActivity::fetchBrowseResponse() {
  loadConfig();
  if (!loadToken() && !(manifest.hasPasswordGrant() && refreshCredentialToken())) {
    setState(State::NO_TOKEN);
    requestUpdate();
    return false;
  }
  if (manifest.isXmlList()) {
    if (manifest.xmlItem.empty()) {
      fail(StrId::STR_PLUGIN_MANIFEST_INVALID);
      return false;
    }
    if (browseCurrentUrl.empty() && !substituted(manifest.browseReq.url, browseCurrentUrl)) {
      fail(StrId::STR_FETCH_FEED_FAILED);
      return false;
    }
  }
  const auto run = [&] {
    pluginhttp::RequestSpec req;
    if (!buildRequest(req, manifest.isXmlList() ? browseCurrentUrl : activeBrowseUrl(),
                      manifest.isXmlList() ? manifest.browseReq.body : activeBrowseBody(), manifest.browseReq.headers,
                      nullptr, activeBrowseOrigin()))
      return -1;
    return pluginhttp::requestToFile(session.get(), req.url, manifest.browseReq.method, req.body, req.headers,
                                     BROWSE_TMP_PATH, MAX_BROWSE_RESPONSE);
  };
  int status = run();
  // Rebuild templates with the refreshed token; retry only once.
  if ((status == 401 || status == 403) && manifest.hasPasswordGrant() && refreshCredentialToken()) status = run();
  if (status >= 200 && status < 300) return true;  // includes WebDAV's 207 Multi-Status
  Storage.remove(BROWSE_TMP_PATH);
  if (status == 401 || status == 403) {
    setState(State::NO_TOKEN);
    requestUpdate();
  } else {
    fail(StrId::STR_FETCH_FEED_FAILED);
  }
  return false;
}

bool PluginCatalogActivity::parseXmlList() {
  ResourceLock resources(*this);
  releaseRows();
  items.clear();
  auto append = [&](XmlListParser::RawItem& row) {
    Item item;
    item.isDir = row.isDir;
    item.url = std::move(row.field[XmlListParser::F_URL]);
    item.title = std::move(row.field[XmlListParser::F_TITLE]);
    item.author = std::move(row.field[XmlListParser::F_AUTHOR]);
    item.id = std::move(row.field[XmlListParser::F_ID]);
    if (items.size() == items.capacity()) {
      items.reserve(std::min(XmlListParser::MAX_ITEMS, std::max<size_t>(manifest.pageSize, items.capacity() * 2)));
    }
    items.push_back(std::move(item));
  };
  const std::string* const selectors[] = {&manifest.urlPath, &manifest.titlePath, &manifest.authorPath,
                                          &manifest.idPath};
  auto parser = makeUniqueNoThrow<XmlListParser>(
      manifest.xmlItem, manifest.xmlContainer, selectors,
      [](void* context, XmlListParser::RawItem& row) { (*static_cast<decltype(append)*>(context))(row); }, &append);
  if (parser) {
    XmlListParser::UrlOptions urls;
    urls.requestUrl = browseCurrentUrl;
    urls.skipSelf = manifest.xmlSkipSelf;
    urls.resolveUrls = manifest.xmlResolveUrls;
    urls.extensions = manifest.xmlExtensions;
    parser->setUrlOptions(std::move(urls));
  }
  if (!streamBrowseFile(parser.get())) {
    fail(StrId::STR_PARSE_FEED_FAILED);
    return false;
  }

  // Folders first, then files, each alphabetical — matches how file managers list.
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    if (a.isDir != b.isDir) return a.isDir;
    return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
  });
  {
    ModelLock lock(*this);
    ui.plugin_flags &= ~XP_UI_CATALOG_MORE;
  }
  return true;
}

bool PluginCatalogActivity::prevRowVisible() const {
  return getState() == State::BROWSING && !manifest.isXmlList() && ui.page > 1;
}

bool PluginCatalogActivity::nextRowVisible() const {
  return getState() == State::BROWSING && !manifest.isXmlList() && (ui.plugin_flags & XP_UI_CATALOG_MORE);
}

int PluginCatalogActivity::rowCount() const {
  if (getState() == State::PLUGIN_PICKER)
    return static_cast<int>(installedPlugins.size()) + (showOpds ? 1 : 0) + (showPluginHubInstallRow ? 1 : 0);
  if (getState() == State::LIST_PICKER) return static_cast<int>(manifest.browseLists.size());
  if (getState() != State::BROWSING) return 0;
  return static_cast<int>(items.size()) + (prevRowVisible() ? 1 : 0) + (nextRowVisible() ? 1 : 0);
}

const std::string& PluginCatalogActivity::activeBrowseUrl() const {
  // Search overrides the list view, reusing the browse url when unspecified.
  if (searchActive) return pick(manifest.searchUrl, manifest.browseReq.url);
  if (ui.current_list < 0 || ui.current_list >= static_cast<int>(manifest.browseLists.size()))
    return manifest.browseReq.url;
  return pick(manifest.browseLists[ui.current_list].url, manifest.browseReq.url);
}

std::string_view PluginCatalogActivity::activeBrowseOrigin() const {
  if (searchActive || ui.current_list < 0 || static_cast<size_t>(ui.current_list) < manifest.fixedListCount ||
      static_cast<size_t>(ui.current_list) >= manifest.browseLists.size())
    return {};
  return UrlUtils::isServerPath(manifest.browseLists[ui.current_list].url) ? std::string_view(manifest.listOrigin)
                                                                           : std::string_view{};
}

const std::string& PluginCatalogActivity::activeBrowseBody() const {
  if (searchActive) return pick(manifest.searchBody, manifest.browseReq.body);
  if (ui.current_list < 0 || ui.current_list >= static_cast<int>(manifest.browseLists.size()))
    return manifest.browseReq.body;
  return pick(manifest.browseLists[ui.current_list].body, manifest.browseReq.body);
}

plugincatalogpaging::Paging PluginCatalogActivity::browsePaging() const {
  return plugincatalogpaging::pagingOf(activeBrowseUrl(), activeBrowseBody(), manifest.browseReq.headers,
                                       activeBrowseOrigin());
}

void PluginCatalogActivity::fetchPage(const int newPage) {
  if (!manifest.isXmlList()) {
    ModelLock lock(*this);
    ui.page = newPage;
  }
  if (!fetchBrowseResponse() || !parseBrowseResponse()) return;
  resetSelection();
  setState(State::BROWSING);
  requestUpdate();
}

bool PluginCatalogActivity::parseBrowseResponse() {
  ResourceLock resources(*this);
  if (manifest.isXmlList()) return parseXmlList();

  const auto paging = browsePaging();
  const bool localPaging = paging == plugincatalogpaging::Paging::Static;
  const size_t pageOffset =
      localPaging ? static_cast<size_t>(std::max<int32_t>(0, ui.page - 1)) * static_cast<size_t>(manifest.pageSize) : 0;

  // Static catalogs paginate *compatible* entries rather than raw entries,
  // so incompatible plugins cannot create empty or misleading pages.
  std::vector<int> compatibleRows;
  bool rawHasMore = false;
  if (manifest.filtersCompatibility()) {
    compatibleRows.reserve(manifest.pageSize);
    int rowIndex = 0;
    plugincatalogpaging::PageWindow compatibleWindow(pageOffset, manifest.pageSize);
    const std::string unused;
    auto selectCompatible = [&](JsonListParser::Row& row) {
      const int thisRow = rowIndex++;
      const bool shown =
          !row.field[JsonListParser::F_TITLE].empty() &&
          catalogEntryCompatible(row.field[COMPAT_FAMILY], row.field[COMPAT_API_MIN], row.field[COMPAT_REQUIRES]);
      if (compatibleWindow.take(plugincatalogpaging::countsTowardPage(paging, shown)) && shown) {
        compatibleRows.push_back(thisRow);
      }
    };
    const std::string* compatibilityFields[JsonListParser::F_COUNT] = {};
    for (auto& field : compatibilityFields) field = &unused;
    compatibilityFields[JsonListParser::F_TITLE] = &manifest.titlePath;
    compatibilityFields[COMPAT_FAMILY] = &manifest.compatFamilyPath;
    compatibilityFields[COMPAT_API_MIN] = &manifest.compatApiMinPath;
    compatibilityFields[COMPAT_REQUIRES] = &manifest.compatRequiresPath;
    auto compatibilityParser = makeUniqueNoThrow<JsonListParser>(
        manifest.itemsPath, compatibilityFields, unused,
        [](void* context, JsonListParser::Row& row) { (*static_cast<decltype(selectCompatible)*>(context))(row); },
        &selectCompatible);
    if (!streamBrowseFile(compatibilityParser.get())) {
      fail(StrId::STR_PARSE_FEED_FAILED);
      return false;
    }
    rawHasMore = compatibleWindow.hasMore(paging);
  }

  releaseRows();
  items.clear();
  items.reserve(manifest.pageSize + 1);
  int rowIndex = 0;
  size_t compatibleIndex = 0;
  plugincatalogpaging::PageWindow visibleWindow(pageOffset, manifest.pageSize);
  auto append = [&](JsonListParser::Row& row) {
    const int thisRow = rowIndex++;
    if (manifest.filtersCompatibility()) {
      if (compatibleIndex >= compatibleRows.size() || compatibleRows[compatibleIndex] != thisRow) return;
      compatibleIndex++;
    } else if (!visibleWindow.take(
                   plugincatalogpaging::countsTowardPage(paging, !row.field[JsonListParser::F_TITLE].empty()))) {
      return;
    }
    if (row.field[JsonListParser::F_TITLE].empty()) return;
    Item item;
    item.title = std::move(row.field[JsonListParser::F_TITLE]);
    item.author = std::move(row.field[JsonListParser::F_AUTHOR]);
    item.id = std::move(row.field[JsonListParser::F_ID]);
    item.url = std::move(row.field[JsonListParser::F_URL]);
    item.version = std::move(row.field[JsonListParser::F_VERSION]);
    item.base = std::move(row.field[JsonListParser::F_BASE]);
    item.files = std::move(row.files);
    items.push_back(std::move(item));
  };
  // Streamed straight from the SD temp file: neither the raw response nor a
  // parsed document occupies DRAM, only the rows kept.
  const std::string* const fields[] = {&manifest.titlePath, &manifest.authorPath,  &manifest.idPath,
                                       &manifest.urlPath,   &manifest.versionPath, &manifest.bundleBasePath};
  auto parser = makeUniqueNoThrow<JsonListParser>(
      manifest.itemsPath, fields, manifest.bundleFilesPath,
      [](void* context, JsonListParser::Row& row) { (*static_cast<decltype(append)*>(context))(row); }, &append);
  if (!streamBrowseFile(parser.get())) {
    fail(StrId::STR_PARSE_FEED_FAILED);
    return false;
  }
  const bool more = manifest.filtersCompatibility() ? rawHasMore : visibleWindow.hasMore(paging);
  {
    ModelLock lock(*this);
    ui.plugin_flags = (ui.plugin_flags & ~XP_UI_CATALOG_MORE) | (more ? XP_UI_CATALOG_MORE : 0);
  }
  computeInstallStatus();
  return true;
}

// Badge each item by comparing its catalog version to the installed copy's
// manifest (located by folder id across the plugin roots). Only a newer
// catalog version is an update, so an older catalog never offers a downgrade.
// Runs once per page.
void PluginCatalogActivity::computeInstallStatus() {
  ResourceLock resources(*this);
  if (!manifest.tracksInstalls()) return;
  JsonDocument filter;
  filter["version"] = true;
  for (auto& item : items) {
    item.status.clear();
    if (item.id.empty()) continue;
    // Locate the install across every plugin root (not just one), matching
    // the discovery the rest of the firmware uses.
    const std::string dir = PluginLocations::findPluginDir(item.id.c_str());
    std::string raw;
    if (dir.empty() || !Storage.readFileToString("PCAT", dir + "/manifest.json", MAX_MANIFEST_SIZE, raw)) {
      // Not installed: show the available version so the row is not blank.
      if (!item.version.empty()) item.status = "v" + item.version;
      continue;
    }
    JsonDocument doc;
    std::string installed;
    if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
      installed = doc["version"] | "";
    }
    item.status =
        PluginLocations::isNewerVersion(item.version, installed) ? tr(STR_UPDATE_AVAILABLE) : tr(STR_INSTALLED);
  }
}

void PluginCatalogActivity::beginAuth() {
  beginLoading();

  String response;
  pluginhttp::RequestSpec req;
  req.method = manifest.authReq.method;
  const int status = buildRequest(req, manifest.authReq.url, manifest.authReq.body, manifest.authReq.headers)
                         ? apiRequest(req, response)
                         : -1;
  JsonDocument doc;
  if (status < 200 || status >= 300 || deserializeJson(doc, response) != DeserializationError::Ok) {
    fail(StrId::STR_PLUGIN_AUTH_FAILED);
    return;
  }
  const JsonVariantConst root = doc.as<JsonVariantConst>();
  ResourceLock resources(*this);
  authUserCode = variantToString(resolvePath(root, manifest.authCodePath));
  authVerifyUrl = variantToString(resolvePath(root, manifest.authVerifyPath));
  authDeviceCode = variantToString(resolvePath(root, manifest.authDeviceCodePath));
  const long interval = resolvePath(root, manifest.authIntervalPath) | 5L;
  const long expires = resolvePath(root, manifest.authExpiresPath) | 900L;
  if (authUserCode.empty() || authDeviceCode.empty()) {
    fail(StrId::STR_PLUGIN_AUTH_FAILED);
    return;
  }
  authIntervalMs = (interval < 3 ? 3 : interval) * 1000UL;
  authDeadlineMs = millis() + (expires < 60 ? 60 : expires) * 1000UL;
  const auto nextPoll = millis() + authIntervalMs;
  {
    ModelLock lock(*this);
    ui.auth_next_poll_ms = nextPoll;
  }
  setState(State::AUTH);
  requestUpdate();
}

void PluginCatalogActivity::pollAuth() {
  // Checked before polling so an expired code fails even while every poll
  // hits a transport error (offline).
  if (static_cast<long>(millis() - authDeadlineMs) >= 0) {
    fail(StrId::STR_PLUGIN_AUTH_FAILED);
    return;
  }
  const auto nextPoll = millis() + authIntervalMs;
  {
    ModelLock lock(*this);
    ui.auth_next_poll_ms = nextPoll;
  }

  pluginhttp::RequestSpec req;
  req.method = manifest.pollReq.method;
  if (!buildRequest(req, manifest.pollReq.url, manifest.pollReq.body, manifest.pollReq.headers, nullptr, {}, true)) {
    fail(StrId::STR_PLUGIN_AUTH_FAILED);
    return;
  }
  String response;
  const int status = apiRequest(req, response);
  if (status < 0) return;  // transient transport failure: keep polling

  JsonDocument doc;
  if (deserializeJson(doc, response) == DeserializationError::Ok) {
    const JsonVariantConst root = doc.as<JsonVariantConst>();
    const std::string newToken = variantToString(resolvePath(root, manifest.authTokenPath));
    if (!newToken.empty()) {
      if (!saveToken(newToken)) {
        fail(StrId::STR_PLUGIN_AUTH_FAILED);
        return;
      }
      startBrowse();
      return;
    }
    const std::string code = variantToString(resolvePath(root, manifest.authErrorPath));
    if (code == "slow_down") {
      authIntervalMs += 5000;
    } else if (code == "expired_token" || code == "access_denied") {
      fail(StrId::STR_PLUGIN_AUTH_FAILED);
      return;
    }
    // authorization_pending (or anything unrecognized): keep polling
  }
}

void PluginCatalogActivity::downloadItem(const int itemIndex) {
  // Own only the selected item's metadata while TLS needs the catalog's heap.
  Item item;
  {
    ResourceLock lock(*this);
    item = std::move(items[itemIndex]);
    beginDownload(item.title);
    releaseRows();
    std::vector<Item>().swap(items);
  }
  requestUpdateAndWait();
  const auto result = manifest.isBundle() && !item.files.empty() ? downloadBundle(item) : downloadBook(item);
  session.reset();
  item = {};
  // Reuse the SD response without another network request or resetting navigation.
  if (!parseBrowseResponse()) return;
  session = makeUniqueNoThrow<freeink::SecureHttpClient>();
  if (!session) LOG_ERR("PCAT", "OOM: browse client; using per-request connections");
  finishDownload(result);
}

void PluginCatalogActivity::downloadFinished(const bool cancelled) {
  setState(cancelled ? State::BROWSING : State::DONE);
  requestUpdate();
}

HttpDownloader::DownloadError PluginCatalogActivity::downloadBundle(const Item& item) {
  session.reset();
  std::string subdir;
  if (!substituted(manifest.bundleSubdir, subdir, &item)) return HttpDownloader::HTTP_ERROR;
  // Reject traversal, the current-directory alias, and ambiguous backslash separators.
  if (subdir.empty() || subdir == "." || subdir.find("..") != std::string::npos ||
      subdir.find('\\') != std::string::npos || subdir.front() == '/') {
    return HttpDownloader::FILE_ERROR;
  }
  std::string root = downloadDir();
  while (!root.empty() && root.back() == '/') root.pop_back();
  std::string dir;
  if (PluginLocations::shouldReuseInstalledBundleDir(root, subdir)) {
    dir = PluginLocations::findPluginDir(subdir.c_str());
  }
  if (dir.empty()) {
    dir = root;
    dir += '/';
    dir += subdir;
  }
  if (Storage.exists(dir.c_str())) {
    HalFile target = Storage.open(dir.c_str());
    if (!target || !target.isDirectory()) {
      LOG_ERR("PCAT", "bundle target is not a directory: %s", dir.c_str());
      return HttpDownloader::FILE_ERROR;
    }
  } else if (!Storage.mkdir(dir.c_str())) {
    LOG_ERR("PCAT", "bundle mkdir failed: %s", dir.c_str());
    return HttpDownloader::FILE_ERROR;
  }
  std::string base = item.base;
  if (!base.empty() && base.back() != '/') base += '/';
  const size_t total = item.files.size();
  // Every file downloads to <dest>.new first and replaces <dest> only once the
  // whole bundle has arrived, so a failed or cancelled update leaves the
  // installed plugin/theme as it was (a half-installed folder would show up
  // broken in the next discovery scan).
  std::vector<std::string> staged;  // final destinations with a complete .new
  staged.reserve(total);
  bool complete = false;
  ScopedCleanup rollback{[&] {
    if (complete) return;
    for (const auto& path : staged) Storage.remove((path + ".new").c_str());
    Storage.rmdir(dir.c_str());  // only succeeds when the folder is empty (a fresh install)
  }};
  for (size_t i = 0; i < total; i++) {
    std::string rel = item.files[i];
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    const std::string dest = dir + "/" + rel;
    if (rel.empty() || !protectedpaths::isPluginPath(dest)) {
      // A manifest listing traversal or credential-store entries is hostile
      // or broken either way; abort rather than install a bundle with holes.
      LOG_ERR("PCAT", "unsafe bundle entry rejected: %s", item.files[i].c_str());
      return HttpDownloader::FILE_ERROR;
    }
    // Create any intermediate folders for nested files ("assets/icon.bin").
    const size_t slash = dest.find_last_of('/');
    if (slash != std::string::npos) {
      const std::string parent = dest.substr(0, slash);
      if (!Storage.exists(parent.c_str())) Storage.mkdir(parent.c_str());
    }
    pluginhttp::RequestSpec req;
    if (!buildRequest(req, rel, "", {}, nullptr, base, false, true)) return HttpDownloader::HTTP_ERROR;
    const auto result = downloadFile(req.url, dest + ".new");
    if (result != HttpDownloader::OK) return result;
    staged.push_back(dest);
  }
  // ponytail: a failed swap mid-loop leaves a mixed old/new bundle; a
  // directory-level swap would close that, at the cost of copying the tree.
  for (size_t i = 0; i < staged.size(); i++) {
    if (!Storage.replaceFile((staged[i] + ".new").c_str(), staged[i].c_str())) {
      LOG_ERR("PCAT", "bundle swap failed: %s", staged[i].c_str());
      for (size_t j = i; j < staged.size(); j++) Storage.remove((staged[j] + ".new").c_str());
      complete = true;  // already-swapped files stay; the rollback must not touch them
      return HttpDownloader::FILE_ERROR;
    }
  }
  complete = true;
  emitBookDownloaded(manifestPath, staged.empty() ? "" : staged.front(), item.title);
  // Bundles are how plugins install (plugin-store); pick up any new event
  // subscriptions without a restart. Cheap: a few small manifest reads.
  pluginevents::refreshSubscriptions();
  return HttpDownloader::OK;
}

HttpDownloader::DownloadError PluginCatalogActivity::downloadBook(const Item& item) {
  // Resolve the file URL: either the template itself, or one API hop away.
  std::string fileUrl, filePrefix;
  bool sendDownloadHeaders = manifest.dlUrlPath.empty();
  if (!manifest.dlUrlPath.empty()) {
    String response;
    const auto resolve = [&] {
      pluginhttp::RequestSpec req;
      req.method = manifest.downloadReq.method;
      if (!buildRequest(req, manifest.downloadReq.url, manifest.downloadReq.body, manifest.downloadReq.headers, &item))
        return -1;
      fileUrl = std::move(req.url);
      return pluginhttp::request(session.get(), fileUrl, req.method, req.body, req.headers, response, MAX_API_RESPONSE);
    };
    int status = resolve();
    // An expired password-grant token: mint a fresh one and retry once.
    if ((status == 401 || status == 403) && manifest.hasPasswordGrant() && refreshCredentialToken()) {
      status = resolve();
    }
    if (status < 200 || status >= 300) {
      return HttpDownloader::HTTP_ERROR;
    }
    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
      return HttpDownloader::HTTP_ERROR;
    }
    const auto origin = UrlUtils::origin(fileUrl);
    const auto resolved = resolvePath(doc.as<JsonVariantConst>(), manifest.dlUrlPath);
    if (resolved.as<JsonString>().size() > plugincatalog::MAX_REQUEST_BYTES) return HttpDownloader::HTTP_ERROR;
    std::string returned = variantToString(resolved);
    sendDownloadHeaders = UrlUtils::isServerPath(returned) && !origin.empty();
    if (sendDownloadHeaders)
      fileUrl.resize(origin.size());
    else
      fileUrl.clear();
    filePrefix = std::move(fileUrl);
    fileUrl = std::move(returned);
    if (fileUrl.empty()) return HttpDownloader::FILE_ERROR;
  }

  const std::string folderDir = downloadDir();
  const char* folder = folderDir.c_str();
  bool haveFolder = folder[0] != '\0';
  if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) {
    LOG_ERR("PCAT", "mkdir failed for %s, using SD root", folder);
    haveFolder = false;
  }

  // Sanitize after substitution so the byte limit applies to the complete
  // filename and does not remove an extension already present in {title}.
  std::string filename;
  if (!substituted(manifest.filenameTpl, filename, &item)) return HttpDownloader::HTTP_ERROR;
  filename = StringUtils::sanitizeFilenamePreservingExtension(filename);
  if (filename.empty() || filename.find("..") != std::string::npos || filename.find('/') != std::string::npos) {
    LOG_ERR("PCAT", "unsafe filename rejected: %s", filename.c_str());
    return HttpDownloader::FILE_ERROR;
  }
  std::string dest;
  dest.reserve((haveFolder ? folderDir.size() : 0) + 1 + filename.size());
  if (haveFolder) dest += folder;
  dest += '/';
  dest += filename;
  if (!protectedpaths::isPluginPath(dest)) {
    LOG_ERR("PCAT", "unsafe download dest rejected: %s", dest.c_str());
    return HttpDownloader::FILE_ERROR;
  }

  // A full URL from url_path may be pre-signed and already authorized.
  const auto fetchFile = [&] {
    pluginhttp::RequestSpec req;
    const pluginhttp::Headers none;
    DownloadCredentials credentials;
    const bool literal = !fileUrl.empty();
    if (!buildRequest(req, literal ? fileUrl : manifest.downloadReq.url, "",
                      sendDownloadHeaders ? manifest.downloadReq.headers : none, &item, filePrefix, false, literal,
                      &credentials))
      return HttpDownloader::HTTP_ERROR;
    if (req.url.empty()) return HttpDownloader::FILE_ERROR;
    fileUrl = std::move(req.url);
    filePrefix.clear();
    return downloadFile(fileUrl, dest, credentials.user, credentials.password, req.headers);
  };
  session.reset();  // free browse TLS before the large file GET
  auto result = fetchFile();
  if (result == HttpDownloader::UNAUTHORIZED && sendDownloadHeaders && manifest.hasPasswordGrant() &&
      refreshCredentialToken()) {
    result = fetchFile();
  }
  if (result != HttpDownloader::OK) return result;
  clearBookCache(dest);
  if (library::isLibraryBookFile(dest)) library::markLibraryIndexDirty();

  // Optional per-book sidecar (e.g. a service book id keyed by the file's
  // path hash) so a later sync stage can associate the file with the service.
  if (!manifest.sidecarPath.empty() && !manifest.sidecarBody.empty()) {
    const std::string md5 = md5Hex(dest);
    std::string path;
    if (!substituted(manifest.sidecarPath, path, &item, md5, dest)) return HttpDownloader::HTTP_ERROR;
    // Sidecar paths legitimately contain '/', but the substituted fields must
    // not climb out of the tree or land on a credential store.
    if (!protectedpaths::isPluginPath(path)) {
      LOG_ERR("PCAT", "unsafe sidecar path rejected: %s", path.c_str());
    } else {
      std::string body;
      if (!substituted(manifest.sidecarBody, body, &item, md5, dest)) return HttpDownloader::HTTP_ERROR;
      if (!Storage.writeFile(path.c_str(), String(body.c_str())))
        LOG_ERR("PCAT", "Sidecar write failed: %s", path.c_str());
    }
  }

  emitBookDownloaded(manifestPath, dest, item.title);
  return HttpDownloader::OK;
}

void PluginCatalogActivity::catalogEffect(const XpUiEffect& effect) {
  switch (effect.kind) {
    case XP_UI_CATALOG_COMPLETE:
      computeInstallStatus();
      releaseRows();
      setState(State::BROWSING);
      requestUpdate();
      break;
    case XP_UI_CATALOG_POLL_AUTH:
      pollAuth();
      break;
    case XP_UI_CATALOG_UNINSTALL:
      confirmUninstallPlugin(effect.value);
      break;
    case XP_UI_CATALOG_HUB_OPTION:
      if (effect.value == 0) {
        auto installer = makeUniqueNoThrow<PluginHubInstallActivity>(renderer, mappedInput, false);
        if (!installer) {
          LOG_ERR("PCAT", "OOM: Plugin Hub installer");
          return;
        }
        startActivityForResult(std::move(installer), [this](const ActivityResult& result) {
          const bool ignoreBack = result.isCancelled && mappedInput.isPressed(MappedInputManager::Button::Back);
          {
            ModelLock lock(*this);
            ui.ignore_back = ignoreBack;
          }
          enterPluginPicker();
        });
      } else if (effect.value == 1) {
        SETTINGS.pluginHubPromptHidden = 1;
        SETTINGS.saveToFile();
        enterPluginPicker();
      }
      requestUpdate();
      break;
    default:
      CatalogActivity::catalogEffect(effect);
      break;
  }
}

void PluginCatalogActivity::retryBrowse() {
  if (getState() == State::NO_TOKEN && manifest.hasDeviceCode()) {
    beginAuth();
  } else if ((!manifest.browseListsUrl.empty() && !listIndexLoaded) || wantsListPicker()) {
    startBrowse();
  } else {
    beginLoading();
    fetchPage(ui.page);
  }
}

// Back on the picker leaves the activity; the base routes it here via
// handleButtons. The catalog states route their Back through exitCatalog()
// instead, landing back on the picker.
void PluginCatalogActivity::onBackButton() {
  if (getState() == State::ERROR || getState() == State::CHECK_WIFI || getState() == State::LOADING ||
      getState() == State::NO_TOKEN) {
    exitCatalog();
    return;
  }
  if (getState() == State::PLUGIN_PICKER) {
    if (rootMode) {
      onGoHome();  // home launch replaced the home screen (root)
    } else {
      finish();  // Settings launch pushed us
    }
    return;
  }
  if (searchActive) {
    // Leave the results and return to the pre-search view (picker or page 1).
    {
      ResourceLock resources(*this);
      searchActive = false;
      searchQuery.clear();
      if (manifest.browseLists.size() == 1) {
        ModelLock lock(*this);
        ui.current_list = 0;
      }  // Its notice was already accepted.
    }
    startBrowse();
    return;
  }
  if (manifest.isXmlList() && !browseHistory.empty()) {
    browseCurrentUrl = browseHistory.back();
    browseHistory.pop_back();
    beginLoading();
    fetchPage(ui.page);
  } else if (getState() == State::BROWSING && ui.current_list >= 0 && manifest.browseLists.size() > 1) {
    // Browsing a picked list: Back returns to the list picker, not out.
    {
      ModelLock lock(*this);
      ui.current_list = -1;
    }
    startBrowse();
  } else {
    exitCatalog();
  }
}

void PluginCatalogActivity::activateIndex(const int index) {
  if (getState() == State::PLUGIN_PICKER) {
    if (index < 0 || index >= rowCount()) return;
    if (showOpds && index == 0) {
      activityManager.goToBrowser();  // replaces this screen with the OPDS browser
      return;
    }
    const int pluginStart = showOpds ? 1 : 0;
    if (showPluginHubInstallRow && index == pluginStart) {
      ResourceLock resources(*this);
      {
        ModelLock lock(*this);
        ui.popup = 1;
      }
      closeRouting();
      requestUpdate();
      return;
    }
    const int syntheticHubRows = showPluginHubInstallRow ? 1 : 0;
    const PluginRef& plugin = installedPlugins[index - pluginStart - syntheticHubRows];
    const auto action = PluginLocations::pickerAction(plugin.deviceKind, !plugin.readmePath.empty());
    if (action == PluginLocations::PickerAction::Readme) {
      // A plain paged text view, not the book reader: viewing instructions must
      // not touch the last-read book, recents, progress, or reader events.
      // Capped: a README is setup notes, not a book, and lives in RAM here.
      static constexpr size_t MAX_README_BYTES = 16 * 1024;
      std::string text;
      if (!Storage.readFileToString("PCAT", plugin.readmePath, MAX_README_BYTES, text)) {
        LOG_ERR("PCAT", "README unreadable, empty, or over %u bytes: %s", static_cast<unsigned>(MAX_README_BYTES),
                plugin.readmePath.c_str());
        return;
      }
      XpDefinitionNativeTransfer transfer{};
      transfer.title = &plugin.title;
      transfer.text.move = &text;
      transfer.kind = XpDefinitionNativeTransfer::TextKind::Move;
      if (!xp_app_definition_open_native(&transfer)) {
        LOG_ERR("PCAT", "OOM: README viewer");
        return;
      }
      resultHandler = [](const ActivityResult&) {};
      return;
    }
    if (action != PluginLocations::PickerAction::Catalog) return;
    {
      ResourceLock resources(*this);
      pickerReturnName = plugin.name;
      manifestPath = plugin.manifestPath;
      catalogTitle = plugin.title;
    }
    enterCatalog();
    return;
  }
  if (getState() == State::LIST_PICKER) {
    if (index < 0 || index >= static_cast<int>(manifest.browseLists.size())) return;
    openList(index);
    return;
  }
  // The pager rows bracket the items: "Previous page" ahead of them past
  // page 1, "Next page" after them while more pages exist.
  int itemIndex;
  bool paging;
  {
    ResourceLock resources(*this);
    {
      ModelLock lock(*this);
      itemIndex = xp_ui_catalog_item(&ui, index, items.size());
    }
    paging = itemIndex == -1 || (nextRowVisible() && itemIndex == static_cast<int>(items.size()));
    if (paging) beginLoading();
  }
  if (paging) {
    fetchPage(ui.page);
    return;
  }
  activateItem(itemIndex);
}

void PluginCatalogActivity::activateItem(const int itemIndex) {
  if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return;
  const Item& item = items[itemIndex];
  if (manifest.isXmlList() && item.isDir) {
    browseHistory.push_back(browseCurrentUrl);
    browseCurrentUrl = item.url;
    beginLoading();
    fetchPage(ui.page);
    return;
  }
  if (!manifest.isXmlList() && manifest.hasItemDetails()) {
    showItemDetails(itemIndex);
    return;
  }
  downloadItem(itemIndex);
}

bool PluginCatalogActivity::loadItemDetails(const int itemIndex, ItemDetails& details) {
  if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return false;
  const Item& target = items[itemIndex];
  // Rows are matched by id, or by title+version when the catalog has no ids.
  const auto isTarget = [&target](const JsonListParser::Row& row) {
    if (!target.id.empty()) return row.field[JsonListParser::F_ID] == target.id;
    return row.field[JsonListParser::F_TITLE] == target.title &&
           (target.version.empty() || row.field[JsonListParser::F_VERSION] == target.version);
  };
  const std::string unused;
  // Every pass keys rows on title/id/version and reads three more fields.
  const auto streamFor = [&](auto& capture, const std::string* author, const std::string* extraUrl,
                             const std::string* extraBase) {
    const std::string* fields[JsonListParser::F_COUNT] = {};
    fields[JsonListParser::F_TITLE] = &manifest.titlePath;
    fields[JsonListParser::F_AUTHOR] = author;
    fields[JsonListParser::F_ID] = &manifest.idPath;
    fields[JsonListParser::F_URL] = extraUrl;
    fields[JsonListParser::F_VERSION] = &manifest.versionPath;
    fields[JsonListParser::F_BASE] = extraBase;
    auto parser = makeUniqueNoThrow<JsonListParser>(
        manifest.itemsPath, fields, unused,
        [](void* context, JsonListParser::Row& row) {
          (*static_cast<std::remove_reference_t<decltype(capture)>*>(context))(row);
        },
        &capture);
    return streamBrowseFile(parser.get());
  };

  bool found = false;
  auto captureMetadata = [&](JsonListParser::Row& row) {
    if (found || !isTarget(row)) return;
    details.author = std::move(row.field[JsonListParser::F_AUTHOR]);
    details.description = std::move(row.field[DETAIL_DESCRIPTION]);
    details.repository = std::move(row.field[DETAIL_REPOSITORY]);
    found = true;
  };
  const bool metadataFound =
      streamFor(captureMetadata, &manifest.detailAuthorPath, &manifest.descriptionPath, &manifest.repositoryPath) &&
      found;
  if (manifest.detailsUrlPath.empty()) return metadataFound;

  bool pageFound = false;
  auto capturePage = [&](JsonListParser::Row& row) {
    if (pageFound || !isTarget(row)) return;
    details.detailsUrl = std::move(row.field[DETAIL_PAGE_URL]);
    details.detailsFormat = std::move(row.field[DETAIL_PAGE_FORMAT]);
    pageFound = true;
  };
  return (streamFor(capturePage, &unused, &manifest.detailsUrlPath, &manifest.detailsFormatPath) && pageFound) ||
         metadataFound;
}

void PluginCatalogActivity::showItemDetails(const int itemIndex) {
  if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return;
  const Item& item = items[itemIndex];

  ItemDetails details;
  loadItemDetails(itemIndex, details);

  DetailsSpec spec;
  spec.header = catalogTitle;
  spec.title = item.title;
  if (!details.author.empty()) spec.author = std::string(tr(STR_AUTHOR)) + ": " + details.author;
  if (!details.repository.empty()) spec.source = std::string(tr(STR_REPOSITORY)) + ": " + details.repository;
  if (!details.detailsUrl.empty()) {
    String response;
    pluginhttp::RequestSpec req;
    int status = -1;
    if (buildRequest(req, details.detailsUrl, "", {}, &item)) {
      pluginhttp::RequestSpec browse;
      if (!UrlUtils::isServerPath(req.url) ||
          (buildRequest(browse, activeBrowseUrl(), "", {}, nullptr, activeBrowseOrigin()) &&
           UrlUtils::origin(browse.url).size() + req.url.size() <= plugincatalog::MAX_REQUEST_BYTES)) {
        UrlUtils::resolveServerPath(req.url, browse.url);
        status = pluginhttp::request(session.get(), req.url, "GET", "", {}, response, MAX_ITEM_DETAILS_RESPONSE);
      }
    }
    const bool html = details.detailsFormat == "html";
    if (status < 200 || status >= 300 || response.length() == 0) {
      LOG_INF("PCAT", "item details fetch failed: HTTP %d", status);
    } else if (html || details.detailsFormat.empty() || details.detailsFormat == "text") {
      spec.description.assign(response.c_str(), response.length());
      spec.descriptionHtml = html;
    } else {
      LOG_INF("PCAT", "unsupported item details format: %s", details.detailsFormat.c_str());
    }
  }
  if (spec.description.empty()) spec.description = std::move(details.description);

  const auto addFact = [&spec](const std::string& fact) {
    if (fact.empty()) return;
    if (!spec.metadata.empty()) spec.metadata += " \xC2\xB7 ";
    spec.metadata += fact;
  };
  if (!item.version.empty()) addFact(std::string(tr(STR_VERSION)) + " " + item.version);
  addFact(item.status);

  const bool updateAvailable = item.status == tr(STR_UPDATE_AVAILABLE);
  spec.actionLabel = updateAvailable ? tr(STR_UPDATE) : (manifest.tracksInstalls() ? tr(STR_INSTALL) : tr(STR_FETCH));

  auto detailsActivity = makeUniqueNoThrow<DetailsActivity>(renderer, mappedInput, std::move(spec));
  if (!detailsActivity) {
    LOG_ERR("PCAT", "OOM: item details");
    return;
  }
  startActivityForResult(std::move(detailsActivity), [this, itemIndex](const ActivityResult& result) {
    if (result.isCancelled || itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) return;
    downloadItem(itemIndex);
  });
}

void PluginCatalogActivity::drawFooter() {
  if (paintState.popup) {
    CatalogUiActivity::drawFooter();
    return;
  }
  MappedInputManager::Labels labels;
  switch (static_cast<State>(paintState.mode)) {
    case State::BROWSING:
    case State::LIST_PICKER:
    case State::PLUGIN_PICKER: {
      const int count = rowCount();
      const int prevOff = prevRowVisible() ? 1 : 0;
      const int itemSel = paintState.list.selected - prevOff;
      const char* confirmLabel;
      if (static_cast<State>(paintState.mode) != State::BROWSING) {
        confirmLabel = count > 0 ? tr(STR_OPEN) : "";
      } else {
        const bool onItem = itemSel >= 0 && itemSel < static_cast<int>(items.size());
        const bool onDir = manifest.isXmlList() && onItem && items[itemSel].isDir;
        const bool onDetails = !manifest.isXmlList() && onItem && manifest.hasItemDetails();
        confirmLabel = count == 0 ? "" : (onDir ? tr(STR_OPEN) : (onDetails ? tr(STR_DETAILS) : tr(STR_FETCH)));
      }
      // On the top row of a searchable catalog the previous-nav slot becomes
      // Search (front Left), mirroring the OPDS browser's side-button search.
      const bool searchable = static_cast<State>(paintState.mode) == State::BROWSING && manifest.hasSearch() &&
                              paintState.list.selected == 0;
      const char* up = searchable ? tr(STR_SEARCH) : (count > 1 ? tr(STR_DIR_UP) : "");
      const char* down = count > 1 ? tr(STR_DIR_DOWN) : "";
      labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, up, down);
      break;
    }
    case State::ERROR:
    case State::NO_TOKEN: {
      const bool canSignIn = static_cast<State>(paintState.mode) == State::NO_TOKEN && manifest.hasDeviceCode();
      labels = mappedInput.mapLabels(tr(STR_BACK), canSignIn ? tr(STR_PLUGIN_SIGN_IN) : tr(STR_RETRY), "", "");
      break;
    }
    case State::DOWNLOADING:
      labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      break;
    default:  // CHECK_WIFI / LOADING / AUTH / DONE (and child-activity handoffs)
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

std::string PluginCatalogActivity::browsingHeaderLabel() const {
  std::string label = catalogTitle;
  if (searchActive) {
    label = std::string(tr(STR_SEARCH)) + ": " + searchQuery;
  } else if (static_cast<State>(paintState.mode) == State::BROWSING && paintState.current_list >= 0 &&
             paintState.current_list < static_cast<int>(manifest.browseLists.size())) {
    label = manifest.browseLists[paintState.current_list].title;
  }
  if (static_cast<State>(paintState.mode) == State::BROWSING && paintState.page > 1) {
    char suffix[16];
    snprintf(suffix, sizeof(suffix), " %d", static_cast<int>(paintState.page));
    label += suffix;
  }
  return label;
}

void PluginCatalogActivity::prepareRows() {
  const bool list = static_cast<State>(paintState.mode) == State::BROWSING ||
                    static_cast<State>(paintState.mode) == State::LIST_PICKER;
  renderedTitle = list ? browsingHeaderLabel() : catalogTitle;
}

const char* PluginCatalogActivity::catalogText(uint32_t handle) const {
  switch (handle) {
    case 0x80000004:
      return static_cast<State>(paintState.mode) == State::PLUGIN_PICKER ? tr(STR_NO_PLUGINS_INSTALLED)
                                                                         : tr(STR_NO_ENTRIES);
    case 0x8000000d:
      return tr(STR_DOWNLOAD_COMPLETE);
    case 0x8000000e:
      return manifest.hasDeviceCode() ? tr(STR_PLUGIN_SIGN_IN_HINT) : tr(STR_PLUGIN_NOT_SIGNED_IN);
    case 0x80000014:
      return authVerifyUrl.c_str();
    case 0x80000015:
      return authUserCode.c_str();
    case 0x80000016:
      return tr(STR_PLUGIN_AUTH_WAITING);
    case 0x80000018:
      return tr(STR_PLUGIN_HUB);
    case 0x80000019:
      return tr(STR_INSTALL);
    case 0x8000001a:
      return tr(STR_HIDE);
    case 0x8000001b:
      return tr(STR_CANCEL);
    default:
      return CatalogActivity::catalogText(handle);
  }
}
void PluginCatalogActivity::paintCatalogResource(const XpUiCommand& command) {
  if (command.resource == 0x80000017) {
    const auto r = command.rect;
    QrUtils::drawQrCode(renderer, Rect{r.x, r.y, r.w, r.h}, authVerifyUrl);
  }
}

bool PluginCatalogActivity::rowAt(int index, XpUiCatalogRow& row) const {
  if (index < 0 || index >= rowCount()) return false;
  row = {};
  if (static_cast<State>(paintState.mode) == State::PLUGIN_PICKER) {
    if (showOpds && index-- == 0) {
      row.label = tr(STR_OPDS_BROWSER);
      row.subtitle = tr(STR_OPDS_SERVERS);
      return true;
    }
    if (showPluginHubInstallRow && index-- == 0) {
      row.label = tr(STR_PLUGIN_HUB);
      row.subtitle = tr(STR_PLUGIN_HUB_DESCRIPTION);
      row.value = ">";
      return true;
    }
    const auto& plugin = installedPlugins[index];
    row.label = plugin.title.c_str();
    row.subtitle =
        plugin.deviceKind == PluginLocations::DeviceKind::None ? tr(STR_PLUGIN_WEB_ONLY) : plugin.description.c_str();
    if (plugin.deviceKind == PluginLocations::DeviceKind::Catalog || !plugin.readmePath.empty()) row.value = ">";
  } else if (static_cast<State>(paintState.mode) == State::LIST_PICKER) {
    row.label = manifest.browseLists[index].title.c_str();
  } else {
    if (prevRowVisible() && index-- == 0) {
      row.label = tr(STR_PREV_PAGE);
      row.value = ">";
      return true;
    }
    if (index == static_cast<int>(items.size())) {
      row.label = tr(STR_NEXT_PAGE);
      row.value = ">";
      return true;
    }
    const auto& item = items[index];
    row.label = item.title.c_str();
    row.subtitle = item.author.c_str();
    row.value = item.isDir ? ">" : item.status.c_str();
  }
  if (row.subtitle && !row.subtitle[0]) row.subtitle = nullptr;
  if (row.value && !row.value[0]) row.value = nullptr;
  return true;
}
void PluginCatalogActivity::releaseRows() { closeRouting(); }

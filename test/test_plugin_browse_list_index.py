"""Exercise catalog requests, download ordering and error routing with scripted transport and storage."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/plugins/PluginCatalogActivity.cpp").read_text()
limits = re.findall(r"^constexpr size_t MAX_BROWSE_LIST(?:_INDEX_RESPONSE|S) = .*;$", source, re.M)
start = source.index("bool PluginCatalogActivity::loadBrowseListIndex(")
loader = source[start:source.index("\n}", start) + 2]
policy = source[source.index("std::string_view trimCsvToken"):source.index("bool matchesCsvRestriction")]
start = source.index("bool matchesListRequirements")
policy += source[start:source.index("\n}", start) + 2]
url_utils = (ROOT / "src/util/UrlUtils.cpp").read_text()
start = url_utils.index("bool isServerPath")
policy += "\nnamespace UrlUtils {\n" + url_utils[start:url_utils.rindex("}  // namespace UrlUtils")] + "\n}\n"

plugin_http = (ROOT / "src/util/PluginHttp.cpp").read_text()
start = plugin_http.index("std::string escapeUrl(")
policy += "\nnamespace pluginhttp {\n" + plugin_http[start:plugin_http.index("\n}", start) + 2] + "\n}\n"
start = plugin_http.index("freeink::SecureHttpClient* openClient(")
open_client = plugin_http[start:plugin_http.index("\n}", start) + 2]

def method(name):
    start = source.index(name)
    return source[start:source.index("\n}", start) + 2]

requests = "\n".join(method(name) for name in (
    "void PluginCatalogActivity::showItemDetails", "void PluginCatalogActivity::startBrowse",
    "bool PluginCatalogActivity::detailsRequest", "bool PluginCatalogActivity::refreshCredentialToken",
    "HttpDownloader::DownloadError PluginCatalogActivity::downloadBundle", "HttpDownloader::DownloadError PluginCatalogActivity::downloadBook",
    "bool PluginCatalogActivity::substituted", "bool PluginCatalogActivity::fetchBrowseResponse", "void PluginCatalogActivity::fetchPage",
    "bool PluginCatalogActivity::buildRequest", "const std::string& PluginCatalogActivity::activeBrowseUrl",
    "std::string_view PluginCatalogActivity::activeBrowseOrigin",
    "const std::string& PluginCatalogActivity::activeBrowseBody", "plugincatalogpaging::Paging PluginCatalogActivity::browsePaging"))
variables = source[source.index("struct PluginCatalogActivity::Variables"):source.index("bool PluginCatalogActivity::buildRequest")]
pick = method("const std::string& pick(")
stub = r"""
#include <ArduinoJson.h>
#include <Memory.h>
#include <algorithm>
#include <cassert>
#include <memory>
#include <string_view>
#include "util/FirmwareIdentity.h"
#include "activities/plugins/PluginCatalogTemplate.h"
#include "activities/plugins/PluginCatalogPaging.h"
#define CROSSPOINT_VERSION "test"
#include <string>
#include <utility>
#include <vector>

size_t largeAllocations = 0;
bool observeAllocations = false;
size_t allAllocations = 0;
const std::unique_ptr<int>* watchedSession = nullptr;
size_t connectedLimit = SIZE_MAX;
bool constrainAfterResponse = false;
bool requireSession = false;
// Model separate request and TLS blocks. The raw index URL fits the first;
// an origin copy would consume the only block that can fit the receive buffer.
int indexBlocks = -1;
int blocksAtIndexGet = -1;
// Model two large blocks during details transport. The SDK's parsed target
// occupies one. The other must be free again when the response grows.
bool constrainDetails = false;
void* detailsBlocks[2]{};
void* operator new(size_t bytes) {
  if (observeAllocations) {
    ++allAllocations;
    if (bytes >= 16000) ++largeAllocations;
  }
  if (watchedSession && *watchedSession && bytes > connectedLimit) throw std::bad_alloc();
  if (indexBlocks >= 0 && bytes > 4104 && indexBlocks-- == 0) throw std::bad_alloc();
  if (constrainDetails && bytes > 4096) {
    auto slot = std::find(std::begin(detailsBlocks), std::end(detailsBlocks), nullptr);
    if (slot == std::end(detailsBlocks)) throw std::bad_alloc();
    if (void* p = std::malloc(bytes)) return *slot = p;
    throw std::bad_alloc();
  }
  if (void* p = std::malloc(bytes)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
  for (auto& block : detailsBlocks)
    if (block == p) block = nullptr;
  std::free(p);
}
void operator delete(void* p, size_t) noexcept {::operator delete(p);}

struct String : std::string {
  using std::string::operator=;
  String(const char* s) : std::string(s) { ++live; }
  inline static int live = 0;
  String() { ++live; }
  ~String() { --live; }
};
#define LOG_ERR(...) ((void)0)

namespace freeink {
struct SecureHttpClient {
  std::string path;
  bool connected = false;
  void setUserAgent(const char*) {}
  void setInsecure() {}
  void setTimeout(int) {}
  void addHeader(const std::string&, const std::string&) { assert(connected); }
  bool begin(const std::string& wire) { path = wire; return true; }
  bool connect(const std::function<bool()>& abort) {
    connected = !abort || !abort();
    return connected;
  }
};
}
namespace pluginhttp {
using Headers = std::vector<std::pair<std::string, std::string>>;
}
freeink::SecureHttpClient* openClient(freeink::SecureHttpClient*, freeink::SecureHttpClient&,
                                    const std::string&, const pluginhttp::Headers&, const std::function<bool()>&);

namespace pluginhttp {
struct RequestSpec { std::string url, method, body; Headers headers; };
std::string escapeUrl(const std::string&);
std::string body;
std::string expectedDetailsRaw, expectedDetailsWire;
std::vector<std::string> pageUrls;
int requestToFile(int* session, const std::string& url, const std::string&, const std::string&, const Headers&,
                  const char*, size_t) {
  if (requireSession) assert(session);
  auto escaped = escapeUrl(url);
  pageUrls.push_back(escaped.empty() ? url : escaped);
  return 200;
}
Headers sent;
int nextStatus = 200, replayStatus = 200, calls = 0, mints = 0;
std::string minted = "fresh";
bool mintPasswordToken(int*, const std::string&, const std::string&, const std::string&, const Headers&,
                       const std::string&, std::string& out) {
  ++mints;
  out = minted;
  return true;
}
int request(int* session, const std::string& url, const std::string&, const std::string&, const Headers& headers,
            String& response, size_t maxSize) {
  if (requireSession) assert(session);
  if (indexBlocks >= 0) {
    blocksAtIndexGet = indexBlocks;
    auto receive = std::make_unique<char[]>(17 * 1024);
    assert(receive && indexBlocks == 0);
    indexBlocks = -1;
  }
  assert(body.size() <= maxSize);
  ++calls;
  sent = headers;
  if (constrainDetails) {
    assert(session);
    freeink::SecureHttpClient client;
    assert(openClient(&client, client, url, headers, {}) == &client);
    assert(client.path == expectedDetailsWire);
    response = body;
    assert(url == expectedDetailsRaw && url.capacity() < 4096);
    constrainDetails = false;
  } else {
    response = body;
  }
  if (constrainAfterResponse) connectedLimit = 6143;
  return std::exchange(nextStatus, replayStatus);
}
}

JsonVariantConst resolvePath(JsonVariantConst node, std::string path) {
  for (auto dot = path.find("."); dot != std::string::npos; dot = path.find(".")) {
    node = node[path.substr(0, dot)];
    path.erase(0, dot + 1);
  }
  return path.empty() ? node : node[path];
}

std::string variantToString(JsonVariantConst value) {
  if (value.is<int>()) return std::to_string(value.as<int>());
  return value | "";
}

constexpr int REQUEST_REFUSED = -2;
constexpr char BROWSE_TMP_PATH[] = "/browse";
constexpr size_t MAX_BROWSE_RESPONSE = 1024 * 1024;
#define LOG_INF(...) ((void)0)
enum { STR_AUTHOR, STR_REPOSITORY, STR_VERSION, STR_UPDATE_AVAILABLE, STR_UPDATE, STR_INSTALL, STR_FETCH };
const char* tr(int) {return "label";}
struct DetailsSpec {std::string header,title,author,source,description,metadata,actionLabel;bool descriptionHtml=false;};
struct DetailsActivity {DetailsActivity(int,int,DetailsSpec) {}};
struct ActivityResult {bool isCancelled;};
constexpr size_t MAX_ITEM_DETAILS_RESPONSE = 24 * 1024;
constexpr size_t MAX_API_RESPONSE = 48 * 1024;
struct HalFile {explicit operator bool() const {return true;} bool isDirectory() const {return true;}};
namespace PluginLocations {
bool shouldReuseInstalledBundleDir(const std::string&, const std::string&) {return false;}
std::string findPluginDir(const char*) {return {};}
}
namespace pluginevents {void refreshSubscriptions() {}}
struct {
  HalFile open(const char*) { return {}; }
  void rmdir(const char*) {}
  bool replaceFile(const char*, const char*) {++renames; return true;}
  int directories = 0, sidecars = 0, renames = 0;
  bool canMkdir = true;
  std::string contents;
  bool exists(const char*) { return directories != 0; }
  bool mkdir(const char*) { if (canMkdir) ++directories; return canMkdir; }
  bool writeFile(const char*, const String& s) { ++sidecars; contents = s; return true; }
  void remove(const char*) {}
} Storage;
namespace HttpDownloader {
enum DownloadError { OK, HTTP_ERROR, FILE_ERROR, UNAUTHORIZED, REQUEST_TOO_LARGE, ABORTED };

}
namespace protectedpaths { bool isPluginPath(const std::string& path) { return path.starts_with("/"); } }
namespace StringUtils { std::string sanitizeFilenamePreservingExtension(const std::string& s) {return s;} }
namespace library {
int changes = 0;
bool isLibraryBookFile(const std::string&) {return true;}
void markLibraryIndexDirty() {++changes;}
}
void clearBookCache(const std::string&) {}
std::string md5Hex(const std::string&) {return "abc";}

enum class StrId { STR_PLUGIN_MANIFEST_INVALID, STR_FETCH_FEED_FAILED, STR_DOWNLOAD_FAILED };
struct CatalogActivity {
  bool goHomeAfterCancel = false;
  StrId error{};
  void onGoHome() {}
  void downloadFinished(bool) {}
  void fail(StrId value) { error = value; }
  void finishDownload(HttpDownloader::DownloadError);
};
struct PluginCatalogActivity {
  struct ModelLock { explicit ModelLock(PluginCatalogActivity&) {} };
  enum class State { BROWSING, NO_TOKEN, LIST_PICKER };
  void setState(State) {}
  void requestUpdate() {}
  void resetSelection() {}
  void loadConfig() {}
  bool tokenPresent = true; bool loadToken() {return tokenPresent;}
  bool parseBrowseResponse() { return true; }
  bool failed = false; void fail(StrId reason) { assert(reason == StrId::STR_FETCH_FEED_FAILED); failed = true; }
  std::string browseCurrentUrl;
  void fetchPage(int);
  bool listIndexLoaded = false; int confirmations=0, picked=0, startedAuth=0;
  bool wantsListPicker() {return !manifest.browseLists.empty();}
  void beginLoading() {}
  void beginAuth() {++startedAuth;}
  void openList(int) {++picked;}
  void releaseRows() {}
  void startBrowse();
  bool fetchBrowseResponse();

  struct Manifest {
    struct BrowseList {
      std::string title, url, body, id, noticeTitle, noticeMessage, noticeConfirm, noticeCancel;
    };
    std::string listOrigin, searchUrl, searchBody, dlUser, dlPass;
    pluginhttp::RequestSpec browseReq, downloadReq, authReq;
    std::string authTokenPath, bundleSubdir = "bundle";
    std::string dlUrlPath, filenameTpl = "book.epub", sidecarPath, sidecarBody;
    int pageSize = 8;
    std::string xmlItem;
    bool xml = false; bool isXmlList() const { return xml; }
    std::string browseListsUrl = "https://example.org:8443/api/index";
    std::vector<BrowseList> browseLists;
    size_t fixedListCount = 0;
    pluginhttp::Headers listsHeaders;
    std::string listsItemsPath = "lists", listsTitlePath = "title", listsUrlPath = "url", listsIdPath = "id";
    std::string listsRequiresPath = "requires";
    bool password = false;
    bool hasDeviceCode() const {return true;} bool tracksInstalls() const {return false;}
    bool hasPasswordGrant() const { return password; }
  } manifest;
  struct ResourceLock {
    explicit ResourceLock(PluginCatalogActivity&) { assert(String::live == 0); }
  };
  std::unique_ptr<int> session;
  std::string token = "old", authDeviceCode, searchQuery;
  struct {int current_list = -1, page = 1, plugin_flags = 0;} ui;
  bool searchActive = false;
  struct Item {std::string id, title, author, url, base; std::vector<std::string> files;std::string version,status;};
  struct ItemDetails {std::string author,repository,detailsUrl,detailsFormat,description;};
  ItemDetails remote; std::vector<Item> items; std::string catalogTitle;
  int renderer=0,mappedInput=0;
  void loadItemDetails(int, ItemDetails& out) {out=remote;}
  void showItemDetails(int); void downloadItem(int) {assert(false);}
  template<class F>void startActivityForResult(std::unique_ptr<DetailsActivity>, F) {++confirmations;}
  pluginhttp::Headers config;
  struct Variables;
  bool substituted(const std::string&, std::string&, const Item* = nullptr, size_t = SIZE_MAX, std::string_view = {}, std::string_view = {}) const;
  struct DownloadCredentials { std::string user, password; };
  bool detailsRequest(std::string&, std::string_view, const Item&) const;
  bool buildRequest(pluginhttp::RequestSpec&, std::string_view, const std::string&, const pluginhttp::Headers&,
                    const Item* = nullptr, std::string_view = {}, bool = false, bool = false, DownloadCredentials* = nullptr) const;
  const std::string& activeBrowseUrl() const;
  std::string_view activeBrowseOrigin() const;
  const std::string& activeBrowseBody() const;
  plugincatalogpaging::Paging browsePaging() const;
  std::string manifestPath, directory;
  bool saveWorks = true;
  std::string savedToken;
  size_t largeAllocationsAtGet = 0;
  int events = 0, files = 0, attempts = 0, unauthorized = 0, tokenSaves = 0;
  std::vector<std::string> urls;
  std::vector<pluginhttp::Headers> fileHeaders;
  std::string downloadDir() {return directory;}
  bool saveToken(const std::string& value) {++tokenSaves; if (saveWorks) savedToken=value; return saveWorks;}
  void emitBookDownloaded(const std::string&, const std::string&, const std::string&) {++events;}
  HttpDownloader::DownloadError downloadFile(const std::string& url, const std::string&, const std::string&,
          const std::string&, const pluginhttp::Headers& headers) {
    urls.push_back(url);
    largeAllocationsAtGet = largeAllocations;
    fileHeaders.push_back(headers);
    if (attempts++ < unauthorized) return HttpDownloader::UNAUTHORIZED;
    ++files;
    return HttpDownloader::OK;
  }
  HttpDownloader::DownloadError downloadBook(const Item&);
  HttpDownloader::DownloadError downloadFile(const std::string& url, const std::string&) {
    urls.push_back(url); ++files; return HttpDownloader::OK;
  }
  HttpDownloader::DownloadError downloadBundle(const Item&);
  bool refreshCredentialToken(int&);
  bool loadBrowseListIndex(int&);
};
"""

catalog = (ROOT / "src/activities/CatalogActivity.cpp").read_text()
start = catalog.index("void CatalogActivity::finishDownload(")
requests += "\n" + catalog[start:catalog.index("\n}", start) + 2]

cases = r"""
int main() {
  CatalogActivity screen;
  for (auto result : {HttpDownloader::REQUEST_TOO_LARGE, HttpDownloader::HTTP_ERROR, HttpDownloader::FILE_ERROR}) {
    screen.finishDownload(result);
    assert(screen.error == (result == HttpDownloader::REQUEST_TOO_LARGE ? StrId::STR_FETCH_FEED_FAILED : StrId::STR_DOWNLOAD_FAILED));
  }
  int status = 0;
  PluginCatalogActivity empty;
  pluginhttp::body = R"({"lists":[{})";
  for (int i = 1; i < 4000; ++i) pluginhttp::body += ",{}";
  pluginhttp::body += "]}";
  assert(!empty.loadBrowseListIndex(status));
  assert(empty.manifest.browseLists.empty());
  assert(empty.manifest.browseLists.capacity() <= 64);

  PluginCatalogActivity valid;
  pluginhttp::body = R"({"lists":[{})";
  for (int i = 0; i < 75; ++i) {
    pluginhttp::body += R"(,{"title":"List )" + std::to_string(i) + R"(","url":"page"})";
  }
  pluginhttp::body += "]}";
  assert(valid.loadBrowseListIndex(status));
  assert(valid.manifest.browseLists.size() == 64);
  assert(valid.manifest.browseLists.capacity() <= 64);
  for (int i = 0; i < 64; ++i) {
    assert(valid.manifest.browseLists.at(i).title == "List " + std::to_string(i));
    assert(valid.manifest.browseLists.at(i).url == "page");
  }

  PluginCatalogActivity shared;
  shared.manifest.browseListsUrl = "https://" + std::string(4096, 'a') + "/index";
  pluginhttp::body = R"({"lists":[)";
  for (int i = 0; i < 64; ++i) {
    if (i) pluginhttp::body += ",";
    pluginhttp::body += R"({"title":"List","url":"/p"})";
  }
  pluginhttp::body += "]}";
  indexBlocks = 2;
  assert(shared.loadBrowseListIndex(status));
  assert(blocksAtIndexGet == 1);
  assert(shared.manifest.listOrigin == "https://" + std::string(4096, 'a'));
  assert(shared.manifest.browseLists.size() == 64);
  for (const auto& list : shared.manifest.browseLists) assert(list.url == "/p");

  for (const char* row : {R"({"title":"A","url":"https://other/book"})", R"({"title":"I","id":"123"})"}) {
    PluginCatalogActivity index;
    index.manifest.browseListsUrl = shared.manifest.browseListsUrl;
    pluginhttp::body = std::string("{\"lists\":[") + row + "]}";
    indexBlocks = 2;
    assert(index.loadBrowseListIndex(status));
    assert(blocksAtIndexGet == 1 && index.manifest.listOrigin.empty());
  }

  PluginCatalogActivity combined;
  combined.manifest.browseLists.push_back({"Fixed", "fixed"});
  combined.manifest.fixedListCount = 1;
  pluginhttp::body = "invalid JSON";
  assert(!combined.loadBrowseListIndex(status));
  assert(combined.manifest.browseLists.size() == 1);
  assert(combined.manifest.browseLists.front().title == "Fixed");
  pluginhttp::body = R"({"lists":[{"title":"Dynamic","url":"/dynamic"}]})";
  assert(combined.loadBrowseListIndex(status));
  assert(combined.manifest.browseLists.size() == 2);
  assert(combined.manifest.browseLists.front().title == "Fixed");
  assert(combined.manifest.browseLists.back().title == "Dynamic");
  assert(combined.manifest.browseLists.back().url == "/dynamic");
  combined.ui.current_list = 1;
  pluginhttp::RequestSpec rooted;
  assert(combined.buildRequest(rooted, combined.activeBrowseUrl(), "", {}, nullptr, combined.activeBrowseOrigin()));
  assert(rooted.url == "https://example.org:8443/dynamic");

  for (const int rejected : {401, 403}) {
    PluginCatalogActivity mapped;
    mapped.manifest.listsItemsPath = "outer.rows";
    mapped.manifest.listsTitlePath = "fields.label";
    mapped.manifest.listsIdPath = "key";
    mapped.manifest.listsUrlPath = "href";
    mapped.manifest.listsHeaders = {{"X-Token", "{token}"}};
    mapped.manifest.password = true;
    pluginhttp::body = R"({"outer":{"rows":[{"fields":{"label":"Mapped"},"key":42}]}})";
    pluginhttp::calls = 0;
    pluginhttp::nextStatus = rejected;
    assert(mapped.loadBrowseListIndex(status));
    assert(status == 200 && pluginhttp::calls == 2);
    assert(pluginhttp::sent.at(0).second == "fresh");
    assert(mapped.manifest.browseLists.size() == 1);
    assert(mapped.manifest.browseLists.front().title == "Mapped");
    assert(mapped.manifest.browseLists.front().id == "42");
    assert(mapped.manifest.browseLists.front().url.empty());
  }

  PluginCatalogActivity idOnly;
  idOnly.manifest.browseReq.url = "https://example/{list_id}?key={list_id_enc}&offset={offset}&limit={limit}";
  idOnly.manifest.listsIdPath = "key";
  pluginhttp::body = R"({"lists":[{"title":"Mapped","key":"a b/+"}]})";
  assert(idOnly.loadBrowseListIndex(status));
  assert(idOnly.manifest.listOrigin.empty());
  idOnly.ui.current_list = 0;
  idOnly.fetchPage(1);
  idOnly.fetchPage(2);
  assert(pluginhttp::pageUrls.size() == 2);
  assert(pluginhttp::pageUrls[0] == "https://example/a%20b/+?key=a%20b%2F%2B&offset=0&limit=9");
  assert(pluginhttp::pageUrls[1] == "https://example/a%20b/+?key=a%20b%2F%2B&offset=8&limit=9");

  PluginCatalogActivity::Item item{"a+/1", "A / Café", "", "http://x/y?z=2"};
  {
    PluginCatalogActivity dense;
    dense.session = std::make_unique<int>(1);
    for (int i = 100; i < 280; ++i) dense.config.emplace_back("k" + std::to_string(i), "");
    watchedSession = &dense.session;
    connectedLimit = 8500;
    pluginhttp::RequestSpec req;
    allAllocations = 0;
    observeAllocations = true;
    assert(dense.buildRequest(req, "/{id}", "", {}, &item));
    observeAllocations = false;
    assert(req.url == "/" + item.id && allAllocations == 0);
    dense.manifest.browseReq.url = "/{token}";
    requireSession = true;
    dense.fetchPage(1);
    dense.fetchPage(2);
    assert(!dense.failed && dense.session);
    dense.manifest.downloadReq.url = "/{id}";
    requireSession = false;
    assert(dense.downloadBook(item) == HttpDownloader::OK);
    assert(!dense.session && dense.attempts == 1);
    dense.session = std::make_unique<int>(1);
    requireSession = true;
    dense.manifest.dlUrlPath = "url";
    pluginhttp::body = R"({"url":"/file"})";
    assert(dense.downloadBook(item) == HttpDownloader::OK);
    requireSession = false;
    assert(!dense.session && dense.attempts == 2);
    watchedSession = nullptr;
  }
  for (bool api : {false, true}) {
    PluginCatalogActivity book;
    book.session = std::make_unique<int>(1);
    connectedLimit = api ? SIZE_MAX : 6143;
    requireSession = constrainAfterResponse = api;
    book.manifest.downloadReq.url = "https://host/{id}";
    book.manifest.downloadReq.headers = {{"X", std::string(6144, 'h')}};
    book.manifest.dlUser.assign(6144, 'u');
    book.manifest.dlPass = "p";
    if (api) {
      book.manifest.dlUrlPath = "url";
      pluginhttp::body = R"({"url":"/file"})";
    }
    watchedSession = &book.session;
    assert(book.downloadBook(item) == HttpDownloader::OK);
    assert(!book.session && book.fileHeaders.at(0) == book.manifest.downloadReq.headers);
    requireSession = constrainAfterResponse = false;
    watchedSession = nullptr;
  }
  {
    PluginCatalogActivity c;
    const std::string longKey(250, 'k');
    const std::string longValue(410, 'v');
    c.token = "{cfg.a}";
    c.config = {{"a","{cfg."}, {"b","joined"}, {"{cfg.x","overlap"}, {longKey,longValue}};
    const std::string tpl = "{token}b}/{cfg.{cfg.{cfg.x}/{cfg." + longKey + "}/{cfg.{cfg.";
    std::string expected = tpl;
    const auto replace = [&](const std::string& key, const std::string& value) {
      for (size_t i = 0; (i = expected.find(key, i)) != expected.npos; i += value.size()) {
        expected.replace(i, key.size(), value);
      }
    };
    replace("{token}", c.token);
    for (const auto& [key,value] : c.config) replace("{cfg." + key + "}", value);
    std::string actual;
    assert(c.substituted(tpl, actual) && actual == expected);
  }
  pluginhttp::RequestSpec aliases;
  assert(idOnly.buildRequest(aliases, "/{title}?id={id_enc}&title={title_enc}&url={url_enc}", "{title}",
                             {{"Title", "{title}"}}, &item));
  assert(pluginhttp::escapeUrl(aliases.url) == "/A%20/%20Caf%C3%A9?id=a%2B%2F1&title=A%20%2F%20Caf%C3%A9&url=http%3A%2F%2Fx%2Fy%3Fz%3D2");
  assert(aliases.body == item.title && aliases.headers.at(0).second == item.title);

  pluginhttp::body = R"({"lists":[{"title":"Mapped","key":")" + std::string(4096, ' ') + R"("}]})";
  assert(idOnly.loadBrowseListIndex(status));
  std::string escaped = "/";
  for (int i = 0; i < 4097; ++i) escaped += "%20";
  for (int delta : {-1, 0, 1}) {
    pluginhttp::RequestSpec out;
    std::string body(4096 - 6 + delta, 'b');
    bool fits = idOnly.buildRequest(out, "/{list_id_enc} ", body, {{"X", "v"}});
    assert(fits == (delta <= 0));
    if (fits) {
      assert(pluginhttp::escapeUrl(out.url) == escaped);
      assert(out.body == body && out.headers.size() == 1);
    } else assert(out.url.empty() && out.body.empty() && out.headers.empty());
  }

  for (int delta : {-1, 0, 1}) {
    PluginCatalogActivity xml;
    xml.manifest.xml = true;
    xml.manifest.xmlItem = "row";
    xml.config = {{"early", ""}, {"late", "{cfg.early}"}};
    xml.manifest.browseReq.url = "https://x/" + std::string(16384 + delta - 21, 'p') + "{cfg.late}";
    const size_t calls = pluginhttp::pageUrls.size();
    assert(xml.fetchBrowseResponse() == (delta <= 0));
    if (delta <= 0) {
      assert(xml.browseCurrentUrl.size() == size_t(16384 + delta));
      assert(xml.browseCurrentUrl.ends_with("{cfg.early}"));
      assert(pluginhttp::pageUrls.back() == xml.browseCurrentUrl.substr(0, xml.browseCurrentUrl.size() - 11));
    } else {
      assert(xml.failed && xml.browseCurrentUrl.empty() && pluginhttp::pageUrls.size() == calls);
    }
  }
  PluginCatalogActivity details;
  details.manifest.browseReq.url = "https://x/" + std::string(20000, 'z');
  for (int delta : {-1, 0, 1}) {
    std::string output;
    std::string path = "/" + std::string(16384 + delta - 10, 'a');
    largeAllocations = 0;
    observeAllocations = true;
    const bool fits = details.detailsRequest(output, path, item);
    observeAllocations = false;
    assert(fits == (delta <= 0) && largeAllocations == size_t(fits));
    if (delta <= 0) assert(output == "https://x" + path);
    else assert(output.empty() && output.capacity() < 100);
    details.items = {item};
    details.remote.detailsUrl = path;
    int calls = pluginhttp::calls, screens = details.confirmations;
    pluginhttp::body = "text";
    details.showItemDetails(0);
    assert(details.failed == (delta > 0) && pluginhttp::calls == calls + (delta <= 0));
    assert(details.confirmations == screens + (delta <= 0));
  }
  {
    PluginCatalogActivity book;
    book.manifest.browseReq.url = "https://{cfg.host}/browse";
    book.config = {{"host", "details.example"}, {"root", "/"}};
    book.items = {item};
    book.remote.detailsUrl = "{cfg.root}";
    pluginhttp::expectedDetailsRaw = "https://details.example/";
    pluginhttp::expectedDetailsWire = pluginhttp::expectedDetailsRaw;
    for (int i = 0; i < 1000; ++i) {
      book.remote.detailsUrl += "\xC3\xA9";
      pluginhttp::expectedDetailsRaw += "\xC3\xA9";
      pluginhttp::expectedDetailsWire += "%C3%A9";
    }
    book.session = std::make_unique<int>(1);
    pluginhttp::body.assign(6144, 'd');
    const int calls = pluginhttp::calls;
    constrainDetails = true;
    book.showItemDetails(0);
    assert(!constrainDetails && !detailsBlocks[0] && !detailsBlocks[1]);
    assert(!book.failed && book.confirmations == 1 && pluginhttp::calls == calls + 1 && book.session);
  }
  for (int delta : {-1, 0, 1}) {
    PluginCatalogActivity book;
    book.manifest.dlUrlPath = "url";
    book.manifest.downloadReq.url = "https://h/api";
    book.manifest.downloadReq.headers = {{"X", "1"}};
    std::string path = "/" + std::string(16384 + delta - 12, 'a');
    pluginhttp::body = "{\"url\":\"" + path + "\\u0000" + std::string(17000, 'b') + "\"}";
    assert(book.downloadBook(item) == (delta > 0 ? HttpDownloader::REQUEST_TOO_LARGE : HttpDownloader::OK));
    if (delta <= 0) assert(book.urls.at(0) == "https://h" + path);
    assert(book.events == int(delta <= 0) && book.files == book.events);
  }
  {
    PluginCatalogActivity book;
    book.manifest.downloadReq.url = "https://h/book";
    book.manifest.sidecarPath = "/sidecar";
    book.manifest.sidecarBody.assign(16384, 'b');
    largeAllocations = 0;
    observeAllocations = true;
    assert(book.downloadBook(item) == HttpDownloader::OK);
    observeAllocations = false;
    assert(book.largeAllocationsAtGet == 0 && largeAllocations > 0);
    assert(Storage.contents == book.manifest.sidecarBody);
    std::string notice;
    assert(book.substituted(std::string(16385, 'n'), notice) && notice.size() == 16385);
  }
  {
    PluginCatalogActivity book;
    book.manifest.downloadReq.url = "https://h/book";
    book.manifest.downloadReq.headers = {{"X", "{token}{token}{token}"}};
    book.manifest.password = true;
    book.unauthorized = 1;
    pluginhttp::minted.assign(5462, 't');
    assert(book.downloadBook(item) == HttpDownloader::REQUEST_TOO_LARGE);
    assert(book.attempts == 1 && book.savedToken == pluginhttp::minted && book.events == 0);
  }
  {
    PluginCatalogActivity book;
    pluginhttp::RequestSpec req;
    PluginCatalogActivity::DownloadCredentials credentials;
    book.token.assign(4096, ' ');
    assert(book.buildRequest(req, "https://h/{token}", "", {}, &item, {}, false, false, &credentials));
    assert(req.url == "https://h/" + book.token);
    assert(req.url.capacity() < 2 * req.url.size());
    assert(pluginhttp::escapeUrl(req.url).size() == 10 + 3 * book.token.size());
  }
  for (bool fitsAfterRefresh : {false, true}) {
    PluginCatalogActivity book;
    book.manifest.password = true;
    book.manifest.downloadReq.url = "https://h/with space/{token}";
    book.manifest.downloadReq.headers = {{"X", "{token}"}};
    book.manifest.sidecarPath = "{dest}.json";
    book.manifest.sidecarBody = std::string(16383, 'b') + "{token}";
    book.token = fitsAfterRefresh ? "xx" : "x";
    pluginhttp::minted = fitsAfterRefresh ? "x" : "xx";
    const std::string url = "https://h/with%20space/" + book.token;
    book.unauthorized = 1;
    int initial = library::changes, writes = Storage.sidecars;
    assert(book.downloadBook(item) == HttpDownloader::OK);
    assert(book.attempts == 2 && book.urls[0] == url && book.urls[1] == url);
    assert(book.fileHeaders[0][0].second != book.fileHeaders[1][0].second);
    assert(book.files == 1 && book.events == 1);
    assert(library::changes == initial + book.files && Storage.sidecars == writes + fitsAfterRefresh);
    assert(book.tokenSaves == 1 && book.savedToken == pluginhttp::minted);
    if (fitsAfterRefresh) assert(Storage.contents == std::string(16383, 'b') + "x");
  }
  for (bool canSave : {true, false}) {
    PluginCatalogActivity book;
    book.manifest.password = true;
    book.manifest.downloadReq.url = "https://h/api";
    book.manifest.downloadReq.method = "POST";
    book.manifest.dlUrlPath = "url";
    book.saveWorks = canSave;
    pluginhttp::nextStatus = 401;
    pluginhttp::replayStatus = 500;
    int calls = pluginhttp::calls;
    assert(book.downloadBook(item) == HttpDownloader::HTTP_ERROR);
    assert(pluginhttp::calls == calls + 1 + canSave && book.attempts == 0 && book.tokenSaves == 1);
    assert(book.savedToken == (canSave ? pluginhttp::minted : ""));
    pluginhttp::nextStatus = pluginhttp::replayStatus = 200;
  }
  for (bool present : {true, false}) {
    PluginCatalogActivity basic;
    basic.token.assign(4096, 'x');
    basic.manifest.dlUser = "{token}{token}{token}{token}";
    basic.manifest.dlPass = present ? "pass" : "{query_raw}";
    PluginCatalogActivity::DownloadCredentials credentials;
    pluginhttp::RequestSpec req;
    assert(basic.buildRequest(req, "url", "", {}, nullptr, {}, false, false, &credentials) == !present);
    assert(credentials.user.empty() && credentials.password.empty());
  }
  PluginCatalogActivity emptyBook;
  emptyBook.directory = "/books";
  emptyBook.manifest.downloadReq.url = "{url}";
  assert(emptyBook.downloadBook({}) == HttpDownloader::FILE_ERROR);
  assert(emptyBook.attempts == 0 && Storage.directories == 0 && emptyBook.events == 0);
  for (int delta : {-1, 0, 1}) {
    PluginCatalogActivity bundle;
    PluginCatalogActivity::Item entry;
    entry.base = "https://h";
    entry.files = {"manifest.json", "///" + std::string(16384 + delta - 10, 'x')};
    Storage.directories = Storage.renames = 0;
    assert(bundle.downloadBundle(entry) == (delta <= 0 ? HttpDownloader::OK : HttpDownloader::REQUEST_TOO_LARGE));
    if (delta <= 0) {
      assert(bundle.urls.at(1) == "https://h/" + entry.files[1].substr(3));
      assert(bundle.files == 2 && Storage.renames == 2 && bundle.events == 1);
    } else assert(bundle.files == 1 && Storage.renames == 0 && Storage.directories == 1 && bundle.events == 0);
    PluginCatalogActivity auth;
    auth.tokenPresent = false;
    auth.manifest.password = true;
    auth.manifest.authReq.url = std::string(16384 + delta, 'x');
    const int mints = pluginhttp::mints;
    assert(auth.fetchBrowseResponse() == (delta <= 0));
    assert(auth.failed == (delta > 0) && auth.tokenSaves == (delta <= 0) && pluginhttp::mints == mints + (delta <= 0));
    PluginCatalogActivity index;
    index.manifest.browseListsUrl = std::string(16384 + delta, 'x');
    pluginhttp::body = R"({"lists":[{"title":"Row","id":"1"}]})";
    int calls = pluginhttp::calls;
    assert(index.loadBrowseListIndex(status) == (delta <= 0));
    assert(pluginhttp::calls == calls + (delta <= 0));
    if (delta > 0) assert(status == REQUEST_REFUSED);
    PluginCatalogActivity picker;
    picker.manifest.browseLists.resize(1);
    picker.manifest.fixedListCount = 1;
    picker.manifest.browseListsUrl = index.manifest.browseListsUrl;
    picker.startBrowse();
    assert(picker.failed == (delta > 0) && picker.listIndexLoaded == (delta <= 0));
    PluginCatalogActivity signIn;
    signIn.tokenPresent = false;
    signIn.manifest.password = true;
    signIn.manifest.authReq.url = auth.manifest.authReq.url;
    signIn.startBrowse();
    assert(signIn.startedAuth == 0 && signIn.failed == (delta > 0));
  }
  for (int delta : {-1, 0, 1}) {
    PluginCatalogActivity book;
    book.directory = "/Books";
    book.manifest.downloadReq.url = "https://h/book";
    book.manifest.sidecarPath = "/safe";
    book.manifest.sidecarBody = std::string(16384 + delta - 10, 'b') + "{dest}";
    Storage.directories = 0;
    Storage.canMkdir = false;
    const int writes = Storage.sidecars;
    assert(book.downloadBook(item) == HttpDownloader::OK && book.events == 1);
    assert(Storage.sidecars == writes + (delta <= 0));
    if (delta <= 0) assert(Storage.contents.ends_with("/book.epub") && !Storage.contents.ends_with("/Books/book.epub"));

  }
  std::string shortWire = "https://example/book";
  for (int i = 0; i < 4096; ++i) shortWire += "{query}";
  PluginCatalogActivity compact;
  compact.manifest.downloadReq.url = shortWire;
  assert(compact.downloadBook(item) == HttpDownloader::OK && compact.urls.at(0) == "https://example/book");
  PluginCatalogActivity unsafe;
  unsafe.manifest.sidecarPath = "unsafe";
  unsafe.manifest.sidecarBody = std::string(20000, 'b');
  unsafe.manifest.downloadReq.url = "https://h/book";
  auto writes = Storage.sidecars;
  assert(unsafe.downloadBook(item) == HttpDownloader::OK && unsafe.events == 1 && Storage.sidecars == writes);
  PluginCatalogActivity filtered;
  filtered.manifest.listsRequiresPath = "meta.caps";
  pluginhttp::body = R"({"lists":[
      {"title":"Missing","url":"page"},
      {"title":"Empty","url":"page","meta":{"caps":[]}},
      {"title":"Supported","url":"page","meta":{"caps":["catalog.list_requirements","catalog.lists"]}},
      {"title":"Unsupported","url":"page","meta":{"caps":["catalog.list_requirements","unknown"]}},
      {"title":"Null","url":"page","meta":{"caps":null}},
      {"title":"Scalar","url":"page","meta":{"caps":"catalog.lists"}},
      {"title":"Number","url":"page","meta":{"caps":[1]}},
      {"title":"Blank","url":"page","meta":{"caps":[""]}}
  ]})";
  assert(filtered.loadBrowseListIndex(status));
  assert(filtered.manifest.browseLists.size() == 3);
  assert(filtered.manifest.browseLists[0].title == "Missing");
  assert(filtered.manifest.browseLists[1].title == "Empty");
  assert(filtered.manifest.browseLists[2].title == "Supported");
}
"""

with tempfile.TemporaryDirectory() as directory:
    unit = Path(directory) / "browse_list_index.cpp"
    executable = unit.with_suffix("")
    unit.write_text("\n".join([stub, *limits, policy, open_client, pick, variables, requests, loader, cases]))
    subprocess.run([sys.argv[1], "-std=c++20", "-DXP_SD_FONT_CATALOG", "-I", str(ROOT / "lib/Memory"), "-I", sys.argv[2], "-I", str(ROOT / "src"), "-I", str(ROOT / "rust/crates/xp-ffi/include"), str(unit), str(ROOT / "test/cmake/xp_ffi_host.cpp"), sys.argv[3], "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

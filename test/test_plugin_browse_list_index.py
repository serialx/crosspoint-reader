"""Exercise the production list-index loader with a stubbed HTTP response."""

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

def method(name):
    start = source.index(name)
    return source[start:source.index("\n}", start) + 2]

requests = "\n".join(method(name) for name in (
    "bool PluginCatalogActivity::substituted", "bool PluginCatalogActivity::fetchBrowseResponse", "void PluginCatalogActivity::fetchPage",
    "bool PluginCatalogActivity::buildRequest", "const std::string& PluginCatalogActivity::activeBrowseUrl",
    "std::string_view PluginCatalogActivity::activeBrowseOrigin",
    "const std::string& PluginCatalogActivity::activeBrowseBody", "plugincatalogpaging::Paging PluginCatalogActivity::browsePaging"))
variables = source[source.index("struct PluginCatalogActivity::Variables"):source.index("bool PluginCatalogActivity::buildRequest")]
pick = method("const std::string& pick(")
stub = r"""
#include <ArduinoJson.h>
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

struct String : std::string {
  using std::string::operator=;
  inline static int live = 0;
  String() { ++live; }
  ~String() { --live; }
};
#define LOG_ERR(...) ((void)0)

namespace pluginhttp {
using Headers = std::vector<std::pair<std::string, std::string>>;
struct RequestSpec { std::string url, method, body; Headers headers; };
std::string body;
std::vector<std::string> pageUrls;
int requestToFile(int*, const std::string& url, const std::string&, const std::string&, const Headers&,
                  const char*, size_t) {
  pageUrls.push_back(url);
  return 200;
}
Headers sent;
int nextStatus = 200, calls = 0;
int request(int*, const std::string&, const char*, const char*, const Headers& headers,
            String& response, size_t maxSize) {
  assert(body.size() <= maxSize);
  ++calls;
  sent = headers;
  response = body;
  return std::exchange(nextStatus, 200);
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

constexpr char BROWSE_TMP_PATH[] = "/browse";
constexpr size_t MAX_BROWSE_RESPONSE = 1024 * 1024;
struct { void remove(const char*) {} } Storage;
enum class StrId { STR_PLUGIN_MANIFEST_INVALID, STR_FETCH_FEED_FAILED };
struct PluginCatalogActivity {
  struct ModelLock { explicit ModelLock(PluginCatalogActivity&) {} };
  enum class State { BROWSING, NO_TOKEN };
  void setState(State) {}
  void requestUpdate() {}
  void resetSelection() {}
  void loadConfig() {}
  bool loadToken() { return true; }
  bool parseBrowseResponse() { return true; }
  bool failed = false; void fail(StrId reason) { assert(reason == StrId::STR_FETCH_FEED_FAILED); failed = true; }
  std::string browseCurrentUrl;
  void fetchPage(int);
  bool fetchBrowseResponse();

  struct Manifest {
    struct BrowseList {
      std::string title, url, body, id, noticeTitle, noticeMessage, noticeConfirm, noticeCancel;
    };
    std::string listOrigin, searchUrl, searchBody, dlUser, dlPass;
    pluginhttp::RequestSpec browseReq;
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
    bool hasPasswordGrant() const { return password; }
  } manifest;
  struct ResourceLock {
    explicit ResourceLock(PluginCatalogActivity&) { assert(String::live == 0); }
  };
  std::unique_ptr<int> session;
  std::string token = "old", authDeviceCode, searchQuery;
  struct {int current_list = -1, page = 1;} ui;
  bool searchActive = false;
  struct Item {std::string id, title, author, url;};
  pluginhttp::Headers config;
  struct Variables;
  bool substituted(const std::string&, std::string&, const Item* = nullptr, std::string_view = {}, std::string_view = {}) const;
  struct DownloadCredentials { std::string user, password; };
  bool buildRequest(pluginhttp::RequestSpec&, const std::string&, const std::string&, const pluginhttp::Headers&,
                    const Item* = nullptr, std::string_view = {}, bool = false, bool = false, DownloadCredentials* = nullptr, std::string* = nullptr) const;
  const std::string& activeBrowseUrl() const;
  std::string_view activeBrowseOrigin() const;
  const std::string& activeBrowseBody() const;
  plugincatalogpaging::Paging browsePaging() const;
  bool refreshCredentialToken() {
    assert(String::live == 0);
    token = "fresh";
    return true;
  }
  bool loadBrowseListIndex(int&);
};
"""

cases = r"""
int main() {
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
  assert(shared.loadBrowseListIndex(status));
  assert(shared.manifest.listOrigin == "https://" + std::string(4096, 'a'));
  assert(shared.manifest.browseLists.size() == 64);
  for (const auto& list : shared.manifest.browseLists) assert(list.url == "/p");

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
  idOnly.ui.current_list = 0;
  idOnly.fetchPage(1);
  idOnly.fetchPage(2);
  assert(pluginhttp::pageUrls.size() == 2);
  assert(pluginhttp::pageUrls[0] == "https://example/a%20b/+?key=a%20b%2F%2B&offset=0&limit=9");
  assert(pluginhttp::pageUrls[1] == "https://example/a%20b/+?key=a%20b%2F%2B&offset=8&limit=9");

  PluginCatalogActivity::Item item{"a+/1", "A / Café", "", "http://x/y?z=2"};
  pluginhttp::RequestSpec aliases;
  assert(idOnly.buildRequest(aliases, "/{title}?id={id_enc}&title={title_enc}&url={url_enc}", "{title}",
                             {{"Title", "{title}"}}, &item));
  assert(aliases.url == "/A%20/%20Caf%C3%A9?id=a%2B%2F1&title=A%20%2F%20Caf%C3%A9&url=http%3A%2F%2Fx%2Fy%3Fz%3D2");
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
      assert(out.url == escaped);
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
    unit.write_text("\n".join([stub, *limits, policy, pick, variables, requests, loader, cases]))
    subprocess.run([sys.argv[1], "-std=c++20", "-DXP_SD_FONT_CATALOG", "-I", str(ROOT / "lib/Memory"), "-I", sys.argv[2], "-I", str(ROOT / "src"), "-I", str(ROOT / "rust/crates/xp-ffi/include"), str(unit), str(ROOT / "test/cmake/xp_ffi_host.cpp"), sys.argv[3], "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

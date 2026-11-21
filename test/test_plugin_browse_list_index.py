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

stub = r"""
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <memory>
#include <string_view>
#include "util/FirmwareIdentity.h"
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
std::string body;
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

struct PluginCatalogActivity {
  struct Manifest {
    struct BrowseList {
      std::string title, url, body, id, noticeTitle, noticeMessage, noticeConfirm, noticeCancel;
    };
    std::string browseListsUrl = "index";
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
  std::string substituted(const std::string& url, std::nullptr_t) { return url; }
  std::string token = "old";
  pluginhttp::Headers substitutedHeaders(pluginhttp::Headers headers, std::nullptr_t) const {
    for (auto& [key, value] : headers)
      if (value == "{token}") value = token;
    return headers;
  }
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

  PluginCatalogActivity combined;
  combined.manifest.browseLists.push_back({"Fixed", "fixed"});
  combined.manifest.fixedListCount = 1;
  pluginhttp::body = "invalid JSON";
  assert(!combined.loadBrowseListIndex(status));
  assert(combined.manifest.browseLists.size() == 1);
  assert(combined.manifest.browseLists.front().title == "Fixed");
  pluginhttp::body = R"({"lists":[{"title":"Dynamic","url":"dynamic"}]})";
  assert(combined.loadBrowseListIndex(status));
  assert(combined.manifest.browseLists.size() == 2);
  assert(combined.manifest.browseLists.front().title == "Fixed");
  assert(combined.manifest.browseLists.back().title == "Dynamic");

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
    unit.write_text("\n".join([stub, *limits, policy, loader, cases]))
    subprocess.run([sys.argv[1], "-std=c++20", "-I", sys.argv[2], "-I", str(ROOT / "src"), str(unit), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

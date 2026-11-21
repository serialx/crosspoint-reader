"""Exercise the production list-index loader with a stubbed HTTP response."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/plugins/PluginCatalogActivity.cpp").read_text()
limits = re.findall(r"^constexpr size_t MAX_BROWSE_LIST(?:_INDEX_RESPONSE|S) = .*;$", source, re.M)
start = source.index("bool PluginCatalogActivity::loadBrowseListIndex()")
loader = source[start:source.index("\n}", start) + 2]

stub = r"""
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

struct String : std::string {
  using std::string::operator=;
  inline static int live = 0;
  String() { ++live; }
  ~String() { --live; }
};
#define LOG_ERR(...) ((void)0)

namespace pluginhttp {
std::string body;
int request(int*, const std::string&, const char*, const char*, std::initializer_list<int>,
            String& response, size_t maxSize) {
  assert(body.size() <= maxSize);
  response = body;
  return 200;
}
}

struct PluginCatalogActivity {
  struct Manifest {
    struct BrowseList {
      std::string title, url, body, noticeTitle, noticeMessage, noticeConfirm, noticeCancel;
    };
    std::string browseListsUrl = "index";
    std::vector<BrowseList> browseLists;
  } manifest;
  struct ResourceLock {
    explicit ResourceLock(PluginCatalogActivity&) { assert(String::live == 0); }
  };
  std::unique_ptr<int> session;
  std::string substituted(const std::string& url, std::nullptr_t) { return url; }
  bool loadBrowseListIndex();
};
"""

cases = r"""
int main() {
  PluginCatalogActivity empty;
  pluginhttp::body = R"({"lists":[{})";
  for (int i = 1; i < 4000; ++i) pluginhttp::body += ",{}";
  pluginhttp::body += "]}";
  assert(!empty.loadBrowseListIndex());
  assert(empty.manifest.browseLists.empty());
  assert(empty.manifest.browseLists.capacity() <= 64);

  PluginCatalogActivity valid;
  pluginhttp::body = R"({"lists":[{})";
  for (int i = 0; i < 75; ++i) {
    pluginhttp::body += R"(,{"title":"List )" + std::to_string(i) + R"(","url":"page"})";
  }
  pluginhttp::body += "]}";
  assert(valid.loadBrowseListIndex());
  assert(valid.manifest.browseLists.size() == 64);
  assert(valid.manifest.browseLists.capacity() <= 64);
  for (int i = 0; i < 64; ++i) {
    assert(valid.manifest.browseLists.at(i).title == "List " + std::to_string(i));
    assert(valid.manifest.browseLists.at(i).url == "page");
  }
}
"""

with tempfile.TemporaryDirectory() as directory:
    unit = Path(directory) / "browse_list_index.cpp"
    executable = unit.with_suffix("")
    unit.write_text("\n".join([stub, *limits, loader, cases]))
    subprocess.run([sys.argv[1], "-std=c++20", "-I", sys.argv[2], str(unit), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

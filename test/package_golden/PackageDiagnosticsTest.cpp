#include <Logging.h>
#include <PackageParser.h>
#include <ZipFile.h>
#include <xp_ffi.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <string>

#include "Epub/BookMetadataCache.h"

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();

static void expect(const std::string& text) {
  assert(std::count(packageLogs.begin(), packageLogs.end(), text) == 1);
  packageLogs.clear();
}
static bool feed(PackageParser& parser, const std::string& text) {
  return parser.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::filesystem::path root = argv[1];
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  packageCaptureLogs = true;
  for (const auto kind : {XP_PKG_CONTAINER, XP_PKG_OPF_PASS, XP_PKG_NAV_PASS, XP_PKG_NCX_PASS}) {
    const char* tag = kind == XP_PKG_CONTAINER  ? "CTR"
                      : kind == XP_PKG_OPF_PASS ? "COF"
                      : kind == XP_PKG_NAV_PASS ? "NAV"
                                                : "TOC";
    {
      PackageParser parser(kind, "", 4, "");
      xp_test_fail_allocation_after(0);
      assert(!parser.setup());
      expect(std::string(kind == XP_PKG_CONTAINER ? "ERR|" : "DBG|") + tag + "|Couldn't allocate memory for parser");
    }
    xp_test_fail_allocation_after(SIZE_MAX);
    {
      PackageParser parser(kind, "", 4, "");
      assert(parser.setup());
      xp_test_fail_allocation_after(0);
      assert(!feed(parser, "<a/>"));
      expect(std::string(kind == XP_PKG_OPF_PASS ? "ERR|" : "DBG|") + tag +
             (kind == XP_PKG_CONTAINER ? "|Couldn't allocate buffer" : "|Couldn't allocate memory for buffer"));
    }
    xp_test_fail_allocation_after(SIZE_MAX);
    {
      PackageParser parser(kind, "", 7, "");
      assert(parser.setup());
      assert(!feed(parser, "<a></b>"));
      expect(std::string(kind == XP_PKG_CONTAINER ? "ERR|" : "DBG|") + tag +
             (kind == XP_PKG_CONTAINER ? "|Parse error: mismatched tag" : "|Parse error at line 1: mismatched tag"));
    }
    assert(xp_test_live_allocations() == 0);
  }
  {
    std::string xml = "<package><metadata>";
    for (int i = 0; i < 9; ++i)
      xml += "<meta property='belongs-to-collection' id='c" + std::to_string(i) + "'>Collection</meta>";
    xml += "<meta property='collection-type' refines='#other'>series</meta></metadata></package>";
    PackageParser parser(XP_PKG_OPF_PASS, "", xml.size(), "");
    assert(parser.setup());
    assert(feed(parser, xml));
    expect("DBG|COF|Ignoring collection metadata beyond 8 entries");
  }
  {
    BookMetadataCache cache((root / "absent").string());
    assert(cache.beginWrite());
    const std::string xml = "<package><manifest/><spine/><guide/></package>";
    PackageParser parser(XP_PKG_OPF_PASS, "", xml.size(), (root / "absent").string(), &cache);
    assert(parser.setup());
    assert(feed(parser, xml));
    assert(std::count(
               packageLogs.begin(), packageLogs.end(),
               "ERR|COF|Couldn't open temp items file for writing. This is probably going to be a fatal error.") == 1);
    assert(std::count(
               packageLogs.begin(), packageLogs.end(),
               "ERR|COF|Couldn't open temp items file for reading. This is probably going to be a fatal error.") == 2);
    packageLogs.clear();
  }
  {
    BookMetadataCache cache(root.string());
    assert(cache.beginWrite());
    assert(cache.beginContentOpfPass());
    cache.createSpineEntry("missing.xhtml");
    HalFileTest::failNextWrite = true;
    assert(!cache.endContentOpfPass());
    expect("ERR|BMC|Failed writing spine tmp file");
  }
  {
    BookMetadataCache cache(root.string());
    assert(cache.beginWrite());
    assert(cache.beginContentOpfPass());
    cache.createSpineEntry("missing.xhtml");
    assert(cache.endContentOpfPass());
    assert(cache.beginTocPass());
    assert(cache.createTocEntry("Title", "missing.xhtml", "", 1));
    HalFileTest::failNextWrite = true;
    assert(!cache.endTocPass());
    expect("ERR|BMC|Failed writing toc tmp file");
  }
  {
    BookMetadataCache cache(root.string());
    assert(cache.beginWrite());
    assert(cache.beginContentOpfPass());
    cache.createSpineEntry("missing.xhtml");
    assert(cache.endContentOpfPass());
    assert(cache.beginTocPass());
    assert(cache.endTocPass());
    assert(cache.endWrite());
    ZipFile::failOpen = true;
    assert(!cache.buildBookBin("test.epub", {}));
    expect("ERR|BMC|Could not open EPUB zip for size calculations");
    ZipFile::failOpen = false;
    assert(cache.buildBookBin("test.epub", {}));
    expect("ERR|BMC|Warning: Could not get size for spine item: missing.xhtml");
    HalFileTest::failNextWrite = true;
    assert(!cache.buildBookBin("test.epub", {}));
    expect("ERR|BMC|Failed writing book.bin, removing truncated file");
  }
  {
    BookMetadataCache cache(root.string());
    assert(cache.beginWrite());
    assert(cache.beginContentOpfPass());
    for (int i = 0; i < 405; ++i) cache.createSpineEntry("short");
    assert(cache.endContentOpfPass());
    xp_test_fail_allocation_after(0);
    assert(cache.beginTocPass());
    expect("ERR|BMC|Failed to allocate spine href index; scanning 405 spine items per TOC entry");
    xp_test_fail_allocation_after(SIZE_MAX);
    assert(cache.endTocPass());
  }
  assert(xp_test_live_allocations() == 0);
  std::filesystem::remove_all(root);
}

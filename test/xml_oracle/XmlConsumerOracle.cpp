#include <PackageParser.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "Epub/BookMetadataCache.h"

static std::string read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static bool feed(Print& parser, const std::string& input, size_t chunk) {
  for (size_t pos = 0; pos < input.size(); pos += chunk) {
    const auto count = std::min(chunk, input.size() - pos);
    if (parser.write(reinterpret_cast<const uint8_t*>(input.data() + pos), count) != count) return false;
  }
  return true;
}

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  const std::filesystem::path root = argv[1];
  const std::string cachePath = argv[2];
  const size_t chunk = std::strtoull(argv[3], nullptr, 10);
  if (chunk == 0 || std::filesystem::exists(cachePath)) return 2;
  std::filesystem::create_directories(cachePath);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file())
      zipEntrySizes[entry.path().lexically_relative(root).generic_string()] = entry.file_size();
  }
  const std::string container = read(root / "META-INF/container.xml");
  PackageParser cp(XP_PKG_CONTAINER, "", container.size(), "");
  if (!cp.setup() || !feed(cp, container, chunk) || cp.value(0).empty()) return 3;
  const std::string opf = read(root / cp.value(0));
  const auto slash = cp.value(0).rfind('/');
  const std::string base = slash == std::string::npos ? "" : cp.value(0).substr(0, slash + 1);
  BookMetadataCache cache(cachePath);
  if (!cache.beginWrite() || !cache.beginContentOpfPass()) return 4;
  PackageParser op(XP_PKG_OPF_PASS, base, opf.size(), cachePath, &cache);
  if (!op.setup() || !feed(op, opf, chunk) || !cache.endContentOpfPass() || !cache.beginTocPass()) return 5;
  const auto items = std::filesystem::path(cachePath) / ".items.bin";
  std::ofstream(std::filesystem::path(cachePath) / "items.bin", std::ios::binary) << read(items);
  if (!op.value(XP_PKG_NCX).empty() && (std::string(argv[4]) == "ncx" || op.value(XP_PKG_NAV).empty())) {
    const std::string ncx = read(root / op.value(XP_PKG_NCX));
    PackageParser toc(XP_PKG_NCX_PASS, base, ncx.size(), "", &cache);
    if (!toc.setup() || !feed(toc, ncx, chunk)) return 6;
  } else if (!op.value(XP_PKG_NAV).empty()) {
    const std::string nav = read(root / op.value(XP_PKG_NAV));
    const std::string navBase = op.value(XP_PKG_NAV).substr(0, op.value(XP_PKG_NAV).find_last_of('/') + 1);
    PackageParser toc(XP_PKG_NAV_PASS, navBase, nav.size(), "", &cache);
    if (!toc.setup() || !feed(toc, nav, chunk)) return 6;
  }
  if (!cache.endTocPass() || !cache.endWrite()) return 7;
  if (!cache.buildBookBin("oracle.epub",
                          {utf8ComposeNfc(op.value(XP_PKG_TITLE)), utf8ComposeNfc(op.value(XP_PKG_AUTHOR)),
                           op.value(XP_PKG_LANGUAGE), op.value(XP_PKG_COVER), op.value(XP_PKG_TEXT)}))
    return 8;
  const auto encryptionPath = root / "META-INF/encryption.xml";
  bool protectedBook = false;
  if (std::filesystem::exists(encryptionPath)) {
    PackageParser probe(XP_PKG_ENCRYPTION, "", 0, "");
    if (!probe.setup() || !feed(probe, read(encryptionPath), chunk) || !probe.finish()) return 9;
    protectedBook = probe.needsProtectionCheck();
  }
  std::ofstream summary(std::filesystem::path(cachePath) / "consumer.txt");
  summary << cp.value(0) << '\n'
          << op.value(XP_PKG_NCX) << '\n'
          << op.value(XP_PKG_NAV) << '\n'
          << protectedBook << '\n';
  return 0;
}

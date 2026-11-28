#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"
#include "LibraryText.h"

using namespace library;
namespace {
void bytes(const void* p, size_t n) {
  if (n) std::fwrite(p, 1, n, stdout);
}
void number(uint64_t n) {
  for (unsigned i = 0; i < 8; ++i) std::putchar((n >> (8 * i)) & 255);
}
void string(const std::string& s) {
  number(s.size());
  bytes(s.data(), s.size());
}
void state(bool ok, const BuildStats& s) {
  number(ok);
  number(s.books);
  number(s.folders);
  number(s.duplicatesDropped);
  number(s.unreadableSkipped);
  number(s.unchanged);
  number(s.added);
  number(s.renamed);
  number(s.removed);
  number(s.enriched);
  number(s.parsed);
  number(s.metadataReused);
  number(s.indexReplaced);
  number(s.ranksDegraded);
  number(s.dedupDegraded);
  number(fake::files.size());
  for (const auto& [path, node] : fake::files) {
    string(path);
    number(node->directory);
    number(node->time);
    number(node->bytes.size());
    bytes(node->bytes.data(), node->bytes.size());
  }
}
void populate() {
  fake::reset();
  bookMetadata.clear();
  const char* titles[] = {"Été", "E\xcc\x81te\xcc\x81", "한글", "中文", "L’Énéide", "123", "Zebra", "alpha"};
  const char* authors[] = {"Austen, Jane", "Jane Austen", "Lu Xun_", "Lu, Xun", "George Sand [Sand, George]",
                           "Unknown",      "Søren",       "Ø"};
  for (unsigned i = 0; i < 8; i++) {
    const std::string path = "/Books/" + std::to_string(i / 3) + "/" + std::to_string(i) + ".epub";
    fake::add(path, std::string(i + 1, 'x'), 8 - i);
    bookMetadata[path] = {titles[i], authors[i], i != 7};
  }
  fake::add("/notes.md", "notes", 0);
  fake::add("/.hidden/book.epub");
  fake::add("/empty.epub", "");
  fake::duplicateDirectoryEntry("/Books/0/0.epub");
}
void readIndex() {
  LibraryIndexFile index;
  number(index.open(libraryIndexPath()));
  number(static_cast<uint8_t>(index.validity()));
  number(index.ioFailed());
  if (!index.isOpen()) return;
  for (uint16_t row = 0; row <= index.bookCount(); row++) {
    for (unsigned order = 0; order < 6; order++) number(index.ordinalForRow(static_cast<SortOrder>(order), row));
    ClixRecord r{};
    number(index.readRecord(row, r));
    bytes(&r, sizeof(r));
    std::string s;
    uint64_t hash = 0;
    number(index.readPathHash(r, hash));
    number(hash);
    number(index.readName(r, s));
    string(s);
    number(index.readAuthor(r, s));
    string(s);
    number(index.readTitle(r, s));
    string(s);
    number(index.readSourceAuthor(r, s));
    string(s);
    number(index.readPath(r, s));
    string(s);
  }
}
}  // namespace
int main(int argc, char** argv) {
  BuildStats s;
  if (argc == 2 && std::string(argv[1]) == "--recovery") {
    for (unsigned variant = 0; variant < 4; ++variant) {
      for (unsigned fault = 0; fault < 5; ++fault) {
        for (int after = 0; after < 8; ++after) {
          populate();
          if (!buildLibraryIndex("/", s, true)) return 4;
          const auto valid = fake::files[libraryIndexPath()]->bytes;
          fake::add("/.crosspoint/library.bak", "");
          fake::files["/.crosspoint/library.bak"]->bytes = valid;
          if (variant == 0) fake::files.erase(libraryIndexPath());
          if (variant >= 2) fake::files[libraryIndexPath()]->bytes[0] = 0;
          if (variant == 3) fake::files["/.crosspoint/library.bak"]->bytes[0] = 0;
          fake::add("/new.epub", "added");
          if (fault == 0) fake::failRename = after;
          if (fault == 1) fake::failRemove = after;
          if (fault == 2) fake::failRead = after;
          if (fault == 3) fake::failOpen = after;
          if (fault == 4) fake::failMkdir = after;
          state(buildLibraryIndex("/", s, true), s);
          number(fake::failureTriggered);
        }
      }
    }
    return std::ferror(stdout) ? 1 : 0;
  }
  if (argc == 2) {
    fake::reset();
    bookMetadata.clear();
    std::ifstream input(argv[1]);
    if (!input) return 2;
    std::string line;
    const auto unhex = [](const std::string& value) {
      std::string result;
      for (size_t i = 0; i + 1 < value.size(); i += 2)
        result.push_back(static_cast<char>(std::stoul(value.substr(i, 2), nullptr, 16)));
      return result;
    };
    while (std::getline(input, line)) {
      std::istringstream fields(line);
      std::string path, title, author;
      uint32_t size, time;
      if (!(fields >> path >> size >> time >> title >> author)) return 3;
      path = unhex(path);
      fake::add(path, "book", time);
      fake::files[path]->reportedSize = size;
      bookMetadata[path] = {title == "-" ? "" : unhex(title), author == "-" ? "" : unhex(author), true};
    }
    state(buildLibraryIndex("/", s, true), s);
    readIndex();
    state(buildLibraryIndex("/", s, true), s);
    readIndex();
    return std::ferror(stdout) ? 1 : 0;
  }

  populate();
  state(buildLibraryIndex("/", s, true), s);
  readIndex();
  const auto valid = fake::files[libraryIndexPath()]->bytes;
  state(buildLibraryIndex("/", s, true), s);
  fake::extraDirectoryEntries.clear();
  Storage.rename("/Books/0/0.epub", "/Books/renamed.epub");
  fake::files.erase("/Books/0/1.epub");
  fake::add("/new.txt", "new", 99);
  state(buildLibraryIndex("/", s, true), s);
  readIndex();
  for (unsigned kind = 0; kind < 4; kind++) {
    for (int fail = 0; fail < 80; fail++) {
      populate();
      buildLibraryIndex("/", s, true);
      fake::add("/new.epub", "added", 99);
      if (kind == 0) fake::failRead = fail;
      if (kind == 1) fake::failWrite = fail;
      if (kind == 2) fake::failAlloc = fail;
      if (kind == 3) fake::failRename = fail;
      state(buildLibraryIndex("/", s, true), s);
      number(fake::failureTriggered);
    }
  }
  for (size_t n = 0; n <= valid.size(); n++) {
    fake::reset();
    fake::add(libraryIndexPath(), "");
    fake::files[libraryIndexPath()]->bytes.assign(valid.begin(), valid.begin() + n);
    readIndex();
  }
  for (size_t i = 0; i < valid.size(); i++) {
    fake::reset();
    fake::add(libraryIndexPath(), "");
    fake::files[libraryIndexPath()]->bytes = valid;
    fake::files[libraryIndexPath()]->bytes[i] ^= 255;
    readIndex();
  }
  for (uint32_t seed = 1; seed < 4096; seed++) {
    uint32_t rng = seed;
    std::string s;
    for (unsigned n = 0; n < seed % 64; n++) {
      rng = rng * 1664525 + 1013904223;
      s += static_cast<char>(rng >> 24);
    }
    string(fold(s));
    string(cleanPersonName(s));
    string(authorKey(s));
    string(surnameKey(s));
    number(matchesQuery(s, "a b"));
    number(foldedGroupInitial(s));
  }
  return std::ferror(stdout) ? 1 : 0;
}

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fixture {
struct Node {
  bool directory;
  std::string contents;
};
static std::map<std::string, Node> files;
static std::map<std::string, std::string> clippings;
static std::set<std::pair<std::string, std::string>> failedRenames;
static std::set<std::string> failedRenameSources;
static bool clippingFailure;
static unsigned cases;

// Directory aliases retain their raw spelling in the call trace.
std::string storageKey(std::string path) {
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  return path;
}
bool contains(const std::string& path) { return files.count(storageKey(path)) != 0; }
void put(const std::string& path, bool directory, const std::string& contents = "") {
  files[storageKey(path)] = {directory, contents};
}
bool canRename(const std::string& from, const std::string& to) { return contains(from) && !contains(to); }
void renameNodes(const std::string& from, const std::string& to) {
  const auto source = storageKey(from);
  const auto destination = storageKey(to);
  assert(canRename(source, destination));
  std::vector<std::pair<std::string, Node>> moved;
  for (auto it = files.begin(); it != files.end();) {
    if (it->first == source || it->first.compare(0, source.size() + 1, source + "/") == 0) {
      moved.emplace_back(destination + it->first.substr(source.size()), it->second);
      it = files.erase(it);
    } else {
      ++it;
    }
  }
  files.insert(moved.begin(), moved.end());
}
void log(const char* level, const char* origin, const char* format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  const int count = std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  assert(count >= 0 && static_cast<size_t>(count) < sizeof(message));
  std::cout << "log " << level << ' ' << origin << ' ' << std::quoted(message) << '\n';
}
}  // namespace fixture

#define LOG_INF(tag, ...) fixture::log("info", tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) fixture::log("error", tag, __VA_ARGS__)

struct StorageFake {
  bool mkdir(const char* raw) {
    const std::string path(raw);
    const auto found = fixture::files.find(fixture::storageKey(path));
    const bool ok = found == fixture::files.end() || found->second.directory;
    if (ok) fixture::put(path, true);
    std::cout << "mkdir " << std::quoted(path) << ' ' << ok << '\n';
    return ok;
  }
  bool exists(const char* raw) const {
    const std::string path(raw);
    const bool present = fixture::contains(path);
    std::cout << "exists " << std::quoted(path) << ' ' << present << '\n';
    return present;
  }
  bool rename(const char* rawFrom, const char* rawTo) {
    const std::string from(rawFrom), to(rawTo);
    const bool ok = fixture::failedRenames.count({from, to}) == 0 && fixture::failedRenameSources.count(from) == 0 &&
                    fixture::canRename(from, to);
    std::cout << "rename " << std::quoted(from) << ' ' << std::quoted(to) << ' ' << ok << '\n';
    if (ok) fixture::renameNodes(from, to);
    return ok;
  }
} Storage;

namespace ClippingStore {
bool moveBook(const std::string& from, const std::string& to) {
  const bool ok = !fixture::clippingFailure && fixture::canRename(from, to);
  std::cout << "clipping-move " << std::quoted(from) << ' ' << std::quoted(to) << ' ' << ok << '\n';
  if (ok) {
    fixture::renameNodes(from, to);
    const auto clip = fixture::clippings.find(from);
    if (clip != fixture::clippings.end()) {
      fixture::clippings[to] = clip->second;
      fixture::clippings.erase(clip);
    }
  }
  return ok;
}
}  // namespace ClippingStore

struct RecentBooksFake {
  std::string path, cache;
  unsigned updates = 0;
  void updatePath(const std::string& from, const std::string& to, const std::string& oldCache,
                  const std::string& newCache) {
    std::cout << "recent-update " << std::quoted(from) << ' ' << std::quoted(to) << ' ' << std::quoted(oldCache) << ' '
              << std::quoted(newCache) << '\n';
    assert(path == from && cache == oldCache);
    path = to;
    cache = newCache;
    ++updates;
  }
} RECENT_BOOKS;

struct AppStateFake {
  std::string openEpubPath, savedOpenPath;
  unsigned saves = 0;
  void saveToFile() {
    std::cout << "app-save " << std::quoted(openEpubPath) << '\n';
    savedOpenPath = openEpubPath;
    ++saves;
  }
} APP_STATE;

// The only production-expression substitution: portable 32-bit FNV-1a over path bytes.
uint32_t fixturePathHash(const std::string& path) {
  uint32_t hash = 2166136261u;
  for (unsigned char byte : path) hash = (hash ^ byte) * 16777619u;
  std::cout << "path-hash " << std::quoted(path) << ' ' << hash << '\n';
  return hash;
}

// PINNED_READ_FOLDER_DEFINITIONS

namespace fixture {
void reset() {
  files.clear();
  clippings.clear();
  failedRenames.clear();
  failedRenameSources.clear();
  clippingFailure = false;
  RECENT_BOOKS = {};
  APP_STATE = {};
}
void finish() {
  for (const auto& entry : files) {
    std::cout << "fs " << (entry.second.directory ? 'd' : 'f') << ' ' << std::quoted(entry.first);
    if (!entry.second.directory) std::cout << ' ' << std::quoted(entry.second.contents);
    std::cout << '\n';
  }
  for (const auto& entry : clippings)
    std::cout << "clip " << std::quoted(entry.first) << ' ' << std::quoted(entry.second) << '\n';
  std::cout << "recent " << std::quoted(RECENT_BOOKS.path) << ' ' << std::quoted(RECENT_BOOKS.cache) << ' '
            << RECENT_BOOKS.updates << '\n';
  std::cout << "open " << std::quoted(APP_STATE.openEpubPath) << ' ' << std::quoted(APP_STATE.savedOpenPath) << ' '
            << APP_STATE.saves << "\nend\n";
}
void classifications() {
  for (const char* path :
       {"", "book.epub", "/", "/read", "/read/", "/read/book.epub", "/Read/", "/Read/book.epub", "/readish/",
        "/readish/book.epub", "read/book.epub", "/nested/book.epub", "/nested/deeper/book.part.epub", "/nested/.hidden",
        ".hidden", "/read/.hidden", "/read//book.epub"}) {
    reset();
    std::cout << "case " << cases++ << " classify " << std::quoted(path) << '\n';
    std::cout << "result " << isInReadFolder(path) << '\n';
    finish();
  }
}
void destination(const std::string& source, unsigned collisions, const std::string& first,
                 const std::string& numberedBase, const std::string& extension) {
  reset();
  if (collisions) {
    put("/read", true);
    put(first, false, "occupied");
    for (unsigned suffix = 2; suffix <= collisions; ++suffix)
      put(numberedBase + " (" + std::to_string(suffix) + ")" + extension, false, "occupied");
  }
  std::cout << "case " << cases++ << " destination " << std::quoted(source) << ' ' << collisions << '\n';
  const auto result = buildReadFolderDestination(source);
  std::cout << "result " << std::quoted(result) << '\n';
  finish();
}
void destinations() {
  for (unsigned collisions : {0u, 1u, 2u, 97u, 98u, 99u}) {
    destination("/in/book.epub", collisions, "/read/book.epub", "/read/book", ".epub");
    destination("/in/book", collisions, "/read/book", "/read/book", "");
  }
  for (const char* path :
       {"", "book.epub", "/", "/read", "/read/", "/nested/deeper/book.part.epub", "/nested/.hidden", "/nested/book."})
    destination(path, 0, "", "", "");
  destination("/nested/deeper/book.part.epub", 1, "/read/book.part.epub", "/read/book.part", ".epub");
  destination("/nested/.hidden", 1, "/read/.hidden", "/read/", ".hidden");
  destination("/nested/book.", 1, "/read/book.", "/read/book", ".");
}

enum class Cache { EmptyPath, Absent, Present, RenameFails };
enum class Failure { None, Clipping, Key, Rights };
const std::string source = "/books/book.epub";
const std::string destinationPath = "/read/book.epub";
const std::string cachePath = "/cache/old";

void move(unsigned sidecars, Cache cache, bool openMatches, Failure failure, unsigned rollbackFailures) {
  reset();
  put(source, false, "book");
  put("/read", true);
  clippings[source] = "annotation";
  if (sidecars & 1) put(source + ".key", false, "key");
  if (sidecars & 2) put(source + ".rights", false, "rights");
  const std::string oldCache = cache == Cache::EmptyPath ? "" : cachePath;
  if (cache == Cache::Present || cache == Cache::RenameFails) {
    put(oldCache, true);
    put(oldCache + "/progress.bin", false, "progress");
  }
  if (cache == Cache::RenameFails) failedRenameSources.insert(oldCache);
  clippingFailure = failure == Failure::Clipping;
  if (failure == Failure::Key) failedRenames.emplace(source + ".key", destinationPath + ".key");
  if (failure == Failure::Rights) failedRenames.emplace(source + ".rights", destinationPath + ".rights");
  if (rollbackFailures & 1) failedRenames.emplace(destinationPath, source);
  if (rollbackFailures & 2) failedRenames.emplace(destinationPath + ".key", source + ".key");
  RECENT_BOOKS.path = source;
  RECENT_BOOKS.cache = oldCache;
  APP_STATE.openEpubPath = openMatches ? source : "/other.epub";
  APP_STATE.savedOpenPath = APP_STATE.openEpubPath;
  std::cout << "case " << cases++ << " move " << sidecars << ' ' << static_cast<int>(cache) << ' ' << openMatches << ' '
            << static_cast<int>(failure) << ' ' << rollbackFailures << '\n';
  moveFinishedBookToReadFolder(source, destinationPath, oldCache);
  finish();
}
void moves() {
  for (unsigned sidecars = 0; sidecars < 4; ++sidecars)
    for (Cache cache : {Cache::EmptyPath, Cache::Absent, Cache::Present, Cache::RenameFails})
      for (bool openMatches : {false, true}) move(sidecars, cache, openMatches, Failure::None, 0);
  for (unsigned sidecars = 0; sidecars < 4; ++sidecars)
    for (bool openMatches : {false, true}) move(sidecars, Cache::Present, openMatches, Failure::Clipping, 0);
  for (unsigned sidecars : {1u, 3u})
    for (unsigned rollback : {0u, 1u})
      for (bool openMatches : {false, true}) move(sidecars, Cache::Present, openMatches, Failure::Key, rollback);
  for (unsigned rollback : {0u, 1u})
    for (bool openMatches : {false, true}) move(2, Cache::Present, openMatches, Failure::Rights, rollback);
  for (unsigned rollback : {0u, 1u, 2u, 3u})
    for (bool openMatches : {false, true}) move(3, Cache::Present, openMatches, Failure::Rights, rollback);
}
}  // namespace fixture

int main() {
  fixture::classifications();
  fixture::destinations();
  fixture::moves();
  assert(fixture::cases == 100);
  std::cout << "cases " << fixture::cases << '\n';
}

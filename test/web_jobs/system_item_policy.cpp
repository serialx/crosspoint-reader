#include <xp_ffi.h>

#include "Oracle.h"
#include "network/WebDAVHandler.h"

#include <cassert>
#include <iostream>

int main() {
  const std::pair<std::string, bool> cases[] = {
      {"System Volume Information", true}, {"XTCache", true}, {"ordinary", false},
      {"XTCacheArchive", false}, {"xtcache", false}, {"System Volume Information ", false},
      {".hidden", false}, {".", false}, {"", false}};
  for (const auto& [name, system] : cases) {
    assert(xp_system_item_name(reinterpret_cast<const uint8_t*>(name.data()), name.size()) == system);
    const std::string path = "/Books/" + name + "/book.epub";
    const bool dot = !name.empty() && name[0] == '.';
    assert(bool(xp_library_book(reinterpret_cast<const uint8_t*>(path.data()), path.size())) ==
           !(system || (dot && name != ".")));
    oracle::reset();
    oracle::files[path] = {'a'};
    WebServer server;
    server.path = path;
    WebDAVHandler handler;
    handler.handle(server, HTTP_GET, server.path);
    const bool forbidden =
        std::find(oracle::trace.begin(), oracle::trace.end(), "send 403 text/plain Forbidden") != oracle::trace.end();
    assert(forbidden == (system || dot));
  }
  assert(!xp_system_item_name(nullptr, 0));
  std::cout << "System item policy: " << std::size(cases) << " explicit names, shared predicate/DAV/library agree\n";
}

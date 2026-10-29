#include <xp_ffi.h>

#include "Oracle.h"
#include "network/WebDAVHandler.h"

#ifndef SYSTEM_ITEMS_HEADER
#define SYSTEM_ITEMS_HEADER "../../lib/FsHelpers/HiddenItems.h"
#endif
#include SYSTEM_ITEMS_HEADER

#include <cassert>
#include <iostream>

int main() {
  std::vector<std::string> names(std::begin(FsHelpers::HIDDEN_ITEMS), std::end(FsHelpers::HIDDEN_ITEMS));
  names.insert(names.end(), {"ordinary", "XTCacheArchive", "xtcache", ".hidden", ".", ""});
  for (const auto& name : names) {
    const bool system = std::any_of(std::begin(FsHelpers::HIDDEN_ITEMS), std::end(FsHelpers::HIDDEN_ITEMS),
                                    [&](const char* item) { return name == item; });
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
  std::cout << "System item policy: " << names.size() << " names, C table/DAV/library agree\n";
}

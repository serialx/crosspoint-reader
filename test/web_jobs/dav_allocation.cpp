#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>

#include "Oracle.h"
#include "network/WebDAVHandler.h"

void* operator new(size_t n) {
  if (allocation_fault::fail()) throw std::bad_alloc();
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }

int main() {
  for (int scenario = 0; scenario < 3; ++scenario) {
    oracle::reset();
    oracle::dirs = {"/"};
    WebServer server;
    server.path = String("/" + std::string(200, 'a') + ".epub");
    server.verb = HTTP_PUT;
    server.headers["Depth"] = String(std::string(100, '1'));
    server.headers["Overwrite"] = String(std::string(100, 'T'));
    const std::string temp = std::string(server.path.c_str()) + ".davtmp";
    WebDAVHandler handler;
    HTTPRaw raw{RAW_START};
    handler.raw(server, server.path, raw);
    if (scenario == 0) oracle::fault = "write";
    uint8_t bytes[] = {1, 2, 3, 4};
    raw = {RAW_WRITE, bytes, 4, 4};
    handler.raw(server, server.path, raw);
    const size_t before = oracle::trace.size();
    allocation_fault::attempts = 0;
    allocation_fault::armed = true;
    raw.status = scenario == 2 ? RAW_ABORTED : RAW_END;
    try {
      handler.raw(server, server.path, raw);
    } catch (const std::bad_alloc&) {
      allocation_fault::armed = false;
      std::cerr << "infallible allocation before upload cleanup, scenario " << scenario << '\n';
      return 1;
    }
    allocation_fault::armed = false;
    const bool closed =
        std::find(oracle::trace.begin() + before, oracle::trace.end(), "close " + temp) != oracle::trace.end();
    assert(closed);
    if (scenario == 0)
      assert(allocation_fault::attempts == 0);
    else
      assert(allocation_fault::attempts > 0);
    handler.handle(server, HTTP_PUT, server.path);
    assert(std::find(oracle::trace.begin(), oracle::trace.end(),
                     "send 500 text/plain Write failed - incomplete upload or disk full") != oracle::trace.end());
    assert(!oracle::files.count(server.path.c_str()));
    assert(!oracle::files.count(temp));
    std::cout << "CASE " << scenario << " closed=1 status=500 files=0 path_allocations=" << allocation_fault::attempts
              << '\n';
    if (scenario == 0) {
      for (auto it = oracle::trace.begin() + before; it != oracle::trace.end(); ++it) std::cout << *it << '\n';
    }
  }
}

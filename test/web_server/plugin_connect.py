"""Exercise the shipping plugin client's connection and header ordering."""
import argparse
from pathlib import Path
import subprocess

from extract import ROOT, function

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--source", type=Path, default=ROOT / "src/util/PluginHttp.cpp")
args = parser.parse_args()
args.build.mkdir(parents=True, exist_ok=True)
source = args.build / "plugin_connect.cpp"
source.write_text(r"""
#include <cassert>
#include <functional>
#include <string>
#include <vector>
namespace pluginhttp { using Headers = std::vector<std::pair<std::string, std::string>>; }
namespace freeink {
struct SecureHttpClient {
  bool ready = false;
  int handshakes = 0;
  int headers = 0;
  void setUserAgent(const char*) {}
  void setInsecure() {}
  void setTimeout(int) {}
  bool begin(const std::string&) { return true; }
  bool connect(const std::function<bool()>& abort) {
    if (abort && abort()) return false;
    if (!ready) ++handshakes;
    return ready = true;
  }
  void addHeader(const std::string&, const std::string&) {
    assert(ready);
    ++headers;
  }
};
}
""" + function(args.source.read_text(), "openClient") + r"""
int main() {
  freeink::SecureHttpClient session, temporary;
  const pluginhttp::Headers headers{{"Cookie", std::string(8192, 'x')}};
  assert(!openClient(&session, temporary, "https://example.test", headers, [] { return true; }));
  assert(session.headers == 0);
  for (int i = 0; i < 2; ++i)
    assert(openClient(&session, temporary, "https://example.test", headers) == &session);
  assert(session.handshakes == 1 && session.headers == 2);
  assert(temporary.handshakes == 0 && temporary.headers == 0);
  assert(openClient(nullptr, temporary, "https://example.test", headers) == &temporary);
  assert(temporary.handshakes == 1 && temporary.headers == 1);
}
""")
binary = args.build / "plugin_connect"
subprocess.run(["c++", "-std=c++20", str(source), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)

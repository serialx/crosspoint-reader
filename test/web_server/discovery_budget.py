#!/usr/bin/env python3
"""Compare the extracted discovery reply with pinned-String budget goldens."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--build', type=Path, required=True)
p.add_argument('--source', type=Path, default=ROOT / 'src/network/CrossPointWebServer.cpp')
p.add_argument('--record', action='store_true')
a = p.parse_args()
s = a.source.read_text()
if 'String message = "crosspoint (on " + hostname' in s:
    start = s.index('String hostname = WiFi.getHostname();')
    end = s.index('udp.endPacket();', start) + len('udp.endPacket();')
    body = s[start:end]
    setup = ''
else:
    start = s.index('          String message =', s.index('.reply ='))
    end = s.index('udp.endPacket();', start) + len('udp.endPacket();')
    body = s[start:end]
    setup = '''String hostname = WiFi.getHostname();
    XpWebText prefix = span("crosspoint (on ");
    XpWebText host = hostname.isEmpty() ? span("crosspoint") : XpWebText{reinterpret_cast<const uint8_t*>(hostname.c_str()), hostname.length()};
    const std::string tail = ");" + std::to_string(wsPort);
    XpWebText suffix = span(tail.c_str());'''
a.build.mkdir(parents=True, exist_ok=True)
program = r'''
#include "StringBudget.h"
struct XpWebText { const uint8_t* data; size_t len; };
XpWebText span(const char* s) { return {reinterpret_cast<const uint8_t*>(s), std::char_traits<char>::length(s)}; }
struct WebFileBridge { static String string(XpWebText s) { return String(reinterpret_cast<const char*>(s.data), s.len); } };
struct { const char* name; const char* getHostname() { return name; } } WiFi;
struct {
  std::string result;
  size_t liveAtSend = 0;
  int remoteIP() { return 0; } int remotePort() { return 0; }
  void beginPacket(int,int) {} void endPacket() {}
  void write(const uint8_t* p, size_t n) { liveAtSend = String::live; result.assign(n ? reinterpret_cast<const char*>(p) : "", n); }
} udp;
void reply(unsigned short wsPort) {
''' + setup + '\n' + body + r'''
}
int main() {
  for (const auto& name : {std::string("CrossPoint-Reader-AABBCCDDEEFF"), std::string("CrossPoint-Reader-AABBCCDDEEF"), std::string(), std::string(100, 'h')}) {
    for (unsigned short port : {81, 65535}) {
      for (size_t budget = 0; budget <= 512; ++budget) {
        for (size_t failure = 0; failure <= 8; ++failure) {
          String::budget = budget; String::failAt = failure == 8 ? SIZE_MAX : failure;
          String::peak = String::attempts = 0; String::requests.clear();
          WiFi.name = name.c_str();
          reply(port);
          if (String::live) return 2;
          std::cout << name.size() << ' ' << port << ' ' << budget << ' ' << failure << ':' << udp.result << ':' << String::peak << ':' << udp.liveAtSend << ':';
          for (size_t request : String::requests) std::cout << request << ',';
          std::cout << '\n';
        }
      }
    }
  }
}
'''
(a.build / 'discovery.cpp').write_text(program)
subprocess.run(['c++','-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                '-I'+str(ROOT/'test/web_server'),str(a.build/'discovery.cpp'),'-o',str(a.build/'discovery')],check=True)
actual = subprocess.check_output([str(a.build/'discovery')])
(a.build/'actual.txt').write_bytes(actual)
golden = ROOT/'test/web_server/discovery-budget.txt.gz'
if a.record:
    if setup: raise SystemExit('record requires original C++ source')
    golden.write_bytes(gzip.compress(actual, mtime=0))
expected = gzip.decompress(golden.read_bytes())
if actual != expected:
    differences = [(old.decode(), new.decode()) for old,new in zip(expected.splitlines(), actual.splitlines()) if old != new]
    (a.build/'differences.json').write_text(json.dumps(differences, indent=2)+'\n')
    raise SystemExit(f'discovery budget mismatch: {len(differences)} cases; first {differences[:3]}')
print(f'discovery budget: {len(actual.splitlines())} matching cases (513 live-byte budgets, 8 injected failures plus no failure)')

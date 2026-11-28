"""Compile the production physical IP leaf with a deterministic SDK boundary."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/network/WifiSelectionPhysical.cpp').read_text()
start = source.index('[](char out[16])')
opening = source.index('{', start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[opening:end]
program = r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
struct IPAddress {
  std::array<unsigned char, 4> value;
  unsigned char operator[](unsigned i) const { return value[i]; }
};
struct Radio {
  IPAddress value;
  IPAddress localIP() const { return value; }
} WiFi;
int main() {
  auto leaf = [](char out[16]) BODY;
  for (const auto value : {std::array<unsigned char,4>{255,255,255,255}, {0,0,0,0}}) {
    WiFi.value.value = value;
    char guarded[18]; std::memset(guarded, 'X', sizeof guarded);
    leaf(guarded+1);
    const char* expected=value[0]?"255.255.255.255":"0.0.0.0";
    assert(std::memchr(guarded+1,0,16));
    assert(std::strcmp(guarded+1,expected)==0);
    assert(guarded[0]=='X'&&guarded[17]=='X');
  }
}
'''.replace('BODY', body)
with tempfile.TemporaryDirectory(prefix='wifi-ip-leaf-') as temporary:
    path = Path(temporary)
    cpp = path / 'test.cpp'
    cpp.write_text(program)
    binary = path / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Wi-Fi IP leaf: maximum/zero address, NUL and guard bytes PASS')

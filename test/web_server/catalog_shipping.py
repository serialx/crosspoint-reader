"""Run unchanged native web bridges with the shipping Rust SD catalog owner."""
import argparse
from pathlib import Path
import subprocess
from extract import ROOT, generate

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--arduinojson', type=Path, required=True)
parser.add_argument('--html-root', type=Path, required=True)
parser.add_argument('--archive', type=Path, required=True)
args = parser.parse_args()
args.build.mkdir(parents=True, exist_ok=True)
source = generate((ROOT / 'src/network/CrossPointWebServer.cpp').read_text(),
                  (ROOT / 'src/network/CrossPointWebServer.h').read_text())
assert source.count('#include "Cases.h"') == 1
source = source.replace('#include "Cases.h"', '#include "CatalogCases.h"')
cpp = args.build / 'server.cpp'
cpp.write_text(source)
binary = args.build / 'catalog'
subprocess.run(['c++', '-std=c++20', '-DXP_SD_FONT_CATALOG', '-funsigned-char', '-UNDEBUG',
                '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                *['-I' + str(path) for path in (ROOT / 'test/web_server/stubs', ROOT / 'test/web_server',
                                              args.html_root, ROOT, args.arduinojson,
                                              ROOT / 'rust/crates/xp-ffi/include')],
                str(cpp), str(ROOT / 'test/cmake/xp_ffi_host.cpp'), str(args.archive),
                '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)

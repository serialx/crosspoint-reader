#!/usr/bin/env python3
"""Compile the public C++ facades against scripted platform leaves, compare frozen traces."""
import argparse
import gzip
import pathlib
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
p = argparse.ArgumentParser()
p.add_argument('--build', type=pathlib.Path, required=True)
p.add_argument('--source', type=pathlib.Path, default=ROOT)
p.add_argument('--archive', type=pathlib.Path)
p.add_argument('--record', action='store_true')
p.add_argument('--dav', action='store_true')
p.add_argument('--lcp', action='store_true')
p.add_argument('--no-enroll-key', action='store_true')
a = p.parse_args()
a.build.mkdir(parents=True, exist_ok=True)
# The SDK resume engine stays real; replace only its HTTP leaf in the build copy.
sdk = ROOT / 'freeink-sdk/libs/network/SecureNet/include/ResumableFetch.h'
(a.build / 'ResumableFetch.h').write_bytes(sdk.read_bytes())
sources = [HERE / 'oracle.cpp'] + [a.source / 'src/network' / (s + '.cpp') for s in ('FirmwareBoardTag', 'FirmwareFlasher', 'HttpDownloader')]
if a.dav:
    sources = [HERE / 'dav.cpp', a.source / 'src/network/WebDAVHandler.cpp']
if a.lcp:
    sources = [HERE/'lcp.cpp', a.source/'src/network/LcpDevice.cpp', ROOT/'freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp']
cmd = ['c++', '-std=c++20', '-O2', '-DCROSSPOINT_VERSION="oracle"', '-I'+str(HERE/'stubs'), '-I'+str(a.build), '-I'+str(a.source/'src'), '-I'+str(ROOT/'rust/crates/xp-ffi/include')]
if a.lcp:
    cmd += ['-I'+str(ROOT/'.pio/libdeps/default/ArduinoJson/src'), '-I'+str(ROOT/'freeink-sdk/libs/network/JsonSax/include')]
    if not a.no_enroll_key:
        cmd += ['-DFREEINK_LCP_RELEASE_KEY="release"']
if a.archive:
    sources.append(ROOT/'test/cmake/xp_ffi_host.cpp')
cmd += [str(s) for s in sources]
if a.archive:
    cmd.append(str(a.archive))
exe = a.build/'oracle'
subprocess.run(cmd+['-o', str(exe)], check=True)
result = subprocess.check_output([str(exe)]) if not a.lcp else b''.join(b'CASE '+str(i).encode()+b'\n'+subprocess.check_output([str(exe),str(i)]) for i in range(18))
(a.build/'actual.txt').write_bytes(result)
golden = HERE/('lcp-disabled-golden.txt.gz' if a.no_enroll_key else 'lcp-golden.txt.gz' if a.lcp else 'dav-golden.txt.gz' if a.dav else 'golden.txt.gz')
if a.record:
    golden.write_bytes(gzip.compress(result, mtime=0))
else:
    expected = gzip.decompress(golden.read_bytes())
    if expected != result:
        import difflib
        print(''.join(difflib.unified_diff(expected.decode().splitlines(True), result.decode().splitlines(True)))[:12000])
        raise SystemExit('web jobs oracle differs')
print('web jobs oracle:', result.count(b'CASE '), 'cases passed')

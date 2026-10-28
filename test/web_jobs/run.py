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
a = p.parse_args()
a.build.mkdir(parents=True, exist_ok=True)
# The SDK resume engine stays real; replace only its HTTP leaf in the build copy.
sdk = ROOT / 'freeink-sdk/libs/network/SecureNet/include/ResumableFetch.h'
(a.build / 'ResumableFetch.h').write_bytes(sdk.read_bytes())
sources = [HERE / 'oracle.cpp'] + [a.source / 'src/network' / (s + '.cpp') for s in ('FirmwareBoardTag', 'FirmwareFlasher', 'HttpDownloader')]
cmd = ['c++', '-std=c++20', '-O2', '-DCROSSPOINT_VERSION="oracle"', '-I'+str(HERE/'stubs'), '-I'+str(a.build), '-I'+str(a.source/'src'), '-I'+str(ROOT/'rust/crates/xp-ffi/include')]
if a.archive:
    sources.append(ROOT/'test/cmake/xp_ffi_host.cpp')
cmd += [str(s) for s in sources]
if a.archive:
    cmd.append(str(a.archive))
exe = a.build/'oracle'
subprocess.run(cmd+['-o', str(exe)], check=True)
result = subprocess.check_output([str(exe)])
(a.build/'actual.txt').write_bytes(result)
golden = HERE/'golden.txt.gz'
if a.record:
    golden.write_bytes(gzip.compress(result, mtime=0))
else:
    expected = gzip.decompress(golden.read_bytes())
    if expected != result:
        import difflib
        print(''.join(difflib.unified_diff(expected.decode().splitlines(True), result.decode().splitlines(True)))[:12000])
        raise SystemExit('web jobs oracle differs')
print('web jobs oracle:', result.count(b'CASE '), 'cases passed')

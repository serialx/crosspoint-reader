#!/usr/bin/env python3
"""Exercise the shipping Rust gate and retained native gate against the same strict physical fakes."""
import argparse, hashlib, json, os, pathlib, shlex, subprocess, sys
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'test/reader_lifecycle'))
from test_popup_resource import native_leaf
from generate import generate
p=argparse.ArgumentParser();p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--archive',type=pathlib.Path);args=p.parse_args()
args.build.mkdir(parents=True,exist_ok=True)
archive=args.archive
if archive is None:
    target=args.build/'rust'
    subprocess.run(['cargo','build','--offline','--release','--manifest-path',str(ROOT/'rust/Cargo.toml'),'-p','xp-ffi','--features','chapter-test'],env=dict(os.environ,CARGO_TARGET_DIR=str(target),CARGO_BUILD_JOBS='2'),check=True)
    archive=target/'release/libxp_ffi.a'
source=(ROOT/'src/activities/reader/RustEpubResources.cpp').read_text()
fields=('file_open_read','file_read','file_close','file_seek','file_size','file_position','protection_close','protection_encrypted','protection_size')
leaves='\n'.join('leaves.'+name+' = '+native_leaf(source,name)+';' for name in fields)
(args.build/'Oracle.cpp').write_text((HERE/'Oracle.cpp').read_text().replace('// ACTUAL_LEAVES',leaves))
(args.build/'ContentProtection.cpp').write_bytes((ROOT/'lib/Epub/ContentProtection.cpp').read_bytes())
sdk=ROOT/'freeink-sdk/libs/book/ContentProtection'
includes=[HERE/'stubs',ROOT/'lib/Epub',ROOT/'lib/DeviceSecret',ROOT/'lib/ZipFile',ROOT/'lib/Memory',ROOT/'lib/miniz/src',sdk/'include',ROOT/'test/book_io/epub-stubs',ROOT/'test/chapter_html_slim_parser/stubs',ROOT/'rust/crates/xp-ffi/include',ROOT/'test/font_cache/legacy',ROOT/'test/sdfont_legacy/abi',ROOT/'test/sdfont_legacy',ROOT/'test/vector_font/legacy',ROOT/'test/reader_lifecycle']
sources=[args.build/'Oracle.cpp',args.build/'ContentProtection.cpp',ROOT/'lib/Epub/BookKey.cpp',ROOT/'lib/DeviceSecret/DeviceSecret.cpp',ROOT/'lib/ZipFile/ZipPlatform.cpp',ROOT/'lib/miniz/src/InflateStream.cpp',ROOT/'lib/miniz/src/MinizCompat.cpp',ROOT/'lib/Memory/BuildScratch.cpp',sdk/'src/ProtectedBook.cpp',sdk/'src/Zip.cpp',ROOT/'test/cmake/xp_ffi_host.cpp']
crypto=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True))
flags=['-O1','-g','-UNDEBUG','-DCONTENT_EXTERNAL_MINIZ=1','-DXP_FFI_HOST_CSS_FAULT_INJECTION=1','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}' for x in includes]]
subprocess.run(['cc',*flags,'-c',str(ROOT/'lib/miniz/src/miniz_impl.c'),'-o',str(args.build/'miniz.o')],check=True)
subprocess.run(['c++','-std=c++20',*flags,*[str(x) for x in sources],str(args.build/'miniz.o'),str(archive),*crypto,'-o',str(args.build/'oracle')],check=True)
identities={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [*sources,archive,ROOT/'rust/crates/xp-ffi/src/app/epub/book_owner/protection.rs']}
(args.build/'source-identities.json').write_text(json.dumps(identities,indent=2)+'\n')
generate(args.build/'fixtures')
subprocess.run([str(args.build/'oracle'),str(args.build/'fixtures')],check=True)

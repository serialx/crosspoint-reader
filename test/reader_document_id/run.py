#!/usr/bin/env python3
"""Compare shipping reader sampling to the retained native KOReader implementation."""
import argparse, io, os, pathlib, subprocess, zipfile
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
p = argparse.ArgumentParser()
p.add_argument('--build', type=pathlib.Path, required=True)
p.add_argument('--archive', type=pathlib.Path)
a = p.parse_args()
a.build.mkdir(parents=True, exist_ok=True)
archive = a.archive
if archive is None:
    target = a.build / 'rust'
    subprocess.run(['cargo', 'build', '--offline', '--release', '--manifest-path', str(ROOT / 'rust/Cargo.toml'), '-p', 'xp-ffi', '--features', 'chapter-test,sd-font-catalog'], env=dict(os.environ, CARGO_TARGET_DIR=str(target), CARGO_BUILD_JOBS='2'), check=True)
    archive = target / 'release/libxp_ffi.a'
includes = [a.build, HERE, HERE / 'stubs', ROOT / 'src/activities/reader', ROOT / 'rust/crates/xp-ffi/include', ROOT / 'lib/KOReaderSync', ROOT / 'lib/CrossPointPosition', ROOT / 'test/crosspoint_settings/stubs']
# Generate a real plain EPUB; no binary fixture is checked in.
book = io.BytesIO()
with zipfile.ZipFile(book, 'w') as z:
    z.writestr('mimetype', 'application/epub+zip')
    z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="package.opf"/></rootfiles></container>')
    z.writestr('package.opf', '<package><metadata><title>Test</title></metadata><manifest><item id="c" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
    z.writestr('chapter.xhtml', '<p>Still here</p>')
(a.build / 'BookData.inc').write_text(','.join(map(str, book.getvalue())))
sources = [HERE / 'Oracle.cpp', ROOT / 'src/activities/reader/RustReaderDocument.cpp', ROOT / 'lib/KOReaderSync/KOReaderDocumentId.cpp', ROOT / 'test/cmake/xp_ffi_host.cpp']
subprocess.run(['c++', '-std=c++20', '-O1', '-g', '-UNDEBUG', '-DXP_SD_FONT_CATALOG', '-DXP_FFI_HOST_CSS_FAULT_INJECTION=1', '-Wno-deprecated-declarations', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', *[f'-I{x}' for x in includes], *map(str, sources), str(archive), '-o', str(a.build / 'oracle')], check=True)
subprocess.run([str(a.build / 'oracle')], check=True)

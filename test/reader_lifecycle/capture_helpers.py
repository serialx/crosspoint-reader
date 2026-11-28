"""Explicitly capture NEW synthetic helper references; never rewrite existing goldens.

The C++ thin-wrapper executable checks source-derived forwarding/value assertions
and emits no golden. Main owns candidate integration, CTest and deliberate mutants.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PARENT = 'b88726243ea49662e161eaab935ee11f303f9197'
MAX_ARTIFACT_BYTES = 200_000
EPUB = 'src/activities/reader/EpubReaderActivity.cpp'
BASE = 'src/activities/reader/ReaderActivity.cpp'
BASE_HEADER = 'src/activities/reader/ReaderActivity.h'
UTILS = 'src/activities/reader/EpubReaderUtils.h'
BOOKMARK = 'src/util/BookmarkUtil.cpp'
PAGE_LINK = 'lib/Epub/Epub/PageLink.h'
FOOTNOTE = 'lib/Epub/Epub/FootnoteEntry.h'
BOOK_ABI = 'rust/crates/xp-ffi/include/xp_book.h'
BOOK_LIMITS = 'rust/crates/xp-ffi/include/xp_book_limits.h'
BOOK_POLICY = 'rust/crates/xp-ffi/src/book_policy.rs'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def definition(source, signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def replace_once(text, marker, value):
    if text.count(marker) != 1:
        raise ValueError(f'Expected one extraction marker: {marker}')
    return text.replace(marker, value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True, help='New output directory; must not exist')
    parser.add_argument('--rust-archive', type=Path, default=os.environ.get('XP_TEST_RUST_ARCHIVE'),
                        help='Already-built host xp-ffi archive; no implicit Cargo build')
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    destination = args.capture.expanduser().absolute()
    if destination.exists():
        parser.error(f'Capture directory already exists: {destination}')
    if args.rust_archive is None or not Path(args.rust_archive).expanduser().is_file():
        parser.error('Supply an existing --rust-archive or XP_TEST_RUST_ARCHIVE')
    archive = Path(args.rust_archive).expanduser().resolve()
    paths = [EPUB, BASE, BASE_HEADER, UTILS, BOOKMARK, PAGE_LINK, FOOTNOTE, BOOK_ABI, BOOK_LIMITS, BOOK_POLICY]
    sources = {path: subprocess.check_output(['git', 'show', f'{PARENT}:{path}'], cwd=ROOT) for path in paths}
    text = {path: data.decode('utf-8') for path, data in sources.items()}
    # PageLink delegates to the real Rust symbol. Require its source body to match the capture parent.
    link_signature = 'pub unsafe extern "C" fn xp_book_link_contains('
    if definition((ROOT / BOOK_POLICY).read_text(), link_signature) != definition(text[BOOK_POLICY], link_signature):
        parser.error('Current xp_book_link_contains differs from the frozen parent; supply matching linked inputs')
    template = (HERE / 'helper_reference.cpp').read_text()
    overlay_names = ['discardOverlayPage', 'pushOverlayRefresh', 'settleOverlayRefresh', 'paintOverlayPopup']
    template = replace_once(template, '// EXTRACT_OVERLAY', '\n\n'.join(
        definition(text[EPUB], f'void EpubReaderActivity::{name}(') for name in overlay_names))
    template = replace_once(template, '// EXTRACT_BASE_HELPERS', '\n\n'.join(
        definition(text[BASE], f'void ReaderActivity::{name}(')
        for name in ['disableFastInitialRefresh', 'applyInitialOrientation']))
    template = replace_once(template, '// EXTRACT_LINK', definition(text[UTILS], 'inline const PageLink* linkAtPoint('))
    sanitizer = definition(text[BOOKMARK], 'std::string BookmarkUtil::sanitizeBookmarkSummary(')
    template = replace_once(template, '// EXTRACT_SANITIZER', sanitizer.replace('BookmarkUtil::', '', 1))
    wrappers = (HERE / 'helper_wrapper_reference.cpp').read_text()
    wrappers = replace_once(wrappers, '// EXTRACT_INLINE_WRAPPERS', '\n'.join(
        definition(text[BASE_HEADER], signature)
        for signature in ['int getProgressPercent()', 'virtual bool inputOverPage()']))
    wrappers = replace_once(wrappers, '// EXTRACT_BACK_WRAPPER',
                            definition(text[BASE], 'bool ReaderActivity::handleBackNavigation('))
    wrappers = replace_once(wrappers, '// EXTRACT_CLAMP', definition(text[EPUB], 'int clampPercent('))
    sys.path.insert(0, str(ROOT / 'test'))
    from rust_host import rust_link_flags
    flags = rust_link_flags(str(archive))
    compiler_version = subprocess.check_output([args.cxx, '--version'], text=True)
    artifacts = {}
    commands = []
    with tempfile.TemporaryDirectory(prefix='reader-helper-reference-') as directory:
        work = Path(directory)
        for path in [PAGE_LINK, FOOTNOTE, BOOK_ABI, BOOK_LIMITS]:
            (work / Path(path).name).write_bytes(sources[path])
        cpp, wrapper_cpp = work / 'reference.cpp', work / 'wrappers.cpp'
        cpp.write_text(template)
        wrapper_cpp.write_text(wrappers)
        executable, wrapper_executable = work / 'reference', work / 'wrappers'
        compile_command = [args.cxx, '-std=c++20', '-UNDEBUG', '-O0', '-I', str(work), str(cpp),
                           *flags, '-o', str(executable)]
        wrapper_command = [args.cxx, '-std=c++20', '-UNDEBUG', '-O0', str(wrapper_cpp), '-o', str(wrapper_executable)]
        for command in [compile_command, wrapper_command]:
            subprocess.run(command, check=True, cwd=ROOT)
            commands.append(command)
        subprocess.run([str(wrapper_executable)], check=True, cwd=ROOT)
        commands.append([str(wrapper_executable)])
        for mode in ['overlay', 'refresh-orientation', 'link-hit', 'sanitize']:
            command = [str(executable), mode]
            data = subprocess.check_output(command, cwd=ROOT)
            if not data or len(data) >= MAX_ARTIFACT_BYTES:
                raise ValueError(f'{mode}: artifact must be nonempty and less than {MAX_ARTIFACT_BYTES} bytes')
            artifacts[mode + '.reference.txt'] = data
            commands.append(command)
    manifest = {
        'parent': PARENT,
        'capture_command': shlex.join([sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]]),
        'capture_argv': [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]],
        'capture_cwd': str(Path.cwd()),
        'reproduce_command': shlex.join([sys.executable, str(Path(__file__).resolve()), '--capture',
                                        str(destination), '--rust-archive', str(archive), '--cxx', args.cxx]),
        'source_sha256': {path: digest(data) for path, data in sources.items()},
        'fixture_sha256': {name: digest((HERE / name).read_bytes()) for name in
                           ['helper_reference.cpp', 'helper_wrapper_reference.cpp', 'capture_helpers.py']},
        'link_inputs_sha256': {'archive': digest(archive.read_bytes()),
                              'test/rust_host.py': digest((ROOT / 'test/rust_host.py').read_bytes()),
                              'test/cmake/xp_ffi_host.cpp': digest((ROOT / 'test/cmake/xp_ffi_host.cpp').read_bytes())},
        'rust_archive': str(archive),
        'link_contract': 'Caller supplies an already-built archive matching the recorded xp_book_link_contains body; '
                         'the archive hash records the actual linked binary, not a proof of build provenance.',
        'compiler_version': compiler_version,
        'platform': {'system': platform.system(), 'machine': platform.machine(), 'python': platform.python_version()},
        'commands': commands,
        'artifacts': {name: {'bytes': len(data), 'sha256': digest(data)} for name, data in artifacts.items()},
        'thin_wrappers': {'expected': 'Source-derived forwarding/value assertions; no frozen artifact',
                          'methods': ['handleBackNavigation', 'inputOverPage', 'getProgressPercent', 'clampPercent'],
                          'result': 'passed'},
        'synthetic_only': True,
        'sanitizer_limit': 'Baseline std::isspace(char) receives negative values on signed-char hosts for UTF-8. '
                           'Those observations are platform-specific and are not claimed to be defined portable C++ '
                           'behavior. The reference preserves the baseline code unchanged and reports bytes in hex.',
        'next_steps': 'Main adds production candidate comparison, CTest registration and at least one deliberate '
                      'mutant per new test, including each thin wrapper. No existing golden was read or written.',
    }
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode('utf-8')
    if len(manifest_data) >= MAX_ARTIFACT_BYTES:
        raise ValueError('Manifest exceeds the artifact limit')
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in {**artifacts, 'manifest.json': manifest_data}.items():
        with (destination / name).open('xb') as stream:
            stream.write(data)
    print(f'Captured {len(artifacts)} new synthetic references and wrapper assertions in {destination}')


if __name__ == '__main__':
    main()

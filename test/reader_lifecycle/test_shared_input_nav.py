"""Verify shared input/navigation and their actual EPUB callers.

Original complete traces remain immutable; no freeze path.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
NAV_SOURCE = Path('crates/xp-ffi/src/app/epub/overlay_input.rs')
NAV_SCENARIOS = (6, 17, 18, 20, 21, 29, 30)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def once(source, old, new):
    assert source.count(old) == 1, ('mechanical anchor', old, source.count(old))
    return source.replace(old, new, 1)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


INPUT_LOGGER = r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static bool inputProbeOnly = false;
static std::vector<std::string> inputProbeLines;
static int inputFixturePrintf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  int count;
  if (inputProbeOnly) {
    char buffer[128];
    count = std::vsnprintf(buffer, sizeof(buffer), format, args);
    assert(count >= 0 && size_t(count) < sizeof(buffer));
    inputProbeLines.emplace_back(buffer, size_t(count));
  } else {
    count = std::vprintf(format, args);
  }
  va_end(args);
  return count;
}
'''


def input_fixture(root):
    oracle = root / 'test/reader_lifecycle'
    adapter = (oracle / 'input_session_rust.inc').read_text()
    adapter = once(adapter, 'static bool currentRtl = false;',
                   'static bool currentRtl = false;\nstatic const char* currentLanguage = nullptr;')
    adapter = once(adapter,
                   '    const char* language = currentRtl ? "ar" : "en";\n'
                   '    assert(language[2] == \'\\0\');\n'
                   '    const uint8_t consumed = sink(context, {reinterpret_cast<const uint8_t*>(language), 2});',
                   '    const char* language = currentLanguage ? currentLanguage : (currentRtl ? "ar" : "en");\n'
                   '    const uint8_t consumed = sink(context, {reinterpret_cast<const uint8_t*>(language), std::strlen(language)});')
    adapter = once(adapter, 'assert(languageCalls == 13500);', 'assert(languageCalls == 13518);')
    extension = (HERE / 'input_owner_extensions.inc').read_text()
    header = (oracle / 'projection_session_fixture.h').read_text()
    if 'struct XpEpubTestChapterDisplay' in header:
        # Main may have installed the approved declaration by execution time.
        begin = extension.index('// Main-approved test-only observation;')
        end = extension.index('static unsigned ownerLanguageCases', begin)
        extension = extension[:begin] + extension[end:]
    source = (oracle / 'input_oracle.cpp').read_text()
    source = source.replace('std::printf(', 'inputFixturePrintf(')
    source = once(source, '#include "input_session_rust.inc"', adapter + '\n' + extension)
    source = once(source,
                  'auto r = rtl ? ReaderUtils::detectTouchPageTurn(renderer, input, rtl) : ownerTouch(input);',
                  'auto r = ownerBookTouch(input, rtl);')
    source = once(source, 'const bool open = ReaderUtils::isTouchMenuGesture(renderer, input);',
                  'const bool open = ownerMenu(input);')
    source = once(source, 'tag, ReaderUtils::isRtlBookLanguage(tag));', 'tag, ownerLanguageTag(tag));')
    source = once(source,
                  '      ChapterPosition p{page, total};\n'
                  '      inputFixturePrintf("chapter %d %d %d %d %08x\\n", page, total, p.displayPage(), p.hasTotal(),\n'
                  '                  std::bit_cast<uint32_t>(p.chapterFraction()));',
                  '      const auto p = ownerChapterDisplay(page, total);\n'
                  '      inputFixturePrintf("chapter %d %d %d %d %08x\\n", page, total, p.display_page, int(p.has_total),\n'
                  '                  std::bit_cast<uint32_t>(p.fraction));')
    source = once(source, '  destroyInputOwner();',
                  '  assert(ownerLanguageCases == 18 && ownerChapterCases == 12);\n  destroyInputOwner();')
    return INPUT_LOGGER + source


def selected_nav(mode, scenario, variant):
    return mode in (2, 3) and scenario in NAV_SCENARIOS and (scenario != 6 or variant % 2 == 1)


def nav_fixture(overlay):
    source = overlay.transformed_fixture()
    return once(source, '        eventIndex = 0;',
                '        id = unsigned((mode - 1) * 36 * 12 + scenario * 12 + variant);\n'
                '        if (!((mode == 2 || mode == 3) &&\n'
                '              (scenario == 6 || scenario == 17 || scenario == 18 || scenario == 20 ||\n'
                '               scenario == 21 || scenario == 29 || scenario == 30) &&\n'
                '              (scenario != 6 || variant % 2 == 1))) continue;\n'
                '        eventIndex = 0;')


def nav_reference(raw):
    lines = raw.splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if line.startswith(b'case=')]
    assert len(starts) == 1728 and starts[0] == 0
    starts.append(len(lines))
    output, counts = [], {}
    for begin, end in zip(starts, starts[1:]):
        fields = dict(value.split('=') for value in lines[begin].decode().split())
        mode, scenario, variant = (int(fields[key]) for key in ('mode', 'scenario', 'variant'))
        assert int(fields['case']) == (mode - 1) * 432 + scenario * 12 + variant
        if selected_nav(mode, scenario, variant):
            block = lines[begin:end]
            # These blocks never activate TOC/font-picker rows, so no lock projection applies.
            assert not any(b' toc-create ' in line or b' discard ' in line or b' font-picker-create ' in line
                           for line in block)
            output.extend(block)
            counts[str(scenario)] = counts.get(str(scenario), 0) + 1
    assert counts == {'6': 12, '17': 24, '18': 24, '20': 24, '21': 24, '29': 24, '30': 24}
    return b''.join(output), counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('shared_input', type=Path)
    parser.add_argument('shared_nav', type=Path)
    parser.add_argument('--mode', choices=('input', 'nav', 'all'), default='all')
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_SHARED_INPUT_NAV_EVIDENCE_DIR'),
                        help='new directory only; mode subdirectory is appended')
    args = parser.parse_args()
    root = args.repo.resolve()
    oracle = root / 'test/reader_lifecycle'
    sys.dont_write_bytecode = True
    sys.path[:0] = [str(oracle), str(root / 'test')]
    overlay = module('shared_nav_overlay', oracle / 'test_overlay_input.py')
    input_adapter = module('shared_input_adapter', oracle / 'test_input_session.py')
    from rust_host import rust_link_flags
    frozen = {name: (oracle / name).read_bytes() for name in ('input.golden.gz', 'nav.golden.gz', overlay.GOLDEN)}
    input_expected = gzip.decompress(frozen['input.golden.gz'])
    shared_nav_expected = gzip.decompress(frozen['nav.golden.gz'])
    assert digest(frozen['input.golden.gz']) == input_adapter.GZIP_SHA
    assert digest(input_expected) == input_adapter.RAW_SHA
    assert digest(frozen[overlay.GOLDEN]) == overlay.GZIP_SHA
    overlay_raw = gzip.decompress(frozen[overlay.GOLDEN])
    assert digest(overlay_raw) == overlay.RAW_SHA
    assert len(shared_nav_expected.splitlines()) == 7680
    nav_expected, nav_counts = nav_reference(overlay_raw)
    fixtures = {'input': input_fixture(root), 'nav': nav_fixture(overlay)}
    paths = list((root / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs'))
    paths += [oracle / name for name in ('input_session_rust.inc', 'input_oracle.cpp', 'test_input_session.py',
              'extract_input.py', 'extract.py', 'nav_oracle.cpp', 'test_overlay_input.py', 'overlay_oracle.cpp',
              'overlay_input_session_rust.inc', 'overlay_input_session_fixture.h', 'overlay_actions_session_fixture.h',
              'projection_session_fixture.h')]
    paths += [root / name for name in ('src/MappedInputManager.h', 'src/util/ButtonNavigator.cpp',
              'src/activities/reader/ReaderUtils.h', 'src/activities/reader/ChapterPosition.h',
              'rust/crates/xp-ffi/include/xp_epub_session.h')]
    hashes = {str(path.relative_to(root)): digest(path.read_bytes()) for path in paths}
    try:
        with tempfile.TemporaryDirectory(prefix='shared-input-nav-owner-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir / args.mode if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=False)
            commands, records = [], []
            def run(name, command, **kwargs):
                commands.append(list(map(str, command)))
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                assert result.returncode == 0, (name, result.returncode, result.stderr.decode(errors='replace'))
                return result.stdout
            if args.mode in ('input', 'all'):
                run('existing-input-driver', [sys.executable, '-B', str(oracle / 'test_input_session.py'),
                                             str(args.shared_input.resolve()), '--repo', str(root)])
            if args.mode in ('nav', 'all'):
                shared = run('shared-nav', [str(args.shared_nav.resolve())], timeout=120)
                assert shared == shared_nav_expected, 'original shared 7680-case ListNav trace changed'
            extracted = work / 'input_methods.inc'
            run('extract-input', [sys.executable, '-B', str(oracle / 'extract_input.py'), str(root), str(extracted)])
            flags = rust_link_flags()
            def compile_fixture(kind, name, archive=None):
                source = work / (kind + '.cpp')
                source.write_text(fixtures[kind])
                (evidence / (kind + '.cpp')).write_text(fixtures[kind])
                binary = work / name
                selected = flags if archive is None else rust_link_flags(archive=str(archive))
                run(name + '.compile', ['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                        '-fno-sanitize-recover=all',
                                        *(['-DXP_INPUT_SESSION=1', '-DXP_INPUT_EPUB=1'] if kind == 'input' else []),
                                        '-I', str(work), '-I', str(oracle),
                                        '-I', str(root / 'src/activities/reader'), str(source), *selected, '-o', str(binary)])
                return binary
            for kind, expected in (('input', input_expected), ('nav', nav_expected)):
                if args.mode not in (kind, 'all'):
                    continue
                actual = run(kind + '-owner', [str(compile_fixture(kind, kind + '-owner'))], timeout=120)
                assert actual == expected, (kind, 'entire unfiltered original trace/subset mismatch', digest(actual), digest(expected))
                records.append({'kind': kind, 'actual_sha256': digest(actual), 'expected_sha256': digest(expected),
                                'lines': len(actual.splitlines())})
            if args.mode in ('nav', 'all'):
                original = (root / 'rust' / NAV_SOURCE).read_text()
                before = 'if self.model(|state| state.panel_cursor) {'
                begin = original.index('    fn page_overlay_rows(')
                end = original.index('    pub(super) fn handle_overlay_input(', begin)
                scope = original[begin:end]
                changed = original[:begin] + once(scope, before, 'if false {') + original[end:]
                copied = work / 'source'
                shutil.copytree(root / 'rust', copied / 'rust', ignore=shutil.ignore_patterns('target', '__pycache__'))
                for name in ('scripts', 'lib', 'src'):
                    (copied / name).symlink_to(root / name, target_is_directory=True)
                (copied / 'rust' / NAV_SOURCE).write_text(changed)
                target = work / 'target'
                run('nav-mutant.cargo', ['cargo', 'build', '--manifest-path', str(copied / 'rust/Cargo.toml'),
                                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                        '--features', 'app-test'], cwd=copied / 'rust')
                actual = run('nav-mutant', [str(compile_fixture('nav', 'nav-mutant', target / 'release/libxp_ffi.a'))], timeout=120)
                assert actual != nav_expected, 'real owner cursor-publication mutant survived'
                records.append({'mutant': 'page-overlay-rows-ignore-cursor', 'source': str(NAV_SOURCE),
                                'replacement_count': 1, 'mutant_sha256': digest(changed.encode()),
                                'actual_sha256': digest(actual), 'returncode': 0})
            summary = {'mode': args.mode, 'scope': 'shared input/base matrices retained; owner 18 language and 12 chapter outputs; '
                                'shared 7680 nav rows retained plus 156 historical owner nav cases/204 input calls',
                       'nav_scenario_counts': nav_counts, 'source_sha256': hashes, 'records': records,
                       'goldens': {name: {'gzip_sha256': digest(data), 'raw_sha256': digest(gzip.decompress(data))}
                                   for name, data in frozen.items()}, 'commands': commands,
                       'limits': 'Back/gesture remain shared; nav scroll outcomes remain historical physical fake values; '
                                 'no full App A1, asynchronous, widget-owner composition or device claim'}
            (evidence / 'manifest.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
    finally:
        for name, data in frozen.items():
            assert (oracle / name).read_bytes() == data, ('immutable golden changed', name)
        for path, expected in hashes.items():
            assert digest((root / path).read_bytes()) == expected, ('source changed', path)


if __name__ == '__main__':
    main()

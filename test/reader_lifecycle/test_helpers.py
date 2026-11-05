"""Compare production session overlay/base helpers with the new native references.

The counter-write event is an explicitly source-derived final-value projection:
disable_fast_initial_refresh makes one state write with no interleaving physical
calls. The adapter observes the production counter after that method returns and
prints its value; it does not supply the expected zero or add a production hook.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PARENT = 'b88726243ea49662e161eaab935ee11f303f9197'
PRODUCTION_PATH = Path('crates/xp-ffi/src/app/epub/overlay.rs')
MODES = ['overlay', 'refresh-orientation']
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f'Expected exactly one source site: {before}')
    return source.replace(before, after, 1)


def candidate_fixture(reference):
    source = reference.decode('utf-8')
    source = replace_once(source, '#include "PageLink.h"',
                          '#include <cstdlib>\n#include "helper_session_fixture.h"')
    source = replace_once(source, '  void discardOverlayPage();', '''  void* owner = nullptr;
  ~EpubReaderActivity();
  void initialize();
  void observe();
  void discardOverlayPage();''')
    source = replace_once(source, '  void disableFastInitialRefresh();', '''  void* owner = nullptr;
  ~ReaderActivity();
  void initialize();
  void observe();
  void disableFastInitialRefresh();''')
    source = replace_once(source, '// EXTRACT_OVERLAY', '')
    source = replace_once(source, '// EXTRACT_BASE_HELPERS', (HERE / 'helper_session_rust.inc').read_text())
    # This batch retains only the original overlay/base case generators and physical fakes.
    start = source.index('namespace EpubReaderUtils {')
    end = source.index('static void overlayCases()', start)
    source = source[:start] + source[end:]
    source = source[:source.index('static PageLink link(')] + '''int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string mode(argv[1]);
  if (mode == "overlay") overlayCases();
  else if (mode == "refresh-orientation") baseHelperCases();
  else return 2;
  assert(!lockDepth && !modelDepth && !activeEpub && !activeBase);
}
'''
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True, help='Captured helper-reference directory')
    parser.add_argument('--mode', choices=MODES, help='One CTest group; omitted runs both groups')
    args = parser.parse_args()
    modes = [args.mode] if args.mode else MODES
    manifest = json.loads((args.reference / 'manifest.json').read_text())
    assert manifest['parent'] == PARENT
    reference_source = (HERE / 'helper_reference.cpp').read_bytes()
    assert digest(reference_source) == manifest['fixture_sha256']['helper_reference.cpp']
    expected = {}
    for mode in modes:
        name = mode + '.reference.txt'
        data = (args.reference / name).read_bytes()
        assert 0 < len(data) < 200_000
        assert len(data) == manifest['artifacts'][name]['bytes']
        assert digest(data) == manifest['artifacts'][name]['sha256']
        expected[mode] = data
    fixture = candidate_fixture(reference_source)
    production_file = ROOT / 'rust' / PRODUCTION_PATH
    production = production_file.read_text()
    mutants = [
        ('overlay', 'popup-settle', 'self.settle_overlay_refresh(&access);',
         '/* deliberate mutant: omitted pending-refresh settlement */'),
        ('refresh-orientation', 'refresh-counter', 'self.set_refresh_counter(0);',
         'self.set_refresh_counter(1);'),
        ('refresh-orientation', 'orientation-forward', 'settings.assume_init().orientation',
         'settings.assume_init().orientation.wrapping_add(1)'),
    ]
    with tempfile.TemporaryDirectory(prefix='reader-helper-session-') as directory:
        work = Path(directory)
        cpp = work / 'helpers.cpp'
        cpp.write_text(fixture)

        def compile_fixture(name, archive=None):
            executable = work / name
            flags = rust_link_flags(archive=None if archive is None else str(archive))
            subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-fsanitize=address,undefined',
                            '-I', str(HERE), str(cpp), *flags, '-o', str(executable)], check=True)
            return executable

        executable = compile_fixture('helpers')
        for mode in modes:
            actual = subprocess.check_output([str(executable), mode])
            assert actual == expected[mode], f'{mode}: production differs from captured helper reference'
            print(json.dumps({'mode': mode, 'bytes': len(actual), 'sha256': digest(actual),
                              'counter_event_projection': 'observed final production value; no physical interleaving'
                              if mode == 'refresh-orientation' else None}), flush=True)
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        target = work / 'target'
        copied_production = copied_rust / PRODUCTION_PATH
        assert copied_production.read_text() == production
        try:
            for mode, name, before, after in mutants:
                if mode not in modes:
                    continue
                changed = replace_once(production, before, after)
                copied_production.write_text(changed)
                print(json.dumps({'mutant': name, 'mode': mode, 'replacement_count': 1,
                                  'original_sha256': digest(production.encode()),
                                  'source_sha256': digest(changed.encode())}), flush=True)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                mutant = compile_fixture(name, target / 'release/libxp_ffi.a')
                result = subprocess.run([str(mutant), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                assert result.returncode != 0 or result.stdout != expected[mode], f'mutant survived: {name}'
        finally:
            assert production_file.read_text() == production
    print(f'helper session: {", ".join(modes)} reference comparison and production mutants passed')


if __name__ == '__main__':
    main()

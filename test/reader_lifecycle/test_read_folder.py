"""Compare real Rust read-folder helpers with the authorized 100-case native capture."""
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
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

TEST_NAME = 'reader_read_folder'
PARENT = 'b88726243ea49662e161eaab935ee11f303f9197'
REFERENCE_SOURCE_SHA256 = '579eee94c29ce8a633ad395b6e899cde3aa82c51339b1e1cba4994f6883b164f'
REFERENCE_TRACE_SHA256 = '0e2c95f21cd62fed645ac398f990ca467a0eb836ddc93b934684d624caf84426'
PRODUCTION_PATH = Path('crates/xp-ffi/src/app/epub/read_folder.rs')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=HERE / 'read_folder_reference',
                        help='Directory containing the authorized trace.txt and manifest.json.')
    args = parser.parse_args()
    manifest_bytes = (args.reference / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    expected = (args.reference / 'trace.txt').read_bytes()
    template_bytes = (HERE / 'read_folder_reference.cpp').read_bytes()
    capture_driver = (HERE / 'capture_read_folder.py').read_bytes()
    assert manifest['parent'] == PARENT
    assert manifest['source_sha256'] == REFERENCE_SOURCE_SHA256
    assert manifest['case_count'] == 100
    assert digest(expected) == REFERENCE_TRACE_SHA256 == manifest['artifacts']['trace.txt']['sha256']
    assert len(expected) == manifest['artifacts']['trace.txt']['bytes']
    assert digest(template_bytes) == manifest['template_sha256'], 'reference fixture changed after capture'
    assert digest(capture_driver) == manifest['driver_sha256'], 'capture driver changed after capture'
    assert manifest['substitution']['from'] == 'std::hash<std::string>{}(dstPath)'
    assert manifest['substitution']['to'] == 'fixturePathHash(dstPath)'
    assert manifest['substitution']['count'] == 1
    template = template_bytes.decode('utf-8')
    marker = '// PINNED_READ_FOLDER_DEFINITIONS'
    assert template.count(marker) == 1
    fixture = template.replace(marker, (HERE / 'read_folder_session_rust.inc').read_text(), 1)
    production = (ROOT / 'rust' / PRODUCTION_PATH).read_bytes()

    with tempfile.TemporaryDirectory(prefix='reader-read-folder-') as directory:
        work = Path(directory)
        source_path = work / 'candidate.cpp'
        source_path.write_text(fixture)
        baseline_flags = rust_link_flags()

        def run(name, archive=None):
            flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                            '-I', str(HERE), str(source_path), *flags, '-o', str(work / name)], check=True)
            return subprocess.check_output([str(work / name)])

        actual = run('read-folder')
        assert actual == expected, (TEST_NAME, digest(actual), REFERENCE_TRACE_SHA256)
        assert sum(line.startswith(b'case ') for line in actual.splitlines()) == 100
        assert actual.splitlines()[-1] == b'cases 100'
        print(json.dumps({
            'test_name': TEST_NAME,
            'cases': 100,
            'parent': PARENT,
            'reference_manifest_sha256': digest(manifest_bytes),
            'reference_source_sha256': REFERENCE_SOURCE_SHA256,
            'reference_trace_sha256': REFERENCE_TRACE_SHA256,
            'candidate_source_sha256': digest(production),
            'candidate_harness_sha256': digest(fixture.encode('utf-8')),
            'candidate_trace_sha256': digest(actual),
        }), flush=True)

        # Mutate the real destination loop, preserving the captured fake and observations.
        before = b'for suffix in 2..=99 {'
        after = b'for suffix in 2..=98 {'
        assert production.count(before) == 1, 'suffix-limit mutation must match exactly once'
        wrong = production.replace(before, after, 1)
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust,
                        ignore=shutil.ignore_patterns('target', '__pycache__'))
        # Existing build scripts read these inputs and write only into the private target.
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        copied_source = copied_rust / PRODUCTION_PATH
        assert copied_source.read_bytes() == production
        copied_source.write_bytes(wrong)
        target = work / 'target'
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        mutant_trace = run('suffix-limit', target / 'release/libxp_ffi.a')
        assert mutant_trace != actual, 'real Rust suffix-limit mutant escaped the native capture'
        assert (ROOT / 'rust' / PRODUCTION_PATH).read_bytes() == production
        print(json.dumps({
            'test_name': TEST_NAME,
            'mutant': 'read-folder-suffix-limit',
            'replacement_count': 1,
            'original_source_sha256': digest(production),
            'mutant_source_sha256': digest(wrong),
            'mutant_trace_sha256': digest(mutant_trace),
            'reference_trace_sha256': REFERENCE_TRACE_SHA256,
        }), flush=True)
        print(f'{TEST_NAME}: 100 native cases, {len(actual.splitlines())} exact lines; '
              'production suffix-limit mutant rejected')


if __name__ == '__main__':
    main()

"""Compare the real EPUB link and summary helpers with their separate native captures."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

PARENT = 'b88726243ea49662e161eaab935ee11f303f9197'
MODES = {
    'link-hit': {
        'test': 'reader_link_hit_helper',
        'sha256': '8d400afc5ed71b1c6590ff67fe2a84e266c332da35ab29137ad377b004705eee',
        'source': 'crates/xp-ffi/src/app/epub/links.rs',
        'cases': 2067,
        'case_prefix': b'link set=',
        'mutant': 'link-slop-zero',
    },
    'sanitize': {
        'test': 'reader_summary_helper',
        'sha256': 'db42247744f78a61690ea1599a1a85b5f21d2b86a2d82dd82109406b6601ec32',
        'source': 'crates/xp-ffi/src/app/epub/text.rs',
        'cases': 61,
        'case_prefix': b'sanitize ',
        'mutant': 'summary-capacity-71',
    },
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def mutant(source, mode):
    if mode == 'link-hit':
        pattern = rb'(link\.contains\(x\.wrapping_sub\(left\), y\.wrapping_sub\(top\),\s*)6(,\s*28\s*\))'
        assert len(re.findall(pattern, source)) == 1, 'link slop mutation must match one real call'
        changed, count = re.subn(pattern, lambda match: match[1] + b'0' + match[2], source)
        assert count == 1
        return changed
    before = b'if length < output.len() {'
    assert source.count(before) == 1, 'summary capacity mutation must match once'
    return source.replace(before, b'if length < output.len() - 1 {', 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', required=True, choices=MODES)
    parser.add_argument('--reference', type=Path, default=HERE / 'helper_reference')
    args = parser.parse_args()
    mode = MODES[args.mode]
    manifest_bytes = (args.reference / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    artifact = args.mode + '.reference.txt'
    expected = (args.reference / artifact).read_bytes()
    template_bytes = (HERE / 'helper_reference.cpp').read_bytes()
    assert manifest['parent'] == PARENT
    assert digest(expected) == mode['sha256'] == manifest['artifacts'][artifact]['sha256']
    assert len(expected) == manifest['artifacts'][artifact]['bytes']
    assert digest(template_bytes) == manifest['fixture_sha256']['helper_reference.cpp']
    assert digest((HERE / 'capture_helpers.py').read_bytes()) == manifest['fixture_sha256']['capture_helpers.py']

    # Preserve the recorded includes and both generators verbatim. None of the
    # original policy definitions or unrelated overlay/base generators are linked.
    template = template_bytes.decode('utf-8')
    for marker in ('static int lockDepth = 0;', 'static PageLink link(', 'int main('):
        assert template.count(marker) == 1
    includes = template[:template.index('static int lockDepth = 0;')]
    generators = template[template.index('static PageLink link('):template.index('int main(')]
    entry = '''int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string mode(argv[1]);
  if (mode == "link-hit") linkCases();
  else if (mode == "sanitize") sanitizeCases();
  else return 2;
}
'''
    fixture = includes + (HERE / 'link_text_session_rust.inc').read_text() + '\n' + generators + entry
    production_path = Path(mode['source'])
    production = (ROOT / 'rust' / production_path).read_bytes()
    headers = {}
    for path in ('lib/Epub/Epub/PageLink.h', 'lib/Epub/Epub/FootnoteEntry.h'):
        data = (ROOT / path).read_bytes()
        assert digest(data) == manifest['source_sha256'][path], f'physical link fixture changed: {path}'
        headers[Path(path).name] = data

    with tempfile.TemporaryDirectory(prefix='reader-link-text-') as directory:
        work = Path(directory)
        for name, data in headers.items():
            (work / name).write_bytes(data)
        source_path = work / 'candidate.cpp'
        source_path.write_text(fixture)
        baseline_flags = rust_link_flags()

        def run(name, archive=None):
            flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
            subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                            '-I', str(work), '-I', str(HERE), str(source_path), *flags,
                            '-o', str(work / name)], check=True)
            return subprocess.check_output([str(work / name), args.mode])

        actual = run('helper')
        assert actual == expected, (mode['test'], digest(actual), mode['sha256'])
        assert sum(line.startswith(mode['case_prefix']) for line in actual.splitlines()) == mode['cases']
        if args.mode == 'sanitize':
            assert actual.splitlines()[0] == expected.splitlines()[0]
            assert actual.startswith(b'platform char-signed=')
        print(json.dumps({
            'test_name': mode['test'],
            'mode': args.mode,
            'cases': mode['cases'],
            'parent': PARENT,
            'reference_manifest_sha256': digest(manifest_bytes),
            'reference_trace_sha256': mode['sha256'],
            'candidate_trace_sha256': digest(actual),
            'candidate_source': str(production_path),
            'candidate_source_sha256': digest(production),
            'candidate_harness_sha256': digest(fixture.encode('utf-8')),
        }), flush=True)

        wrong = mutant(production, args.mode)
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust,
                        ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        copied_source = copied_rust / production_path
        assert copied_source.read_bytes() == production
        copied_source.write_bytes(wrong)
        target = work / 'target'
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        mutant_trace = run(mode['mutant'], target / 'release/libxp_ffi.a')
        assert mutant_trace != actual, (mode['test'], 'production mutant escaped')
        assert (ROOT / 'rust' / production_path).read_bytes() == production
        print(json.dumps({
            'test_name': mode['test'],
            'mutant': mode['mutant'],
            'replacement_count': 1,
            'original_source_sha256': digest(production),
            'mutant_source_sha256': digest(wrong),
            'mutant_trace_sha256': digest(mutant_trace),
            'reference_trace_sha256': mode['sha256'],
        }), flush=True)
        print(f"{mode['test']}: {mode['cases']} native cases, {len(actual.splitlines())} exact lines; "
              'actual Rust mutant rejected')


if __name__ == '__main__':
    main()

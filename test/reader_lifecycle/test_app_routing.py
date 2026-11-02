"""Exercise shipping Reader launch classification and owned construction through App."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    root = args.repo.resolve()
    here = root / 'test/reader_lifecycle'
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(root / 'test'))
    from rust_host import rust_link_flags
    # -1 means owned EPUB; other values are retained XpAppLaunchKind values.
    paths = [('', 0), ('/a.bmp', 9), ('/a.PNG', 9), ('/a.JpG', 9), ('/a.jpeg', 9),
             ('/a.xtc', 8), ('/a.XtC', 8), ('/a.xtch', 8), ('/a.XTCH', 8),
             ('/a.epub', -1), ('/a.EPUB', -1), ('/a.txt', -1), ('/a.TxT', -1),
             ('/a.md', -1), ('/a.MD', -1), ('/a.unknown', -1), ('/no-extension', -1),
             ('/a.gif', -1), ('/' + 'p' * 4096 + '.epub', -1)]
    cases = [('route', path, str(kind), str(fast), '15') for path, kind in paths for fast in (0, 1)]
    cases += [('route', '/frequency.epub', '-1', '1', str(frequency)) for frequency in (-1, 0, 1, 2, 3)]
    cases += [('failure', fault, str(index), str(fast))
              for fault, count in [('rust', 2), ('native', 4)]
              for index in range(count) for fast in (0, 1)]
    cases += [('invalid-span',), ('null-table',)]
    assert len(cases) == 57
    scaffold = (here / 'app_owned_epub.cpp').read_text().split('int main(int argc, char** argv) {', 1)[0]
    source = scaffold + '\n' + (here / 'app_routing.inc').read_text()
    inputs = [here / p for p in ('app_owned_epub.cpp', 'app_routing.inc', 'base_session_fixture.h',
                                 'physical_defaults.h', 'test_app_routing.py')]
    inputs += list((root / 'rust/crates/xp-ffi/src/app').rglob('*.rs'))
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    archive_hash = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='reader-app-routing-') as directory:
        work = Path(directory)
        evidence = args.evidence_dir or work / 'evidence'
        evidence.mkdir(parents=True, exist_ok=False)
        (work / 'routing.cpp').write_text(source)
        (evidence / 'routing.cpp').write_text(source)
        commands, results = [], []

        def run(command, label):
            commands.append(list(map(str, command)))
            (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
            (evidence / (label + '.stdout')).write_bytes(result.stdout)
            (evidence / (label + '.stderr')).write_bytes(result.stderr)
            assert result.returncode == 0, (label, command, result.returncode, result.stderr.decode(errors='replace'))
            results.append(dict(label=label, returncode=result.returncode))

        try:
            binary = work / 'routing'
            run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION', '-I', str(here),
                 '-I', str(root / 'test/ui_runtime'), str(work / 'routing.cpp'),
                 *rust_link_flags(archive=str(args.archive)), '-o', str(binary)], 'compile')
            for index, case in enumerate(cases):
                run([str(binary), *case], f'{index:02d}-{case[0]}')
            summary = dict(cases=cases, results=results, source_sha256=hashes, archive_sha256=archive_hash,
                           policy_hooks=0, scope='Actual App navigation, native factory observations, owned entry, '
                           'construction Frequency sequence, complete-table requirement and acquisition failure recovery.')
            (evidence / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(f'PASS {len(cases)} App routing/construction/null-table cases; no policy hooks')
        finally:
            assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p, h in hashes.items())
            assert hashlib.sha256(args.archive.read_bytes()).hexdigest() == archive_hash


if __name__ == '__main__':
    main()

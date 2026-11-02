"""Exercise dormant App-owned EPUB acquisition and lifetime with physical fakes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args()
    root = args.repo.resolve()
    here = Path(__file__).resolve().parent
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(root / 'test'))
    from rust_host import rust_link_flags
    cases = [('rust-failure', str(i)) for i in range(2)]
    cases += [('native-failure', str(i)) for i in range(4)]
    cases += [('entry',), ('active-pending',)]
    source_paths = [here / 'app_owned_epub.cpp', here / 'physical_defaults.h', Path(__file__).resolve(),
                    root / 'rust/crates/xp-ffi/src/app/mod.rs']
    source_paths += list((root / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs'))
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
    with tempfile.TemporaryDirectory(prefix='app-owned-epub-') as directory:
        work = Path(directory)
        evidence = args.evidence_dir or work / 'evidence'
        evidence.mkdir(parents=True, exist_ok=False)
        commands, results = [], []
        def run(argv, label):
            commands.append([str(x) for x in argv])
            (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
            result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
            (evidence / (label + '.stdout')).write_bytes(result.stdout)
            (evidence / (label + '.stderr')).write_bytes(result.stderr)
            assert result.returncode == 0, (label, result.returncode, result.stderr.decode(errors='replace'))
            results.append({'case': label, 'returncode': result.returncode,
                            'stdout_sha256': hashlib.sha256(result.stdout).hexdigest()})
            return result
        try:
            binary = work / 'app_owned_epub'
            run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION', '-I', str(here), '-I', str(root / 'test/ui_runtime'),
                 str(here / 'app_owned_epub.cpp'), *rust_link_flags(archive=str(args.archive)),
                 '-o', str(binary)], 'compile')
            for case in cases:
                run([str(binary), *case], '-'.join(case))
            summary = {'cases': results, 'source_sha256': hashes, 'policy_hooks': 0,
                       'scope': 'constructor faults, actual cached entry, pending-owner cleanup and locked exit; no render/input/child-result coverage'}
            (evidence / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary))
        finally:
            assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == digest for p, digest in hashes.items())

if __name__ == '__main__':
    main()

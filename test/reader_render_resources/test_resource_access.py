"""Compile the actual session accessor with a resource loan across unlock."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GUARDED = Path('crates/xp-ffi/src/app/epub/guarded.rs')


def check(output):
    original = (ROOT / 'rust' / GUARDED).read_text()
    with tempfile.TemporaryDirectory(prefix='reader-resource-access-') as directory:
        work = Path(directory)
        copied = work / 'rust'
        shutil.copytree(ROOT / 'rust', copied, ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (work / name).symlink_to(ROOT / name, target_is_directory=True)
        results = []
        for mode, features in [('shipping-off', ''), ('shipping-on', 'reader-strips'),
                               ('app-off', 'app-test'), ('app-on', 'app-test,reader-strips')]:
            for case in ('scoped', 'after-unlock'):
                operations = ['core::hint::black_box(resources);', 'drop(access);']
                if case == 'after-unlock':
                    operations.reverse()
                probe = ('\nimpl EpubSession {\n'
                         '    fn resource_access_probe(&self, access: ResourceAccess<\'_>) {\n'
                         '        let resources = self.page_resources(&access);\n'
                         + ''.join('        ' + line + '\n' for line in operations)
                         + '    }\n}\n')
                (copied / GUARDED).write_text(original + probe)
                command = ['cargo', 'check', '--manifest-path', str(copied / 'Cargo.toml'),
                           '--target-dir', str(work / 'target'), '-p', 'xp-ffi', '--release',
                           '--locked', '--offline', '--message-format=json']
                if features:
                    command += ['--features', features]
                result = subprocess.run(command, cwd=copied, capture_output=True, text=True)
                messages = [json.loads(line) for line in result.stdout.splitlines()]
                errors = [row['message'] for row in messages if row.get('reason') == 'compiler-message'
                          and row['message']['level'] == 'error']
                if output:
                    (output / f'{mode}-{case}.jsonl').write_text(result.stdout)
                    (output / f'{mode}-{case}.stderr').write_text(result.stderr)
                results.append({'mode': mode, 'case': case, 'command': command,
                                'returncode': result.returncode, 'errors': errors})
                if output:
                    (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
                if case == 'scoped':
                    assert result.returncode == 0, (mode, errors, result.stderr)
                else:
                    assert result.returncode != 0 and len(errors) == 1, (mode, 'resource loan survived unlock', errors)
                    error = errors[0]
                    assert error['code']['code'] == 'E0505', error
                    assert error['message'] == 'cannot move out of `access` because it is borrowed', error
                    spans = error['spans']
                    assert all(span['file_name'].endswith('/app/epub/guarded.rs') for span in spans)
                    assert any(span['is_primary'] and any('drop(access);' in line['text']
                               for line in span['text']) for span in spans), spans
                    assert any(span['label'] == 'borrow later used here' and
                               any('black_box(resources)' in line['text'] for line in span['text'])
                               for span in spans), spans
        assert (ROOT / 'rust' / GUARDED).read_text() == original
    print('ResourceAccess: scoped loan compiles; use after unlock rejected in all four feature modes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
    check(args.output)

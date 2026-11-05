"""Compile the result-tag contract and reject a same-size variant reorder."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--evidence-dir', type=Path,
                        default=os.environ.get('XP_RESULT_TAG_EVIDENCE_DIR'))
    args = parser.parse_args()
    root = args.repo.resolve()
    temporary = tempfile.TemporaryDirectory(prefix='result-tags-') if args.evidence_dir is None else None
    out = args.evidence_dir.resolve() if args.evidence_dir else Path(temporary.name) / 'evidence'
    out.mkdir(parents=True, exist_ok=False)
    native = (root / 'src/activities/ActivityResult.h').read_text()
    view = (root / 'src/activities/ActivityResultView.h').read_text()
    rust = (root / 'rust/crates/xp-ffi/src/app/abi.rs').read_text()
    tags = dict(re.findall(r'pub const RESULT_(\w+): u8 = (\d+);', rust))
    assert set(tags) == {'MENU', 'CHAPTER', 'PERCENT', 'CLIPPING', 'PROGRESS', 'FOOTNOTE'}
    source = '#include "ActivityResultView.h"\n'
    source += ''.join(f'static_assert(XP_APP_RESULT_{name} == {value}, "C and Rust result tags differ");\n'
                      for name, value in tags.items())
    (out / 'contract.cpp').write_text(source)
    (out / 'ActivityResultView.h').write_text(view)
    (out / 'ActivityResult.h').write_text(native)
    command = [args.compiler, '-std=c++20', '-fsyntax-only',
               '-I' + str(root / 'rust/crates/xp-ffi/include'), str(out / 'contract.cpp')]
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')

    def compile_case(label):
        result = subprocess.run(command, capture_output=True, text=True)
        (out / (label + '.stdout')).write_text(result.stdout)
        (out / (label + '.stderr')).write_text(result.stderr)
        return result

    positive = compile_case('original')
    assert positive.returncode == 0, positive.stderr
    pair = 'MenuResult, ChapterResult'
    assert native.count(pair) == 1
    (out / 'ActivityResult.h').write_text(native.replace(pair, 'ChapterResult, MenuResult'))
    negative = compile_case('reordered')
    assert negative.returncode != 0, 'same-size variant reorder compiled'
    assert 'Activity result tag order changed' in negative.stderr, negative.stderr
    (out / 'result.json').write_text(json.dumps({
        'original_returncode': positive.returncode,
        'reordered_returncode': negative.returncode,
        'reorder_rejected_by_contract': True,
        'rust_tags_checked': tags,
    }, indent=2) + '\n')
    print('C/Rust tags agree; swapping Menu and Chapter fails the native order contract')


if __name__ == '__main__':
    main()

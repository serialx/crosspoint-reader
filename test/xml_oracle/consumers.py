#!/usr/bin/env python3
"""Record real C++ consumer/cache output using host HAL and ZIP-size adapters."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent


def capture(executable, epub, mode, chunk, work):
    extracted = work / 'book'
    if not extracted.exists():
        with zipfile.ZipFile(epub) as archive:
            archive.extractall(extracted)
    output = work / f'{mode}-{chunk}'
    subprocess.run([str(executable.resolve()), str(extracted), str(output), str(chunk), mode], check=True)
    assert not (output / '.items.bin').exists(), 'OPF temporary store survived parser destruction'
    return {name: (output / name).read_bytes().hex()
            for name in ('book.bin', 'spine.bin.tmp', 'toc.bin.tmp', 'items.bin', 'consumer.txt')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--epub', type=Path, action='append')
    parser.add_argument('--goldens', type=Path, default=ROOT / 'consumer_goldens')
    parser.add_argument('action', choices=('record', 'verify'))
    args = parser.parse_args()
    epubs = args.epub or sorted((ROOT.parent / 'epubs').glob('*.epub'))
    if args.action == 'record':
        args.goldens.mkdir(parents=True, exist_ok=True)
    for epub in epubs:
        result = {'sha256': hashlib.sha256(epub.read_bytes()).hexdigest(), 'modes': {}}
        with tempfile.TemporaryDirectory(prefix='xml-consumers-') as temp:
            for mode in ('nav', 'ncx'):
                reference = None
                for chunk in (1, 7, 512, 1024):
                    current = capture(args.oracle, epub, mode, chunk, Path(temp))
                    if reference is not None:
                        assert current == reference, (epub.name, mode, chunk)
                    reference = current
                result['modes'][mode] = reference
        golden = args.goldens / (epub.stem + '.json')
        if args.action == 'record':
            golden.write_text(json.dumps(result, indent=2) + '\n')
        else:
            assert result == json.loads(golden.read_text()), f'Consumer/cache drift: {epub.name}'
    print(f'{args.action}: {len(epubs)} EPUBs, both TOC preferences, 4 chunk sizes, byte-identical caches')


if __name__ == '__main__':
    main()

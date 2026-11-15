#!/usr/bin/env python3
"""Page bytes/value differential against production C++ v68 codecs."""
import argparse
import json
import importlib.util
import io
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent

def collect(archive, destination):
    inventory = []
    with tarfile.open(archive) as tar, destination.open('wb') as out:
        for member in tar.getmembers():
            if not member.isfile() or not member.name.endswith('.bin'):
                continue
            section = tar.extractfile(member).read()
            if len(section) < 44 or section[0] != 68:
                raise ValueError(f'Unexpected section: {member.name}')
            count, lut = struct.unpack_from('<HI', section, 22)
            visible_lut = struct.unpack_from('<I', section, 40)[0]
            starts = list(struct.unpack_from(f'<{count}I', section, lut)) + [lut]
            for page in range(count):
                data = section[starts[page]:starts[page + 1]]
                visible = struct.unpack_from('<I', section, visible_lut + page * 4)[0]
                out.write(struct.pack('<II', len(data), visible))
                out.write(data)
                inventory.append({'section': member.name, 'page': page, 'bytes': len(data)})
    return inventory

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', type=Path, required=True)
    ap.add_argument('--archive', type=Path, default=HERE.parent / 'section_golden/goldens/sections.tar.gz')
    ap.add_argument('--output', type=Path)
    ap.add_argument('--section-dump', type=Path)
    ap.add_argument('--epub', type=Path, action='append')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='page-codec-') as tmp:
        output = args.output or Path(tmp)
        output.mkdir(parents=True, exist_ok=True)
        corpus = output / 'pages.bin'
        archive = args.archive
        if args.section_dump:
            spec = importlib.util.spec_from_file_location('sections', HERE.parent / 'section_golden/run.py')
            sections = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(sections)
            archive = output / 'sections.tar.gz'
            books = args.epub or sorted((HERE.parent / 'epubs').glob('*.epub'))
            with tarfile.open(archive, 'w:gz') as tar:
                for index, epub in enumerate(books):
                    root = Path(tmp) / f'book-{index}'
                    root.mkdir()
                    sections.prepare(epub, root)
                    result = subprocess.run([str(args.section_dump.resolve()), str(root)], check=True, capture_output=True, text=True)
                    for record in result.stdout.splitlines():
                        config, spine, _, _ = record.split('\t')
                        data = (root / 'output' / config / f'{spine}.bin').read_bytes()
                        entry = tarfile.TarInfo(f'{epub.stem}/{config}/{spine}.bin')
                        entry.size = len(data)
                        tar.addfile(entry, io.BytesIO(data))
        inventory = collect(archive, corpus)
        (output / 'inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
        result = subprocess.run([str(args.dump.resolve()), str(corpus), str(output / 'memory.csv')], capture_output=True, text=True)
        if result.returncode:
            print(result.stderr, end='')
            result.check_returncode()
        (output / 'result.json').write_text(result.stdout)
        print(result.stdout, end='')

if __name__ == '__main__':
    main()

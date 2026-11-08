"""Compare every parsed field/interval byte with the frozen native parser."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def corpus(data):
    yield data
    region = min(160, len(data))
    for size in range((len(data) if len(data) < 256 else region) + 1):
        yield data[:size]
    for pos in range(region):
        for bit in range(8):
            bad = bytearray(data)
            bad[pos] ^= 1 << bit
            yield bad
    for toc in range(32, region - 31, 32):
        count, = struct.unpack_from('<I', data, toc + 4)
        offset, = struct.unpack_from('<I', data, toc + 24)
        if count and count <= 4096 and offset + count * 12 <= len(data):
            for start in (offset, offset + (count - 1) * 12):
                for pos in range(start, start + 12):
                    for bit in range(8):
                        bad = bytearray(data)
                        bad[pos] ^= 1 << bit
                        yield bad
        for pos in (toc + 4, toc + 8, toc + 24):
            for value in (0, 1, 4096, 4097, 65535, 65536, 65537, len(data), len(data) + 1, 0xfffffff0, 0xffffffff):
                bad = bytearray(data)
                struct.pack_into('<I', bad, pos, value)
                yield bad


def synthetic(first, last, count, styles=1):
    data = bytearray(32 + styles * 32 + 44)
    data[:8] = b'CPFONT\0\0'
    data[8] = 4
    data[12] = styles
    for i in range(styles):
        toc = 32 + i * 32
        data[toc] = i
        struct.pack_into('<II', data, toc + 4, 1, count)
        struct.pack_into('<I', data, toc + 24, 32 + styles * 32)
    struct.pack_into('<III', data, 32 + styles * 32, first, last, 0)
    return data


def main():
    args = argparse.ArgumentParser()
    args.add_argument('--oracle', required=True, type=Path)
    args.add_argument('--candidate', required=True, type=Path)
    args = args.parse_args()
    root = Path(__file__).resolve().parents[2]
    fonts = {p.name: gzip.decompress(p.read_bytes()) for p in sorted((root / 'test/text_golden/fonts').glob('*.cpfont.gz'))}
    assert len(fonts) == 3, 'public fixture census changed'
    for first, last, count, styles in [(65, 66, 2, 4), (0x10000, 0x10001, 2, 1), (0, 0xffffffff, 0, 1), (1, 0, 1, 1)]:
        fonts[f'synthetic-{first}-{last}-{styles}'] = synthetic(first, last, count, styles)
    cases = 0
    with tempfile.TemporaryDirectory(prefix='sdfont-owner-') as temp:
        temp = Path(temp)
        with (temp / 'input').open('wb') as stream:
            for data in fonts.values():
                for case in corpus(data):
                    stream.write(struct.pack('<I', len(case)))
                    stream.write(case)
                    cases += 1
        outputs = []
        for name, exe in [('native', args.oracle), ('rust', args.candidate)]:
            out = temp / name
            with (temp / 'input').open('rb') as source, out.open('wb') as dest:
                subprocess.run([str(exe.resolve())], stdin=source, stdout=dest, check=True)
            outputs.append(out.read_bytes())
        assert outputs[0] == outputs[1], 'parsed output bytes differ'
        print(json.dumps({'cases': cases, 'canonical_bytes': len(outputs[0]), 'sha256': hashlib.sha256(outputs[0]).hexdigest(),
            'fixtures': {k: hashlib.sha256(v).hexdigest() for k, v in fonts.items()}}, indent=2))


if __name__ == '__main__':
    main()

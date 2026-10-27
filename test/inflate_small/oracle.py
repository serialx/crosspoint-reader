#!/usr/bin/env python3
"""Freeze uzlib results before changing firmware; compare candidate bytes separately."""
import argparse
import gzip
import hashlib
import json
import pathlib
import random
import re
import struct
import subprocess
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def raw(data, strategy=zlib.Z_DEFAULT_STRATEGY):
    c = zlib.compressobj(9, zlib.DEFLATED, -15, strategy=strategy)
    return c.compress(data) + c.flush()


def fonts():
    for path in sorted((ROOT / 'lib/EpdFont/builtinFonts').glob('*.h')):
        text = path.read_text()
        bitmap = re.search(r'uint8_t \w+Bitmaps\[.*?\] = \{(.*?)\};', text, re.S)
        groups = re.search(r'EpdFontGroup \w+Groups\[.*?\] = \{(.*?)\};', text, re.S)
        if not bitmap or not groups:
            continue
        data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', bitmap[1]))
        for i, row in enumerate(re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', groups[1])):
            offset, size, length, _, _ = map(int, row)
            yield f'{path.stem}/{i}', data[offset:offset + size], length


def fixture():
    # Public-domain opening of the US Declaration of Independence, plus byte diversity.
    text = (b'When in the Course of human events, it becomes necessary for one people '
            b'to dissolve the political bands which have connected them with another.\n')
    data = (text * 800) + bytes(range(256)) * 100
    size = 4096
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    chunks = [compressor.compress(data[i:i + size]) + compressor.flush(
        zlib.Z_FINISH if i + size >= len(data) else zlib.Z_FULL_FLUSH)
        for i in range(0, len(data), size)]
    ra = struct.pack('<HHH', 1, size, len(chunks)) + b''.join(struct.pack('<H', len(c)) for c in chunks)
    extra = b'RA' + struct.pack('<H', len(ra)) + ra
    encoded = b'\x1f\x8b\x08\x04' + bytes(6) + struct.pack('<H', len(extra)) + extra
    encoded += b''.join(chunks) + struct.pack('<II', zlib.crc32(data), len(data))
    return data, chunks, encoded


def probe(binary, data, capacity, streaming=0, inchunk=2048, outchunk=512, iterations=1):
    request = struct.pack('<6I', len(data), capacity, streaming, inchunk, outchunk, iterations) + data
    try:
        p = subprocess.run([binary], input=request, capture_output=True, timeout=2)
    except subprocess.TimeoutExpired:
        return {'failure': 'timeout'}
    if p.returncode:
        return {'failure': f'exit {p.returncode}'}
    status, size, ns = struct.unpack('<iIQ', p.stdout[:16])
    output = p.stdout[16:]
    assert len(output) == size
    return {'status': status, 'length': size, 'sha256': hashlib.sha256(output).hexdigest(), 'ns': ns}


def malformed():
    seeds = [raw(b'hello hello hello', zlib.Z_FIXED), raw(bytes(range(16))),
             bytes.fromhex('010300fcff616263'), raw(bytes(range(24)) * 40)]
    for s, seed in enumerate(seeds):
        for n in range(len(seed)):
            yield f'truncate/{s}/{n}', seed[:n], 128
        for n in range(len(seed) * 8):
            changed = bytearray(seed)
            changed[n // 8] ^= 1 << (n % 8)
            yield f'bitflip/{s}/{n}', bytes(changed), 128
        for cap in [1, 2, 7, 15]:
            yield f'output-limit/{s}/{cap}', seed, cap
    # Reserved block type; fixed backreference before any output; malformed dynamic tables.
    for label, value in [('reserved', '07'), ('history', '030200'),
                         ('oversubscribed', '05009200'), ('incomplete', '05008000')]:
        yield label, bytes.fromhex(value), 128


def stable(result):
    return {k: v for k, v in result.items() if k != 'ns'}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--probe', required=True)
    p.add_argument('--record', action='store_true')
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--iterations', type=int, default=30)
    a = p.parse_args()
    expected_path = pathlib.Path(__file__).with_name('uzlib-oracle.json.gz')
    results = {'fonts': {}, 'chunks': {}, 'malformed': {}}
    for name, data, length in fonts():
        decoded = zlib.decompress(data, -15)
        assert len(decoded) == length
        result = probe(a.probe, data, length, iterations=a.iterations)
        assert result.get('sha256') == hashlib.sha256(decoded).hexdigest(), (name, result)
        results['fonts'][name] = result
    data, chunks, encoded = fixture()
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'public-text.dz').write_bytes(encoded)
    (a.output / 'public-text.txt').write_bytes(data)
    for i, chunk in enumerate(chunks):
        for inchunk, outchunk in [(1, 1), (7, 511), (2048, 512)]:
            name = f'{i}/{inchunk}/{outchunk}'
            decoded = zlib.decompressobj(-15).decompress(chunk)
            result = probe(a.probe, chunk, len(decoded), 1, inchunk, outchunk)
            assert result.get('sha256') == hashlib.sha256(decoded).hexdigest(), (name, result)
            results['chunks'][name] = result
    for name, compressed, capacity in malformed():
        results['malformed'][name] = {
            'input': compressed.hex(), 'capacity': capacity,
            'result': stable(probe(a.probe, compressed, capacity))}
    (a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    frozen = {group: {k: stable(v) for k, v in values.items()} for group, values in results.items()}
    if a.record:
        expected_path.write_bytes(gzip.compress(json.dumps(frozen, sort_keys=True).encode(), mtime=0))
    else:
        expected = json.loads(gzip.decompress(expected_path.read_bytes()))
        assert results['fonts'].keys() == expected['fonts'].keys()
        changes = {k: {'old': expected['malformed'][k], 'new': v}
                   for k, v in results['malformed'].items() if v != expected['malformed'][k]}
        (a.output / 'malformed-differences.json').write_text(json.dumps(changes, indent=2) + '\n')
        print(f'{len(changes)} malformed differences require review')
    print({group: len(values) for group, values in results.items()})


if __name__ == '__main__':
    main()

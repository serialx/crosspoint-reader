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


def fixture(size=4096, length=None):
    # Public-domain opening of the US Declaration of Independence, plus byte diversity.
    text = (b'When in the Course of human events, it becomes necessary for one people '
            b'to dissolve the political bands which have connected them with another.\n')
    data = (text * 800) + bytes(range(256)) * 100
    if length is not None:
        data = data[:length]
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
    rng = random.Random(7)
    dynamic = raw(bytes(rng.choices(range(8), weights=[50, 20, 10, 7, 5, 4, 3, 1], k=150)))
    assert (dynamic[0] >> 1) & 3 == 2
    seeds = [raw(b'hello hello hello', zlib.Z_FIXED), raw(bytes(range(16))),
             bytes.fromhex('010300fcff616263'), raw(bytes(range(24)) * 40), dynamic]
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
    for label, value in [('reserved', '07'), ('history', '030200')]:
        yield label, bytes.fromhex(value), 128
    for label, lengths in [('oversubscribed', [1, 1, 1, 1]), ('incomplete', [2, 2, 0, 0])]:
        bits = 5  # Final dynamic block, minimum HLIT/HDIST/HCLEN.
        for i, length in enumerate(lengths):
            bits |= length << (17 + 3 * i)
        yield label, bits.to_bytes(4, 'little'), 128


def dictionary(binary, output):
    data, _, encoded = fixture(58000)
    assert gzip.decompress(encoded) == data
    path = output / 'dictionary.dz'
    path.write_bytes(encoded)
    rng = random.Random(71)
    ranges = [(0, len(data)), (57999, 2), (58000, 1), (115999, 2), (len(data) - 1, 1)]
    ranges += [(offset, min(5000, len(data) - offset)) for offset in range(0, len(data), 4096)]
    for _ in range(100):
        offset = rng.randrange(len(data))
        ranges.append((offset, rng.randrange(min(70000, len(data) - offset)) + 1))
    results = {}
    for offset, size in ranges:
        request = struct.pack('<II', offset, size)
        result = subprocess.run([binary, str(path)], input=request, capture_output=True, check=True)
        status, length, ns = struct.unpack('<iIQ', result.stdout[:16])
        assert status == 0 and length == size, (offset, size, status, length)
        assert result.stdout[16:] == data[offset:offset + size], (offset, size)
        results[f'{offset}/{size}'] = {'length': size, 'sha256': hashlib.sha256(result.stdout[16:]).hexdigest(), 'ns': ns}
    # The final chunk ends exactly at DictZip's 512-byte read boundary.
    data, _, encoded = fixture(512, 1536)
    path.write_bytes(encoded)
    result = subprocess.run([binary, str(path)], input=struct.pack('<II', 1024, 512),
                            capture_output=True, check=True)
    status, size, ns = struct.unpack('<iIQ', result.stdout[:16])
    assert (status, size, result.stdout[16:]) == (0, 512, data[1024:])
    results['final-full-buffer'] = {'length': size, 'sha256': hashlib.sha256(result.stdout[16:]).hexdigest(), 'ns': ns}
    return results


def stable(result):
    return {k: v for k, v in result.items() if k != 'ns'}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--probe', required=True)
    p.add_argument('--record', action='store_true')
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--iterations', type=int, default=30)
    p.add_argument('--dict-probe')
    a = p.parse_args()
    expected_path = pathlib.Path(__file__).with_name('uzlib-oracle.json.gz')
    expected = None if a.record else json.loads(gzip.decompress(expected_path.read_bytes()))
    results = {'fonts': {}, 'chunks': {}, 'streams': {}, 'malformed': {}}
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
    rng = random.Random(191)
    blocks = [b'a' * 1000, bytes(rng.getrandbits(8) for _ in range(50000)), b'abcdef' * 10000]
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    compressed = b''.join(compressor.compress(block) + compressor.flush(zlib.Z_SYNC_FLUSH)
                          for block in blocks) + compressor.flush(zlib.Z_FINISH)
    decoded = b''.join(blocks)
    for inchunk, outchunk in [(1, 1), (7, 511), (2048, 512), (65536, 32768)]:
        result = probe(a.probe, compressed, len(decoded), 1, inchunk, outchunk)
        assert result.get('sha256') == hashlib.sha256(decoded).hexdigest()
        results['streams'][f'{inchunk}/{outchunk}'] = result
    cases = malformed() if a.record else (
        (name, bytes.fromhex(case['input']), case['capacity']) for name, case in expected['malformed'].items())
    for name, compressed, capacity in cases:
        results['malformed'][name] = {
            'input': compressed.hex(), 'capacity': capacity,
            'result': stable(probe(a.probe, compressed, capacity))}
    if a.dict_probe:
        results['dictionary'] = dictionary(a.dict_probe, a.output)
    (a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    frozen = {group: {k: stable(v) for k, v in values.items()} for group, values in results.items()}
    if a.record:
        expected_path.write_bytes(gzip.compress(json.dumps(frozen, sort_keys=True).encode(), mtime=0))
    else:
        assert results['fonts'].keys() == expected['fonts'].keys()
        for group in ['fonts', 'chunks', 'streams', 'dictionary']:
            for name, value in results.get(group, {}).items():
                for key in ['length', 'sha256']:
                    assert value[key] == expected[group][name][key], (group, name, key)
        changes = {k: {'old': expected['malformed'][k], 'new': v}
                   for k, v in results['malformed'].items() if v != expected['malformed'][k]}
        (a.output / 'malformed-differences.json').write_text(json.dumps(changes, indent=2) + '\n')
        documented = json.loads(pathlib.Path(__file__).with_name('strict-results.json').read_text())
        assert changes.keys() == documented.keys(), 'Malformed difference set changed'
        for name, change in changes.items():
            assert change['new']['result'] == documented[name], name
        print(f'{len(changes)} documented malformed differences')
    print({group: len(values) for group, values in results.items()})


if __name__ == '__main__':
    main()

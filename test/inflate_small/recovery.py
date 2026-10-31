#!/usr/bin/env python3
"""Exercise production dictionary publication and font reader recovery."""
import argparse
import pathlib
import struct
import subprocess
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def extract_definition(output):
    # Compile the production methods without unrelated index/UI dependencies.
    source = (ROOT / 'src/util/Dictionary.cpp').read_text()
    parts = [line for line in source.splitlines() if line.startswith('constexpr ') and
             ('DICT_TMP_FILE =' in line or 'DEFINITION_HEAP_HEADROOM_BYTES =' in line)]
    assert len(parts) == 2
    for signature in ['bool Dictionary::buildPath(', 'bool Dictionary::readDefinition(']:
        start = source.index(signature)
        end = source.index('{', start) + 1
        depth = 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        parts.append(source[start:end])
    output.write_text('\n'.join(parts) + '\n')


def run(binary, output):
    output.mkdir(parents=True, exist_ok=True)
    data = bytes(range(256)) * 8
    # A stored block makes the injected byte position unambiguous: one completed
    # 512-byte slice, then a partial second slice when input fails at byte 700.
    compressed = b'\x01' + struct.pack('<HH', len(data), 0xffff - len(data)) + data
    ra = struct.pack('<HHHH', 1, len(data), 1, len(compressed))
    extra = b'RA' + struct.pack('<H', len(ra)) + ra
    encoded = b'\x1f\x8b\x08\x04' + bytes(6) + struct.pack('<H', len(extra)) + extra
    encoded += compressed + struct.pack('<II', zlib.crc32(data), len(data))
    path = output / 'recovery.dict.dz'
    path.write_bytes(encoded)
    subprocess.run([binary, str(path)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--extract', type=pathlib.Path)
    parser.add_argument('--probe')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    if args.extract:
        extract_definition(args.extract)
    else:
        run(args.probe, args.output)

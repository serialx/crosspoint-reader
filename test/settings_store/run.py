#!/usr/bin/env python3
"""Compile the actual store facades with deterministic storage/crypto leaves."""
import argparse
import base64
import gzip
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def corpus():
    cases = {}
    templates = {
        'state': {'recentSleepImages': [1, 2, 65535, '3', -1], 'recentSleepPos': 255, 'recentSleepFill': 20, 'lastSleepImage': 8},
        'recent': {'books': [{'path': '/a', 'title': '한글 é', 'author': 'A', 'coverBmpPath': '/cache/a'}, None, 1]},
        'wifi': {'credentials': [{'ssid': 'first', 'password': 'secret'}, {'ssid': 'empty', 'password': ''}, {'ssid': 'crc', 'password_len': 1, 'password_crc32': 0}]},
        'opds': {'servers': [{'name': 'one', 'url': 'http://local', 'password': 'secret', 'headers': [{'name': 'X', 'value_obf': 'AAAA'}]}]},
        'tokens': {'tokens': [{'url': 'a', 'access_obf': 'AAAA', 'refresh_obf': 'AAAA', 'refresh_url': 'b'}, {'url': ''}]},
    }
    for kind, obj in templates.items():
        encoded = json.dumps(obj, ensure_ascii=False, separators=(',', ':'))
        inputs = ['', '{}', 'null', '[]', 'true', '0', '"s"', encoded]
        inputs += [encoded[:i] for i in range(len(encoded))]
        inputs += [encoded[:i] + '?' + encoded[i+1:] for i in range(len(encoded))]
        key = next(iter(obj))
        for value in [None, False, -1, 256, 'text', {}, [], [None] * 20]:
            inputs.append(json.dumps({key: value}))
        if kind == 'wifi':
            for length in [None, -1, 0, 64, 65, 4294967295, '1', True, 1.5]:
                for checksum in [None, -1, 0, 4294967295, '1', True]:
                    inputs.append(json.dumps({'credentials': [{'ssid': 'x', 'password': 'a', 'password_len': length, 'password_crc32': checksum}]}))
            for password in ['', 'x' * 64, 'x' * 65, 'x' * 2048]:
                inputs.append(json.dumps({'credentials': [{'ssid': 'x', 'password': password}]}))
        cases[kind] = inputs
    cases['obf'] = ['', '!', '====', 'AA', 'AAA', 'AAAA', 'AA==', 'AAA=', 'AA A', 'AAAA ', 'AAAA\t', 'AA=A']
    cases['obf'] += [base64.b64encode(bytes(range(i))).decode() for i in range(100)]
    cases['buffer'] = [str(i) for i in range(100)]
    return cases

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source', type=Path, default=ROOT)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--arduinojson', type=Path, required=True)
    p.add_argument('--mbedtls', type=Path, required=True)
    p.add_argument('--archive', type=Path)
    p.add_argument('--record', type=Path)
    p.add_argument('--golden', type=Path, default=HERE/'golden.json.gz')
    args = p.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    includes = [HERE/'stubs', args.source/'src', args.source/'lib/Serialization', ROOT/'lib/HttpHeader', ROOT/'rust/crates/xp-ffi/include', args.arduinojson, args.mbedtls/'include', args.mbedtls/'library']
    flags = [f'-I{x}' for x in includes]
    subprocess.run(['cc', '-c', str(args.mbedtls/'library/base64.c'), *flags, '-DMBEDTLS_CONFIG_FILE="oracle_mbedtls.h"', '-o', str(args.build/'base64.o')], check=True)
    sources = [HERE/'Oracle.cpp', *[args.source/'src'/f'{name}.cpp' for name in ['CrossPointState', 'RecentBooksStore', 'WifiCredentialStore', 'OpdsServerStore', 'OpdsTokenStore']], args.source/'lib/Serialization/PersistableStore.cpp', args.source/'lib/Serialization/ObfuscationUtils.cpp']
    if args.archive:
        sources += [ROOT/'test/cmake/xp_ffi_host.cpp', args.archive]
    subprocess.run(['c++', '-std=c++20', '-O2', '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0', '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', *flags, *map(str, sources), str(args.build/'base64.o'), '-o', str(args.build/'oracle')], check=True)
    actual = {}
    for kind, inputs in corpus().items():
        run = subprocess.run([str(args.build/'oracle'), kind], input='\n'.join(inputs)+'\n', text=True, capture_output=True, check=True)
        actual[kind] = [json.loads(line) for line in run.stdout.splitlines()]
        assert len(actual[kind]) == len(inputs)
    if args.record:
        with gzip.open(args.record, 'wt') as out: json.dump(actual, out, ensure_ascii=False)
    else:
        with gzip.open(args.golden, 'rt') as stream: expected = json.load(stream)
        for kind, results in actual.items():
            for i, result in enumerate(results):
                if result != expected[kind][i]:
                    raise AssertionError(f'{kind} case {i}: {corpus()[kind][i]!r}\nexpected {expected[kind][i]}\nactual {result}')
    print(f'{sum(map(len, actual.values()))} cases passed')
if __name__ == '__main__': main()

#!/usr/bin/env python3
"""Compare production bookmark persistence against a frozen native byte oracle."""
import argparse
import copy
import hashlib
import json
import pathlib
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASELINE = '2543f2a4ef2b092bb01483c5941f12008e2490ca'
SDK = '6a573acd0562e45e4ce96837e0c2aebada219760'


def hexbytes(value):
    return (value.encode() if isinstance(value, str) else value).hex()


def record(**values):
    result = dict(xpath=hexbytes('/body/p[1]'), summary=hexbytes('Synthetic text'),
                  name='', bits=0, si=0, pc=0, pp=0, has_vo=False, vo=0)
    result.update(values)
    return result


def corpus():
    cases = []
    path = '/books/example.epub'
    target = '/.crosspoint/bookmarks/books_example.json'

    def save(name, records, **extra):
        cases.append(dict(id=name, action='save', path=hexbytes(path), records=records, **extra))

    def load(name, data, **extra):
        files = [] if data is None else [dict(path=hexbytes(target), bytes=hexbytes(data))]
        cases.append(dict(id=name, action='load', path=hexbytes(path), files=files,
                          records=[record(name=hexbytes('cleared sentinel'), bits=0x3f800000)], **extra))

    save('empty', [])
    save('one', [record()])
    save('multiple-optionals', [record(), record(name=hexbytes('Named'), has_vo=True, vo=0),
                                record(name=hexbytes('한글 é'), has_vo=True, vo=0xffffffff,
                                       si=65535, pc=65534, pp=32768, bits=0x3f800000)])
    for bits in [0, 0x80000000, 0x3f800000, 0x3dcccccd, 0x3eaaaaab, 0x3f7fffff,
                 0x3f800001, 0x00000001, 0x007fffff, 0x00800000, 0x7f7fffff,
                 0x7f800000, 0xff800000, 0x7fc00000, 0xbf800000]:
        save(f'float-{bits:08x}', [record(bits=bits)])
    for value in [1e-5, 1e7]:
        bits = struct.unpack('<I', struct.pack('<f', value))[0]
        for delta in [-1, 0, 1]:
            save(f'exponent-{bits+delta:08x}', [record(bits=bits+delta)])
    for name, text in [('escapes', b'quote" slash\\ /\b\f\n\r\t\x01\x1f'),
                       ('nul', b'before\0after'), ('unicode', '한글 中文 é 🙂'.encode()),
                       ('invalid-utf8', b'\xff\xfe\xc0\xaf\xed\xa0\x80\x80')]:
        save(name, [record(xpath=hexbytes(text), name=hexbytes(text), summary=hexbytes(text))])
    for n in [128, 129]:
        save(f'name-{n}', [record(name=hexbytes('x' * n))],
             files=[dict(path=hexbytes(target), bytes=hexbytes('old target'))])
    paths = ['/a.b.c.epub', '/nested/a\\b.epub', '/no-extension', 'noslash.epub',
             '', '/', '/dir.with.dot/leaf', '/.epub', '/한글/book.epub', b'/bad\xff\\x.e',
             b'/embedded\0tail.epub', '/line\nname.epub']
    for i, p in enumerate(paths):
        cases.append(dict(id=f'path-{i}', action='save', path=hexbytes(p), records=[]))
    for name, data in [('missing', None), ('empty-input', ''), ('malformed', '{broken'),
                       ('truncated', '{"bookmarks":[{"xpath":"a"'), ('no-fields', '{}'),
                       ('null-root', 'null'), ('wrong-array', '{"bookmarks":{}}'),
                       ('absent-fields', '{"bookmarks":[{}]}'),
                       ('wrong-fields', '{"bookmarks":[{"xpath":7,"summary":true,"name":[],"percentage":"bad","si":-1,"pc":65536,"pp":"4","vo":"bad"}]}'),
                       ('null-offset', '{"bookmarks":[{"vo":null}]}'),
                       ('zero-offset', '{"bookmarks":[{"vo":0}]}'),
                       ('max-offset', '{"bookmarks":[{"vo":4294967295}]}'),
                       ('large-fields', '{"bookmarks":[{"si":65535,"pc":65535,"pp":65535,"percentage":0.1}]}'),
                       ('non-object-records', '{"bookmarks":[null,0,"x",[],{}]}'),
                       ('load-escape', '{"bookmarks":[{"name":"a\\u0000b\\n\\uD83D\\uDE42"}]}'),
                       ('load-invalid', b'{"bookmarks":[{"name":"\xff\xfe"}]}'),
                       ('load-long-name', json.dumps({'bookmarks':[{'name':'x'*129}]})),
                       ('load-split-utf8', json.dumps({'bookmarks':[{'name':'x'*127+'한'}]}))]:
        load(name, data)
    good = '{"bookmarks":[{"xpath":"p","percentage":0.5}]}'
    for op in ['exists', 'open-read', 'read', 'close']:
        load(f'fail-{op}', good, fail=op)
    load('short-read', good, read_limit=12)
    for op, nth in [('mkdir', 1), ('mkdir', 2), ('open-write', 1), ('write', 1),
                    ('close', 1), ('remove', 1), ('rename', 1)]:
        save(f'fail-{op}-{nth}', [record()], fail=op, fail_at=nth,
             files=[dict(path=hexbytes(target), bytes=hexbytes('old target'))])
    save('short-write', [record()], write_limit=17,
         files=[dict(path=hexbytes(target), bytes=hexbytes('old target'))])
    save('zero-write', [record()], write_limit=0,
         files=[dict(path=hexbytes(target), bytes=hexbytes('old target'))])
    save('failed-short-cleanup', [record()], write_limit=17, fail='remove',
         files=[dict(path=hexbytes(target), bytes=hexbytes('old target'))])
    save('stale-tmp-replaced', [record()],
         files=[dict(path=hexbytes(target), bytes=hexbytes('old target')),
                dict(path=hexbytes(target+'.tmp'), bytes=hexbytes('stale temporary'))])
    save('rename-and-cleanup-fail', [record()], failures={'rename': 1, 'remove': 2},
         files=[dict(path=hexbytes(target), bytes=hexbytes('old target'))])
    save('close-and-cleanup-fail', [record()], failures={'close': 1, 'remove': 1},
         files=[dict(path=hexbytes(target), bytes=hexbytes('old target'))])
    for field in ['xpath', 'summary', 'name', 'percentage', 'si', 'pc', 'pp', 'vo']:
        for label, value in [('null', None), ('bool', True), ('array', []), ('object', {}),
                             ('negative', -1), ('overflow', 4294967296), ('number-string', '0.5')]:
            load(f'typed-{field}-{label}', json.dumps({'bookmarks': [{field: value}]}))
    return cases


def extract(text, signature):
    start = text.index(signature)
    assert text.count(signature) == 1, signature
    opening = text.index('{', start)
    # These selected bodies have no braces in strings/comments.
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end], start, end


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compare(expected, actual):
    assert expected.keys() == actual.keys(), 'case inventory changed'
    for name in expected:
        assert expected[name] == actual[name], f'bookmark byte/operation/record mismatch: {name}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--arduinojson', type=pathlib.Path, required=True)
    parser.add_argument('--archive', type=pathlib.Path, required=True)
    parser.add_argument('--golden', type=pathlib.Path, default=HERE/'golden.json')
    parser.add_argument('--capture-baseline', action='store_true')
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    sdk = ROOT/'freeink-sdk'
    assert subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip() == SDK
    sources = [ROOT/'src/util/BookmarkFile.cpp', ROOT/'src/util/BookmarkFile.h',
               ROOT/'src/util/BookmarkUtil.cpp', ROOT/'src/util/BookmarkUtil.h', ROOT/'src/BookmarkEntry.h',
               ROOT/'lib/Serialization/PersistableStore.cpp', ROOT/'lib/Serialization/PersistableStore.h',
               ROOT/'rust/crates/xp-ffi/src/settings.rs',
               ROOT/'lib/hal/HalStorage.cpp', ROOT/'platformio.ini', ROOT/'rust/Cargo.lock']
    manifest = dict(head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    sdk=SDK, sources={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in sources},
                    extraction=[], arduinojson={str(p.relative_to(args.arduinojson)):sha(p.read_bytes())
                                              for p in sorted(args.arduinojson.rglob('*')) if p.is_file()})
    cpp = sdk/'libs/hardware/SDCardManager/src/SDCardManager.cpp'
    header = sdk/'libs/hardware/SDCardManager/include/SDCardManager.h'
    for source, signatures, output in [
        (cpp, ['String SDCardManager::readFile(const char* path)',
               'bool SDCardManager::writeFile(const char* path, const String& content)',
               'bool SDCardManager::openFileForRead(const char* moduleName, const char* path, FsFile& file)',
               'bool SDCardManager::openFileForWrite(const char* moduleName, const char* path, FsFile& file)'],
         args.build/'SdkStorage.cpp'),
        (header, ['bool mkdir(const char* path, const bool pFlag = true)',
                  'bool exists(const char* path)', 'bool replaceFile(const char* tmpPath, const char* path)'],
         args.build/'SdkInline.inc')]:
        pinned = subprocess.check_output(['git', '-C', str(sdk), 'show', SDK+':'+str(source.relative_to(sdk))])
        assert source.read_bytes() == pinned, f'SDK source differs from pinned {SDK}: {source}'
        original = pinned.decode()
        pieces = []
        for signature in signatures:
            body, start, end = extract(original, signature)
            assert body == original[start:end]
            manifest['extraction'].append(dict(source=str(source.relative_to(ROOT)),
                                               file_sha256=sha(source.read_bytes()), signature=signature,
                                               start_byte=len(original[:start].encode()), end_byte=len(original[:end].encode()),
                                               sha256=sha(body.encode())))
            pieces.append(body)
        output.write_text(('#include <HalStorage.h>\n' if source == cpp else '') + '\n\n'.join(pieces)+'\n')
    flags = ['-DARDUINOJSON_ENABLE_ARDUINO_STRING=1', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
             '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', '-DARDUINOJSON_ENABLE_PROGMEM=0',
             '-DARDUINOJSON_SIZEOF_POINTER=4']
    include = [HERE/'stubs', args.build, ROOT, ROOT/'src', ROOT/'lib/Serialization',
               ROOT/'rust/crates/xp-ffi/include', args.arduinojson]
    cmd = ['c++', '-std=c++20', '-O2', '-ffp-contract=off', '-ffunction-sections', '-fdata-sections', *flags,
           *['-I'+str(p) for p in include], str(HERE/'Oracle.cpp'),
           str(ROOT/'src/util/BookmarkFile.cpp'), str(ROOT/'src/util/BookmarkUtil.cpp'),
           str(ROOT/'lib/Serialization/PersistableStore.cpp'), str(args.build/'SdkStorage.cpp'),
           str(ROOT/'test/cmake/xp_ffi_host.cpp'), str(args.archive),
           '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-o', str(args.build/'oracle')]
    manifest['proof_boundary'] = {
        'proven': 'Host production serializer bytes with matching numeric/string ArduinoJson configuration, IEEE float32/double64 assertions, and actual Arduino String adapters',
        'not_proven': ['Target floating-point formatting execution', 'Physical FAT behavior', 'Power loss', 'Allocation failure', 'Task concurrency'],
        'host_only_configuration_differences': ['Arduino Stream/Print support disabled (unused)', 'PROGMEM disabled (host direct reads of the same constant bit patterns)'],
        'follow_ups': ['O5a design freeze must specify target-written bookmark comparison in eink-qemu', 'Physical bookmark files on X3 and S3 remain human QA'],
    }
    manifest['command'] = cmd
    manifest['compiler'] = subprocess.check_output(['c++', '--version'], text=True)
    manifest['dependency_metadata'] = {str(p): sha(p.read_bytes()) for p in
                                       [args.arduinojson.parent/'library.json', args.arduinojson.parent/'.piopm']
                                       if p.is_file()}
    subprocess.run(cmd, check=True)
    macros = subprocess.check_output(['c++', '-std=c++20', *flags, *['-I'+str(p) for p in include],
                                      '-dM', '-E', '-x', 'c++', '-include', 'ArduinoJson.h', '/dev/null'], text=True)
    manifest['configuration'] = sorted(line for line in macros.splitlines() if line.startswith('#define ARDUINOJSON_'))
    manifest['configuration_sha256'] = sha('\n'.join(manifest['configuration']).encode())
    manifest['arduinojson_tree_sha256'] = sha(json.dumps(manifest['arduinojson'], sort_keys=True).encode())
    manifest['arduinojson_release'] = '7.4.2'
    cases = corpus()
    manifest['corpus_sha256'] = sha(json.dumps(cases, sort_keys=True).encode())
    manifest['archive_sha256'] = sha(args.archive.read_bytes())
    (args.build/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    result = subprocess.run([str(args.build/'oracle')], input=''.join(json.dumps(c)+'\n' for c in cases),
                            capture_output=True, text=True, check=True)
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == len(cases)
    actual = {c['id']: row for c, row in zip(cases, rows)}
    for case in cases:
        faults = dict(case.get('failures', {}))
        if case.get('fail'):
            faults[case['fail']] = case.get('fail_at', 1)
        for operation in faults:
            assert any(event.startswith(operation+':') and event.endswith(':fail')
                       for event in actual[case['id']]['io']), f"Unreached failure: {case['id']} {operation}"

    (args.build/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
    (args.build/'actual.json').write_text(json.dumps(actual, indent=2)+'\n')
    if args.capture_baseline:
        assert manifest['head'] == BASELINE, 'Only the approved native parent may capture goldens'
        subprocess.run(['git', 'diff', '--exit-code', BASELINE, '--', *[str(p) for p in sources]], cwd=ROOT, check=True)
        if args.golden.exists():
            frozen = json.loads(args.golden.read_text())
            compare(frozen, {name: actual[name] for name in frozen})
            assert frozen.keys() < actual.keys(), 'Only new baseline cases may be appended; frozen cases cannot change'
        args.golden.write_text(json.dumps(actual, indent=2)+'\n')
    compare(json.loads(args.golden.read_text()), actual)
    for name, row in actual.items():
        dest = args.build/'raw'/name
        dest.mkdir(parents=True, exist_ok=True)
        for i, file in enumerate(row['files']):
            (dest/f'{i}.bin').write_bytes(bytes.fromhex(file['bytes']))
            (dest/f'{i}.path').write_bytes(bytes.fromhex(file['path']))
    if args.negative_controls:
        expected = json.loads(args.golden.read_text())
        original = bytes.fromhex(actual['one']['files'][0]['bytes'])
        mutations = {
            'byte': original.replace(b'Synthetic', b'synthetic'),
            'field-order': original.replace(b'"si":0,"pc":0', b'"pc":0,"si":0'),
            'float': original.replace(b'"percentage":0', b'"percentage":0.0'),
            'optional-field': original.replace(b'"si":0', b'"name":"","si":0'),
        }
        for label, changed in mutations.items():
            assert changed != original
            mutant = copy.deepcopy(actual)
            mutant['one']['files'][0]['bytes'] = changed.hex()
            try:
                compare(expected, mutant)
            except AssertionError as error:
                print(f'Negative control {label}: rejected ({error})')
            else:
                raise AssertionError(f'Comparator accepted {label} mutation')
    print(f'{len(cases)} bookmark cases passed')


if __name__ == '__main__':
    main()

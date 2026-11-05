#!/usr/bin/env python3
"""Compare production clipping persistence against a frozen native byte oracle."""
import argparse
import copy
import gzip
import hashlib
import json
import pathlib
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASELINE = 'deafbe1070b58ecfee66fc03e888aeb0bfd7b098'
SDK = '6a573acd0562e45e4ce96837e0c2aebada219760'

BOOK_A = b'/books/alpha.epub'
BOOK_B = b'/books/beta.epub'
BOOK_C = b'/books/sub/gamma.epub'
BOOK_RENAMED = b'/renamed/delta.epub'
BOOK_RENAMED_ALPHA = b'/renamed/alpha.epub'
BOOK_UNICODE = '/books/한글 é.epub'.encode()
BOOK_INVALID = b'/books/bad\xff.epub'
BOOK_LONG = b'/books/' + b'x' * 4090 + b'.epub'

PRODUCTION = ['src/ClippingStore.cpp', 'src/ClippingStore.h', 'src/clippings/ClippingsManager.cpp',
              'src/clippings/ClippingsManager.h', 'src/clippings/ClippingPreview.h', 'lib/Serialization/Serialization.h',
              'lib/Memory/Memory.h', 'lib/Utf8/Utf8.cpp', 'lib/Utf8/Utf8.h', 'lib/hal/HalStorage.h',
              'lib/hal/HalClock.h', 'platformio.ini', 'rust/Cargo.lock']


def hexbytes(value):
    return (value.encode() if isinstance(value, str) else value).hex()


def unhex(value):
    return bytes.fromhex(value)


def u8(value):
    return struct.pack('<B', value)


def u16(value):
    return struct.pack('<H', value)


def u32(value):
    return struct.pack('<I', value)


def header_string(value):
    return u32(len(value)) + value


def clip(**values):
    result = dict(spine=0, start=0, end=0, pages=1, startWord=0, endWord=0, words=1, para=0, timestamp=0, sig=1,
                  startOffset=0, endOffset=3, revision=0, pending=True, id=b'', textOffset=0, textLength=0,
                  chapter=b'Chapter')
    result.update(values)
    for key in ('id', 'chapter'):
        if isinstance(result[key], (bytes, bytearray)):
            result[key] = hexbytes(result[key])
    return result


def load(path=BOOK_A, title=b'', author=b'', book_type=b'epub'):
    return dict(op='load', path=hexbytes(path), title=hexbytes(title), author=hexbytes(author),
                type=hexbytes(book_type))


def add(text=b'one', **values):
    row = dict(op='add', text=hexbytes(text))
    row.update({key: hexbytes(value) if isinstance(value, (bytes, str)) and key in ('chapter',) else value
                for key, value in values.items()})
    return row


def remote(clipping=None, text=b'', deleted=False):
    return dict(op='remote', clipping=clip(**(clipping or {})), text=hexbytes(text), deleted=deleted)


def export(text=b'Kerning is the adjustment of spacing', page=12, title=b'Book A', author=b'Author A',
           chapter=b'Chapter One'):
    return dict(op='export', title=hexbytes(title), author=hexbytes(author), chapter=hexbytes(chapter),
                page=page, text=hexbytes(text))


def case(name, ops, **extra):
    row = dict(id=name, ops=ops)
    row.update(extra)
    return row


def build_legacy(version, records, title=b'Legacy', author=b'Author', path=BOOK_A):
    """Legacy v1/v3 input built from the read order of ClippingStore::readFromFile."""
    blob = u8(version) + u16(len(records)) + header_string(title) + header_string(author) + header_string(path)
    for record in records:
        blob += b''.join(u16(record.get(key, 0)) for key in
                         ('spine', 'start', 'end', 'pages', 'startWord', 'endWord', 'words', 'para'))
        blob += u32(record.get('timestamp', 0))
        if version >= 3:
            blob += u32(record.get('sig', 0))
        blob += record.get('chapter', b'').ljust(48, b'\0')[:48]
        text = record['text']
        blob += (u32(len(text)) if version == 1 else u16(len(text))) + text
    return blob


def v4_parts(blob):
    """Byte offset where the first record of a v4 store starts."""
    offset = 3
    for _ in range(3):
        length = struct.unpack_from('<I', blob, offset)[0]
        offset += 4 + length
    return offset


def read_records(blob):
    """Records as production writes them, used to build remote-application inputs."""
    version, count = struct.unpack_from('<BH', blob, 0)
    assert version == 4
    offset = v4_parts(blob)
    records = []
    for _ in range(count):
        layout = struct.unpack_from('<4I', blob, offset + 16)
        records.append(dict(timestamp=layout[0], sig=layout[1], startOffset=layout[2], endOffset=layout[3],
                            revision=struct.unpack_from('<Q', blob, offset + 32)[0], pending=blob[offset + 40] != 0,
                            id=blob[offset + 41:offset + 106],
                            text=blob[offset + 156:offset + 156 + struct.unpack_from('<H', blob, offset + 154)[0]]))
        offset += 156 + struct.unpack_from('<H', blob, offset + 154)[0]
    return records


def corpus(stores, canonical, synced, synced_records):
    cases = []
    checks = []
    store = stores[BOOK_A]
    store_b = stores[BOOK_B]
    store_c = stores[BOOK_C]
    store_unicode = stores[BOOK_UNICODE]
    store_invalid = stores[BOOK_INVALID]
    export_path = b'/My Clippings.txt'

    def expect(name, path, value):
        checks.append((name, path, value))

    # Writers: format, text, headers and the 16-record block boundaries.
    cases.append(case('write-save-clean', [load(), dict(op='save')]))
    cases.append(case('write-one', [load(), add()]))
    cases.append(case('write-one-reload', [load(), add(), dict(op='unload'), load(), dict(op='read_text', index=0)]))
    cases.append(case('write-optionals',
                      [load(BOOK_A, b'Book A', b'Author A'), add(b'alpha', start=3, end=7, pages=2, sig=0x12345678,
                                                          startOffset=11, endOffset=99, para=65535),
                       add('가나다'.encode(), chapter='제 1 장'.encode(), spine=5, words=3), dict(op='sync')]))
    cases.append(case('many-33', [load(), *[add(str(index).encode()) for index in range(33)]],
                      io=False, dump=3))
    cases.append(case('many-33-reload',
                      [load(), *[add(str(index).encode()) for index in range(33)], dict(op='unload'), load()],
                      io=False, dump=4))
    cases.append(case('limit-256', [load(), *[add(b'c') for _ in range(256)], add(b'overflow'),
                                    remote(clip(id=b'new'), b'remote'), dict(op='remove', index=255)],
                      io=False, dump=2))
    expect('limit-256', ('ops', -3, 'result'), 1)
    expect('limit-256', ('ops', -3, 'count'), 256)
    expect('limit-256', ('recordCount',), 255)

    cases.append(case('header-title-max', [load(title=b't' * 4096), add()], io=False))
    cases.append(case('header-title-over', [load(title=b't' * 4097), add()], io=False))
    cases.append(case('header-author-over', [load(author=b'a' * 4097), add()], io=False))
    cases.append(case('header-path-over', [load(BOOK_LONG), add()], io=False))
    expect('header-title-max', ('ops', 1, 'ok'), True)
    expect('header-title-over', ('ops', 1, 'ok'), False)
    expect('header-path-over', ('ops', 1, 'ok'), False)
    cases.append(case('header-nonascii', [load(BOOK_UNICODE, '한글 책'.encode(), '작가 é'.encode()),
                                          add(b'text', chapter='장'.encode())]))
    cases.append(case('header-invalid-path', [load(BOOK_INVALID, b'Title', b'Author'), add()]))
    cases.append(case('chapter-truncate',
                      [load(), add(b'one', chapter=b'x' * 46 + '가'.encode()),
                       add(b'two', chapter=b'x' * 47), add(b'three', chapter=b'\xff\xfe' * 30)]))
    cases.append(case('text-empty', [load(), add(b''), dict(op='unload'), load(), dict(op='read_text', index=0),
                                     dict(op='preview', index=0, size=260)]))
    cases.append(case('text-max', [load(), add(('é' * 2048).encode()), dict(op='unload'), load(),
                                   dict(op='read_text', index=0), dict(op='preview', index=0, size=260),
                                   dict(op='preview', index=0, size=259), dict(op='preview', index=0, size=512)]))
    cases.append(case('text-over', [load(), add(b'x' * 4097)]))
    expect('text-over', ('ops', 1, 'result'), 2)
    cases.append(case('text-nul', [load(), add(b'before\x00after'), dict(op='unload'), load(),
                                   dict(op='read_text', index=0)]))
    cases.append(case('text-invalid-utf8', [load(), add(b'\xff\xfe\xc0\xaf\xed\xa0\x80'),
                                            dict(op='unload'), load(), dict(op='read_text', index=0),
                                            dict(op='preview', index=0, size=260)]))
    cases.append(case('timestamps', [load(), add(b'a', **dict(now=0)), add(b'b', **dict(now=1577836800)),
                                     add(b'c', **dict(now=1577836801))], now=1700000000))

    # Readers: canonical v4 bytes, truncated and mutated variants, and migrations.
    cases.append(case('read-v4', [load(), dict(op='read_text', index=0), dict(op='read_text', index=1),
                                  dict(op='preview', index=1, size=260)], files=[
        dict(path=hexbytes(store), bytes=hexbytes(canonical))]))
    expect('read-v4', ('ops', 0, 'ok'), True)
    expect('read-v4', ('recordCount',), 2)
    body = v4_parts(canonical)
    record_two = body + (len(canonical) - body) // 2
    splits = [0, 1, 2, 3, 5, 6, body - 1, body, body + 1, body + 18, body + 22, body + 26, body + 34, body + 99,
              body + 152, body + 153, body + 154, record_two - 1, len(canonical) - 1]
    for length in sorted(set(splits)):
        cases.append(case(f'read-v4-truncated-{length}', [load()],
                          files=[dict(path=hexbytes(store), bytes=hexbytes(canonical[:length]))]))
        expect(f'read-v4-truncated-{length}', ('ops', 0, 'ok'), False)
    mutations = {
        'version-zero': (0, 0),
        'version-two': (0, 2),
        'version-five': (0, 5),
        'count-over': (3, 257),
        'pending-nonbool': (body + 40, 0x07),
        'id-not-nul': (body + 105, 0x41),
        'textlength-over': (body + 154, 4097),
        'chapter-not-nul': (body + 153, 0x42),
    }
    for name, (offset, value) in mutations.items():
        blob = bytearray(canonical)
        blob[offset:offset + (2 if name in ('count-over', 'textlength-over') else 1)] = struct.pack(
            '<H' if name in ('count-over', 'textlength-over') else '<B', value)
        cases.append(case(f'read-{name}', [load(), dict(op='read_text', index=0)],
                          files=[dict(path=hexbytes(store), bytes=hexbytes(bytes(blob)))]))
    expect('read-version-zero', ('ops', 0, 'ok'), False)
    expect('read-count-over', ('ops', 0, 'ok'), False)
    expect('read-id-not-nul', ('ops', 0, 'ok'), True)
    expect('read-pending-nonbool', ('ops', 0, 'ok'), True)
    expect('read-pending-nonbool-rewrite', ('records', 0, 'pendingByte'), 7)
    expect('read-textlength-over', ('ops', 0, 'ok'), False)
    expect('read-chapter-not-nul', ('ops', 0, 'ok'), True)
    rewritten = bytearray(canonical)
    rewritten[body + 40] = 0x07
    cases.append(case('read-pending-nonbool-rewrite',
                      [load(), add(b'extra'), dict(op='unload'), load()],
                      files=[dict(path=hexbytes(store), bytes=hexbytes(bytes(rewritten)))]))

    legacy = [dict(spine=1, start=2, end=3, pages=2, startWord=4, endWord=5, words=6, para=7, timestamp=1577836801,
                   chapter=b'Legacy chapter', text=b'legacy one'),
              dict(spine=9, start=10, end=11, pages=1, startWord=0, endWord=0, words=8, para=65535,
                   timestamp=0, sig=0xdeadbeef, chapter=b'', text='다른 텍스트'.encode())]
    for version in (1, 3):
        cases.append(case(f'read-v{version}-migrate',
                          [load(), dict(op='read_text', index=0), dict(op='read_text', index=1),
                           add(b'new clip'), dict(op='unload'), load()],
                          files=[dict(path=hexbytes(store), bytes=hexbytes(build_legacy(version, legacy)))]))
        expect(f'read-v{version}-migrate', ('ops', 0, 'ok'), True)
        expect(f'read-v{version}-migrate', ('texts', 0, 'text'), b'legacy one'.hex())
    cases.append(case('read-v1-text-over-max', [load(), dict(op='read_text', index=0),
                                                dict(op='preview', index=0, size=260)],
                      files=[dict(path=hexbytes(store), bytes=hexbytes(
                          build_legacy(1, [dict(text=b'x' * 5000)])))]))
    cases.append(case('read-v1-truncated-text', [load()],
                      files=[dict(path=hexbytes(store), bytes=hexbytes(
                          build_legacy(1, [dict(text=b'x' * 100)])[:-90]))]))
    expect('read-v1-truncated-text', ('ops', 0, 'ok'), False)
    cases.append(case('read-v3-text-over-max', [load()],
                      files=[dict(path=hexbytes(store), bytes=hexbytes(
                          build_legacy(3, [dict(text=b'x' * 4097)])))]))
    expect('read-v3-text-over-max', ('ops', 0, 'ok'), False)
    cases.append(case('read-missing-store', [load(), dict(op='read_text', index=0), add()]))
    cases.append(case('read-invalid-type', [load(book_type=b'txt')]))
    cases.append(case('read-empty-path', [load(path=b'')]))
    expect('read-invalid-type', ('ops', 0, 'ok'), False)
    expect('read-empty-path', ('ops', 0, 'ok'), False)
    cases.append(case('read-open-fail', [load()], files=[dict(path=hexbytes(store), bytes=hexbytes(canonical))],
                      failures={'open': 1}))
    expect('read-open-fail', ('ops', 0, 'ok'), False)
    cases.append(case('read-short-read', [load()], files=[dict(path=hexbytes(store), bytes=hexbytes(canonical))],
                      short={'read': {'at': 2, 'bytes': 1}}))
    expect('read-short-read', ('ops', 0, 'ok'), False)
    cases.append(case('read-bak-recovery', [load(), dict(op='read_text', index=0)],
                      files=[dict(path=hexbytes(store + b'.bak'), bytes=hexbytes(canonical))]))
    expect('read-bak-recovery', ('ops', 0, 'ok'), True)
    cases.append(case('read-bak-recovery-fail', [load()],
                      files=[dict(path=hexbytes(store + b'.bak'), bytes=hexbytes(canonical))],
                      failures={'rename': 1}))
    expect('read-bak-recovery-fail', ('ops', 0, 'ok'), False)
    cases.append(case('read-bak-and-store', [load(), dict(op='read_text', index=0)],
                      files=[dict(path=hexbytes(store), bytes=hexbytes(canonical)),
                             dict(path=hexbytes(store + b'.bak'), bytes=hexbytes(b'stale'))]))
    cases.append(case('read-exists-fail', [load()], files=[dict(path=hexbytes(store), bytes=hexbytes(canonical))],
                      failures={'exists': 1}))
    expect('read-exists-fail', ('ops', 0, 'ok'), True)
    cases.append(case('read-dirty-unload', [load(), add(), dict(op='unload'), dict(op='read_text', index=0)]))
    expect('read-dirty-unload', ('ops', 3, 'ok'), False)

    # Write transactions, rollback and injected physical failures.
    sidecars = dict(files=[dict(path=hexbytes(store), bytes=hexbytes(canonical)),
                           dict(path=hexbytes(store + b'.bak'), bytes=hexbytes(b'old backup')),
                           dict(path=hexbytes(store + b'.tmp'), bytes=hexbytes(b'old temporary'))])
    cases.append(case('write-first', [load(), add()]))
    cases.append(case('write-existing', [load(), add()], **copy.deepcopy(sidecars)))
    cases.append(case('write-tmp-present', [load(), add()],
                      files=[dict(path=hexbytes(store + b'.tmp'), bytes=hexbytes(b'stale temporary'))]))
    cases.append(case('write-backup-rename-fail', [load(), add()], **copy.deepcopy(sidecars),
                      failures={'rename': 1}))
    expect('write-backup-rename-fail', ('ops', 1, 'ok'), False)
    cases.append(case('write-tmp-rename-fail', [load(), add()], **copy.deepcopy(sidecars),
                      failures={'rename': 2}))
    expect('write-tmp-rename-fail', ('ops', 1, 'ok'), False)
    cases.append(case('write-rename-rollback-fail', [load(), add()], **copy.deepcopy(sidecars),
                      failures={'rename': [2, 3]}))
    expect('write-rename-rollback-fail', ('ops', 1, 'ok'), False)
    for label, plan in [('short-header', {'short': {'write': {'at': 1, 'bytes': 0}}}),
                        ('short-record', {'short': {'write': {'at': 7, 'bytes': 2}}}),
                        ('short-text', {'short': {'write': {'at': 24, 'bytes': 0}}}),
                        ('open-fail', {'failures': {'open': 3}}),
                        ('source-open-fail', {'failures': {'open': 2}}),
                        ('seek-fail', {'failures': {'seek': 3}}),
                        ('mkdir-fail', {'failures': {'mkdir': [1, 2]}}),
                        ('close-fail', {'failures': {'close': 2}}),
                        ('flush-fail', {'failures': {'flush': 1}}),
                        ('exists-fail', {'failures': {'exists': 2}}),
                        ('read-back-fail', {'failures': {'read': 43}})]:
        cases.append(case(f'write-{label}', [load(), add(b'two')], **copy.deepcopy(sidecars), **plan))
    cases.append(case('write-remove-tmp-fail', [load(), add()], **copy.deepcopy(sidecars),
                      short={'write': {'at': 1, 'bytes': 0}}, failures={'remove': 3}))
    cases.append(case('write-replacement-text',
                      [load(), add(b'alpha'), add(b'beta'), remote(clip(id=b'', textLength=4, pending=False),
                                                                    b'replaced!')], **copy.deepcopy(sidecars)))
    cases.append(case('write-remove-last', [load(), add(b'alpha'), dict(op='remove', index=0), dict(op='unload'),
                                            load()]))

    # Deletion journal, tombstones and recovery.
    cases.append(case('delete-with-id', [load(), add(b'one'), add(b'two'), dict(op='sync'),
                                         dict(op='remove', index=0)]))
    cases.append(case('delete-without-id', [load(), add(b'one'), add(b'two'), dict(op='remove', index=0)]))
    cases.append(case('delete-index-out-of-range', [load(), add(b'one'), dict(op='remove', index=4)]))
    expect('delete-index-out-of-range', ('ops', 2, 'ok'), False)
    cases.append(case('delete-journal-open-fail', [load(), add(b'one'), dict(op='sync'),
                                                   dict(op='remove', index=0)], failures={'open': 4}))
    expect('delete-journal-open-fail', ('ops', 3, 'ok'), False)
    cases.append(case('delete-journal-short-write', [load(), add(b'one'), dict(op='sync'),
                                                     dict(op='remove', index=0)],
                      short={'write': {'at': 49, 'bytes': 4}}))
    expect('delete-journal-short-write', ('ops', 3, 'ok'), False)
    cases.append(case('delete-journal-short-write-rollback-fail',
                      [load(), add(b'one'), dict(op='sync'), dict(op='remove', index=0)],
                      short={'write': {'at': 49, 'bytes': 4}}, failures={'truncate': 1}))
    expect('delete-journal-short-write-rollback-fail', ('ops', 3, 'ok'), False)
    cases.append(case('delete-store-write-fail', [load(), add(b'one'), dict(op='sync'), dict(op='remove', index=0)],
                      short={'write': {'at': 50, 'bytes': 0}}))
    expect('delete-store-write-fail', ('ops', 3, 'ok'), False)
    journal = u8(0) * 65
    cases.append(case('journal-tail-recovery', [load(), dict(op='next_deletion', offset=0)],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal + b'x'))]))
    cases.append(case('journal-tail-truncate-fail', [load(), dict(op='next_deletion', offset=0)],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal + b'x'))],
                      failures={'truncate': 1}))
    cases.append(case('journal-misaligned-offset', [load(), dict(op='next_deletion', offset=1)],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal))]))
    cases.append(case('journal-read-fail', [load(), dict(op='next_deletion', offset=0)],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal))],
                      failures={'read': 1}))
    cases.append(case('journal-skip-acknowledged',
                      [load(), dict(op='next_deletion', offset=0), dict(op='next_deletion', offset=65)],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal * 2))]))
    cases.append(case('journal-open-fail', [load(), dict(op='next_deletion', offset=0)],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal))],
                      failures={'open': 1}))
    cases.append(case('journal-missing', [load(), dict(op='next_deletion', offset=0)]))
    cases.append(case('finish-deletions', [load(), dict(op='finish_deletions')],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal))]))
    cases.append(case('finish-deletions-fail', [load(), dict(op='finish_deletions')],
                      files=[dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal))],
                      failures={'remove': 1}))
    cases.append(case('finish-deletions-missing', [load(), dict(op='finish_deletions')]))

    # Sync and remote application.
    cases.append(case('sync-assigns-ids', [load(), add(b'one'), add(b'two'), dict(op='sync'), dict(op='sync')]))
    cases.append(case('sync-preserves-existing', [load(), add(b'one'), dict(op='sync'), add(b'two'), dict(op='sync')]))
    cases.append(case('uploads-clears-pending', [load(), add(b'one'), dict(op='sync'), dict(op='uploads'),
                                                 dict(op='uploads')]))
    cases.append(case('remote-new', [load(), remote(clip(id=b'r1', revision=7), b'remote text')]))
    cases.append(case('remote-new-write-fail', [load(), remote(clip(id=b'r1', revision=7), b'remote text')],
                      short={'write': {'at': 2, 'bytes': 0}}))
    expect('remote-new-write-fail', ('ops', 1, 'ok'), False)
    first, second = synced_records[0]['id'], synced_records[1]['id']
    existing = dict(files=[dict(path=hexbytes(store), bytes=hexbytes(synced))])
    cases.append(case('remote-unchanged', [load(), remote(clip(id=first, revision=0, pending=False), b'x')],
                      **copy.deepcopy(existing)))
    expect('remote-unchanged', ('ops', 1, 'ok'), True)
    cases.append(case('remote-update', [load(), remote(clip(id=first, revision=1, pending=False), b'replacement')],
                      **copy.deepcopy(existing)))
    expect('remote-update', ('ops', 1, 'ok'), True)
    cases.append(case('remote-delete', [load(), remote(clip(id=first, revision=1), b'', deleted=True)],
                      **copy.deepcopy(existing)))
    expect('remote-delete', ('recordCount',), 1)
    cases.append(case('remote-delete-write-fail', [load(), remote(clip(id=first, revision=1), b'', deleted=True)],
                      **copy.deepcopy(existing), short={'write': {'at': 2, 'bytes': 0}}))
    expect('remote-delete-write-fail', ('ops', 1, 'ok'), False)
    expect('remote-delete-write-fail', ('recordCount',), 2)
    cases.append(case('remote-delete-missing', [load(), remote(clip(id=b'absent'), b'', deleted=True)],
                      **copy.deepcopy(existing)))
    cases.append(case('remote-second-record', [load(), remote(clip(id=second, revision=2, pending=False), b'x')],
                      **copy.deepcopy(existing)))
    cases.append(case('remote-text-over', [load(), remote(clip(id=b'long'), b'x' * 4097)],
                      **copy.deepcopy(existing)))
    expect('remote-text-over', ('ops', 1, 'ok'), False)
    cases.append(case('remote-not-loaded', [remote(clip(id=b'r1'), b'text')]))
    expect('remote-not-loaded', ('ops', 0, 'ok'), False)

    # File moves, deletion of every sidecar, and rename rules.
    def move(source, target):
        return dict(op='move_book', **{'from': hexbytes(source), 'to': hexbytes(target)})

    cases.append(case('move-file', [move(BOOK_A, BOOK_B)], files=[
        dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book')),
        dict(path=hexbytes(store), bytes=hexbytes(canonical)),
        dict(path=hexbytes(store + b'.bak'), bytes=hexbytes(b'bak')),
        dict(path=hexbytes(store + b'.tmp'), bytes=hexbytes(b'tmp')),
        dict(path=hexbytes(store + b'.deleted'), bytes=hexbytes(journal)),
        dict(path=hexbytes(store + b'.migrate.bak'), bytes=hexbytes(b'migrate'))]))
    cases.append(case('move-file-refuses-existing', [move(BOOK_A, BOOK_B)],
                      files=[dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(store), bytes=hexbytes(canonical)),
                             dict(path=hexbytes(store_b), bytes=hexbytes(b'other'))]))
    expect('move-file-refuses-existing', ('ops', 0, 'ok'), False)
    cases.append(case('move-same-path', [move(BOOK_A, BOOK_A)],
                      files=[dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(store), bytes=hexbytes(canonical))]))
    cases.append(case('move-folder', [move(b'/books', b'/renamed')],
                      dirs=[hexbytes(b'/books'), hexbytes(b'/books/sub')],
                      files=[dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(BOOK_C), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(store), bytes=hexbytes(canonical)),
                             dict(path=hexbytes(store_c), bytes=hexbytes(canonical)),
                             dict(path=hexbytes(store_c + b'.deleted'), bytes=hexbytes(journal)),
                             dict(path=hexbytes(b'/books/notes.txt'), bytes=hexbytes(b'not a store'))]))
    cases.append(case('move-folder-check-fail', [move(b'/books', b'/renamed')],
                      dirs=[hexbytes(b'/books')],
                      files=[dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(store), bytes=hexbytes(canonical)),
                             dict(path=hexbytes(stores[BOOK_RENAMED_ALPHA]), bytes=hexbytes(b'occupied'))]))
    expect('move-folder-check-fail', ('ops', 0, 'ok'), False)
    cases.append(case('move-folder-rename-fail', [move(b'/books', b'/renamed')],
                      dirs=[hexbytes(b'/books'), hexbytes(b'/books/sub')],
                      files=[dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(BOOK_C), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(store), bytes=hexbytes(canonical)),
                             dict(path=hexbytes(store_c), bytes=hexbytes(canonical))], failures={'rename': 3}))
    expect('move-folder-rename-fail', ('ops', 0, 'ok'), False)
    cases.append(case('move-folder-rollback-fail', [move(b'/books', b'/renamed')],
                      dirs=[hexbytes(b'/books'), hexbytes(b'/books/sub')],
                      files=[dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(BOOK_C), bytes=hexbytes(b'book')),
                             dict(path=hexbytes(store), bytes=hexbytes(canonical)),
                             dict(path=hexbytes(store_c), bytes=hexbytes(canonical))],
                      failures={'rename': [3, 4]}))
    expect('move-folder-rollback-fail', ('ops', 0, 'ok'), False)
    cases.append(case('move-file-no-store', [move(BOOK_A, BOOK_B)],
                      files=[dict(path=hexbytes(BOOK_A), bytes=hexbytes(b'book'))]))
    cases.append(case('delete-for-path', [dict(op='delete_for_path', path=hexbytes(BOOK_A), type=hexbytes(b'epub'))],
                      files=[dict(path=hexbytes(store + suffix), bytes=hexbytes(b'x')) for suffix in
                             (b'', b'.bak', b'.tmp', b'.deleted', b'.migrate.bak')]))
    cases.append(case('delete-for-path-fail', [dict(op='delete_for_path', path=hexbytes(BOOK_A), type=hexbytes(b'epub'))],
                      files=[dict(path=hexbytes(store + suffix), bytes=hexbytes(b'x')) for suffix in
                             (b'', b'.bak', b'.tmp', b'.deleted', b'.migrate.bak')], failures={'remove': 3}))
    expect('delete-for-path-fail', ('ops', 0, 'ok'), False)
    cases.append(case('delete-for-path-missing',
                      [dict(op='delete_for_path', path=hexbytes(BOOK_A), type=hexbytes(b'epub'))]))

    # My Clippings.txt export: text, truncation and rollback.
    cases.append(case('export-new', [export()]))
    cases.append(case('export-append', [export()], files=[dict(path=hexbytes(export_path), bytes=hexbytes(b'old line\n'))]))
    cases.append(case('export-truncate-2000', [export(('é' * 2048).encode())]))
    cases.append(case('export-truncate-midcodepoint', [export(b'a' + '가'.encode() * 1400)]))
    cases.append(case('export-invalid-utf8', [export(b'\xff' * 2100)]))
    cases.append(case('export-empty-chapter', [export(chapter=b'')]))
    cases.append(case('export-clock-unavailable', [export()], clock=dict(available=False)))
    cases.append(case('export-clock-year-low', [export()], clock=dict(year=99)))
    cases.append(case('export-clock-year-high', [export()], clock=dict(year=200)))
    cases.append(case('export-am', [export()], clock=dict(hour=0, minute=5, weekday=0)))
    cases.append(case('export-pm', [export()], clock=dict(hour=12, minute=59, weekday=6)))
    cases.append(case('export-late', [export()], clock=dict(hour=23, minute=0, weekday=2)))
    cases.append(case('export-short-write', [export()], files=[dict(path=hexbytes(export_path), bytes=hexbytes(b'old\n'))],
                      short={'write': {'at': 1, 'bytes': 10}}))
    expect('export-short-write', ('ops', 0, 'ok'), False)
    cases.append(case('export-short-write-rollback-fail', [export()],
                      files=[dict(path=hexbytes(export_path), bytes=hexbytes(b'old\n'))],
                      short={'write': {'at': 1, 'bytes': 10}}, failures={'truncate': 1}))
    expect('export-short-write-rollback-fail', ('ops', 0, 'ok'), False)
    cases.append(case('export-open-fail', [export()], failures={'open': 1}))
    cases.append(case('export-close-fail', [export()], files=[dict(path=hexbytes(export_path),
                                                                   bytes=hexbytes(b'old\n'))],
                      failures={'close': 1}))
    return cases, checks


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


def golden_bytes(cases):
    return (json.dumps(cases, indent=2) + '\n').encode()


def read_golden(path):
    """Frozen results are compared decompressed; the archive is deterministic."""
    return json.loads(gzip.decompress(path.read_bytes()))


def compare(expected, actual):
    assert expected.keys() == actual.keys(), 'case inventory changed'
    for name in expected:
        assert expected[name] == actual[name], f'clipping byte/operation mismatch: {name}'


def lookup(row, path):
    value = row
    for key in path:
        value = value[key]
    return value


def run_oracle(binary, cases):
    result = subprocess.run([str(binary)], input=''.join(json.dumps(row) + '\n' for row in cases),
                            capture_output=True, text=True, check=True)
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == len(cases), 'oracle row count'
    return rows


def probe_store_paths(binary, paths):
    """Ask production for each book's store path by writing one clipping."""
    cases = [dict(id=hexbytes(path), io=False, files=[], ops=[load(path), add(b'p')]) for path in paths]
    stores = {}
    for path, row in zip(paths, run_oracle(binary, cases)):
        found = [unhex(file['path']) for file in row['files'] if unhex(file['path']).endswith(b'.bin')]
        assert len(found) == 1, (path, row['files'])
        stores[path] = found[0]
    return stores


def collect_sources(arduinojson):
    sources = {name: sha((ROOT / name).read_bytes()) for name in PRODUCTION}
    for extra in ('test/clipping_bytes/run.py', 'test/clipping_bytes/Oracle.cpp'):
        sources[extra] = sha((ROOT / extra).read_bytes())
    for stub in sorted((HERE / 'stubs').rglob('*')):
        if stub.is_file():
            sources[str(stub.relative_to(ROOT))] = sha(stub.read_bytes())
    sources['arduinojson'] = sha(json.dumps({str(path.relative_to(arduinojson)): sha(path.read_bytes())
                                             for path in sorted(arduinojson.rglob('*')) if path.is_file()},
                                            sort_keys=True).encode())
    return sources


def build(build_dir, arduinojson, archive):
    build_dir.mkdir(parents=True, exist_ok=True)
    sdk = ROOT / 'freeink-sdk'
    assert subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip() == SDK
    manifest = dict(head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    sdk=SDK, sources=collect_sources(arduinojson), extraction=[])
    cpp = sdk / 'libs/hardware/SDCardManager/src/SDCardManager.cpp'
    header = sdk / 'libs/hardware/SDCardManager/include/SDCardManager.h'
    extractions = [
        (cpp, 'bool SDCardManager::openFileForRead(const char* moduleName, const char* path, FsFile& file)',
         build_dir / 'SdkStorage.cpp', ''),
        (header, 'FsFile open(const char* path, const oflag_t oflag = O_RDONLY)', build_dir / 'SdkInline.inc', ''),
        (header, 'bool mkdir(const char* path, const bool pFlag = true)', build_dir / 'SdkInline.inc', ''),
        (header, 'bool exists(const char* path)', build_dir / 'SdkInline.inc', ''),
        (header, 'bool remove(const char* path)', build_dir / 'SdkInline.inc', ''),
        (header, 'bool rename(const char* path, const char* newPath)', build_dir / 'SdkInline.inc', ''),
    ]
    pieces = {}
    for source, signature, output, prefix in extractions:
        pinned = subprocess.check_output(['git', '-C', str(sdk), 'show', SDK + ':' + str(source.relative_to(sdk))])
        assert source.read_bytes() == pinned, f'SDK source differs from pinned {SDK}: {source}'
        original = pinned.decode()
        body, start, end = extract(original, signature)
        manifest['extraction'].append(dict(source=str(source.relative_to(sdk)), file_sha256=sha(source.read_bytes()),
                                           signature=signature, start_byte=len(original[:start].encode()),
                                           end_byte=len(original[:end].encode()), sha256=sha(body.encode())))
        pieces.setdefault(output, []).append(body)
    for output, bodies in pieces.items():
        prefix = '#include <Arduino.h>\n#include <HalStorage.h>\n' if output.name == 'SdkStorage.cpp' else ''
        output.write_text(prefix + '\n\n'.join(bodies) + '\n')

    # ArduinoJson is only the case I/O format here, never the store's serializer.
    # String lengths must exceed its 2-byte default because one frozen store holds
    # 256 records, so its hex image is longer than 65535 characters.
    flags = ['-DARDUINOJSON_ENABLE_ARDUINO_STRING=0', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
             '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', '-DARDUINOJSON_ENABLE_PROGMEM=0',
             '-DARDUINOJSON_STRING_LENGTH_SIZE=4']
    include = [HERE / 'stubs', build_dir, ROOT, ROOT / 'src', ROOT / 'lib', ROOT / 'lib/hal',
               ROOT / 'lib/Memory', ROOT / 'lib/Serialization', ROOT / 'lib/Utf8', ROOT / 'rust/crates/xp-ffi/include',
               arduinojson]
    common = ['c++', '-std=c++20', '-O2', '-ffp-contract=off', '-ffunction-sections', '-fdata-sections', *flags,
              *['-I' + str(path) for path in include]]
    units = [
        (ROOT / 'test/clipping_bytes/Oracle.cpp', []),
        (ROOT / 'src/ClippingStore.cpp', ['-include', str(HERE / 'stubs/clip_clock.h')]),
        (ROOT / 'src/clippings/ClippingsManager.cpp', []),
        (ROOT / 'lib/Utf8/Utf8.cpp', []),
        (build_dir / 'SdkStorage.cpp', []),
        (ROOT / 'test/cmake/xp_ffi_host.cpp', []),
    ]
    objects = []
    commands = []
    for source, extra in units:
        target = build_dir / (source.stem + '.o')
        command = [*common, *extra, '-c', str(source), '-o', str(target)]
        commands.append(command)
        subprocess.run(command, check=True)
        objects.append(target)
    link = ['c++', *[str(obj) for obj in objects], str(archive),
            '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-o', str(build_dir / 'oracle')]
    commands.append(link)
    subprocess.run(link, check=True)
    manifest['commands'] = commands
    manifest['compiler'] = subprocess.check_output(['c++', '--version'], text=True)
    manifest['configuration'] = sorted(line for line in subprocess.check_output(
        ['c++', '-std=c++20', *flags, *['-I' + str(path) for path in include], '-dM', '-E', '-x', 'c++',
         '-include', 'ArduinoJson.h', '/dev/null'], text=True).splitlines() if line.startswith('#define ARDUINOJSON_'))
    manifest['arduinojson_release'] = '7.4.2'
    manifest['archive_sha256'] = sha(archive.read_bytes())
    manifest['substitutions'] = {
        'storage': 'stubs/HalStorage.h models the SD volume; SDCardManager methods are extracted from the pinned SDK',
        'logging': 'stubs/Logging.h disables LOG_*',
        'clock': 'stubs/clip_clock.h substitutes time() for ClippingStore.cpp; stubs/HalClock.h for ClippingsManager',
        'randomness': 'stubs/esp_random.h replaces esp_fill_random with a fixed xorshift32 stream',
        'allocator': 'real host new; allocation failure and layout are not modelled',
    }
    return manifest, build_dir / 'oracle'


def find_file(row, path):
    for file in row['files']:
        if unhex(file['path']) == path:
            return file
    raise AssertionError(f'missing file {path!r}')


def set_file(row, path, data):
    find_file(row, path)['bytes'] = hexbytes(data)


def mutate_file(row, path, old, new):
    data = unhex(find_file(row, path)['bytes'])
    assert old in data, 'control byte sequence absent'
    set_file(row, path, data.replace(old, new, 1))


def swap_record_fields(row, path):
    data = bytearray(unhex(find_file(row, path)['bytes']))
    body = v4_parts(bytes(data))
    data[body + 2:body + 4], data[body + 4:body + 6] = data[body + 4:body + 6], data[body + 2:body + 4]
    set_file(row, path, bytes(data))


def set_u16(row, path, offset, value):
    data = bytearray(unhex(find_file(row, path)['bytes']))
    data[offset:offset + 2] = struct.pack('<H', value)
    set_file(row, path, bytes(data))


def drop_tombstone(row, path):
    file = find_file(row, path)
    data = unhex(file['bytes'])
    assert len(data) >= 65, 'tombstone journal too short'
    file['bytes'] = hexbytes(data[:-65])


def check_negative_controls(actual, expected, store):
    """Every deliberate mutation must be rejected by the comparator."""
    controls = {
        'byte': ('write-one', lambda row: mutate_file(row, store, b'one', b'two')),
        'field-order': ('write-optionals', lambda row: swap_record_fields(row, store)),
        'missing-tombstone': ('delete-with-id', lambda row: drop_tombstone(row, store + b'.deleted')),
        'truncation-length': ('write-one', lambda row: set_u16(
            row, store, v4_parts(unhex(find_file(row, store)['bytes'])) + 154, 2)),
    }
    for label, (name, mutate) in controls.items():
        mutant = copy.deepcopy(actual)
        mutate(mutant[name])
        try:
            compare(expected, mutant)
        except AssertionError as error:
            print(f'Negative control {label}: rejected ({error})')
        else:
            raise AssertionError(f'Comparator accepted {label} mutation')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--arduinojson', type=pathlib.Path, required=True)
    parser.add_argument('--archive', type=pathlib.Path, required=True)
    parser.add_argument('--golden', type=pathlib.Path, default=HERE / 'golden.json.gz')
    parser.add_argument('--capture-baseline', action='store_true')
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    manifest, binary = build(args.build, args.arduinojson, args.archive)

    stores = probe_store_paths(binary, [BOOK_A, BOOK_B, BOOK_C, BOOK_RENAMED, BOOK_RENAMED_ALPHA, BOOK_UNICODE,
                                        BOOK_INVALID])
    (args.build / 'stores.json').write_text(json.dumps({hexbytes(key): value.hex() for key, value in stores.items()},
                                                       indent=2) + '\n')
    manifest['store_paths'] = {hexbytes(key): value.hex() for key, value in stores.items()}
    manifest['store_path_note'] = ('Derived by production from std::hash<std::string>; the values are host libc++ '
                                   'hashes and do not prove the C3/S3 libstdc++ hash')

    def build_store(name, ops):
        row = run_oracle(binary, [case(name, [load(BOOK_A, b'Book A', b'Author A'), *ops])])[0]
        blobs = [unhex(file['bytes']) for file in row['files'] if unhex(file['path']) == stores[BOOK_A]]
        assert len(blobs) == 1, row['files']
        return blobs[0]

    canonical = build_store('canonical', [add(b'alpha text'), add('가나다'.encode()), dict(op='sync')])
    synced = build_store('synced', [add(b'alpha text'), add('가나다'.encode()), dict(op='sync'),
                                    dict(op='uploads')])
    synced_records = read_records(synced)
    (args.build / 'canonical.bin').write_bytes(canonical)
    (args.build / 'synced.bin').write_bytes(synced)
    manifest['canonical_records'] = [dict(id=record['id'].hex(), revision=record['revision'],
                                          pending=record['pending'], text=record['text'].hex())
                                     for record in read_records(canonical)]

    cases, checks = corpus(stores, canonical, synced, synced_records)
    manifest['corpus_sha256'] = sha(json.dumps(cases, sort_keys=True).encode())
    (args.build / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (args.build / 'cases.json').write_text(json.dumps(cases, indent=2) + '\n')
    rows = run_oracle(binary, cases)
    actual = {row['id']: row for row in rows}
    for name, path, value in checks:
        assert lookup(actual[name], path) == value, f'{name}: expected {value} at {path}'
    for row, case_row in zip(rows, cases):
        faults = dict(case_row.get('failures', {}))
        for operation, attempts in faults.items():
            for attempt in (attempts if isinstance(attempts, list) else [attempts]):
                assert any(event.startswith(operation + ':') and event.endswith(':fail')
                           for event in row.get('io', [])), f'Unreached failure: {row["id"]} {operation} {attempt}'
    (args.build / 'actual.json').write_text(json.dumps(actual, indent=2) + '\n')
    for name, row in actual.items():
        dest = args.build / 'raw' / name
        dest.mkdir(parents=True, exist_ok=True)
        for index, file in enumerate(row['files']):
            (dest / f'{index}.bin').write_bytes(unhex(file['bytes']))
            (dest / f'{index}.path').write_bytes(unhex(file['path']))
    if args.capture_baseline:
        assert manifest['head'] == BASELINE, 'Only the approved native parent may capture goldens'
        subprocess.run(['git', 'diff', '--exit-code', BASELINE, '--', *PRODUCTION], cwd=ROOT, check=True)
        if args.golden.exists():
            frozen = read_golden(args.golden)
            compare(frozen, {name: actual[name] for name in frozen})
            assert frozen.keys() < actual.keys(), 'Only new baseline cases may be appended; frozen cases cannot change'
        args.golden.write_bytes(gzip.compress(golden_bytes(actual), mtime=0))
    expected = read_golden(args.golden)
    compare(expected, actual)
    if args.negative_controls:
        check_negative_controls(actual, expected, stores[BOOK_A])
    print(f'{len(cases)} clipping cases passed')


if __name__ == '__main__':
    main()

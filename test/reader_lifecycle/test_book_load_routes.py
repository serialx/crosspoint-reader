"""Complete TXT/Markdown/protection loads and the shared query cap."""
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
from test_book_load import ROOT, QUERY_CAP, build, capture, load_trace, persisted_files, restore, values


def main():
    output = Path(os.environ.get('XP_BOOK_LOAD_ROUTES_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-load-routes-'))
    output.mkdir(parents=True, exist_ok=True)
    binary, command, _ = build(output)
    results = []

    def pair(case, path, seed, stage, build_missing=True, skip=0, env=None):
        logs, files, queries = [], [], []
        for mode in ('native', 'rust'):
            restore(case / 'cache', seed)
            log, query = capture(binary, case, mode, skip, 0, case / f'{stage}-{mode}.log',
                                 source_path=path, build_missing=build_missing, env=env)
            logs.append(log)
            queries.append(query)
            files.append(persisted_files(case / 'cache'))
        record = dict(case=case.name, stage=stage, build_missing=build_missing, skip_css=skip, env=env or {},
                      trace_equal=load_trace(logs[0]) == load_trace(logs[1]),
                      values_equal=values(logs[0]) == values(logs[1]), files_equal=files[0] == files[1],
                      queries_equal=queries[0] == queries[1], queries=queries)
        results.append(record)
        (output / 'results.json').write_text(json.dumps(dict(command=command, cases=results), indent=2) + '\n')
        assert all(record[key] for key in ('trace_equal', 'values_equal', 'files_equal', 'queries_equal')), record
        return files[0]

    for index, (name, data, cover) in enumerate([
        ('plain.txt', b'text', None), ('title.MD', b'# markdown\n', None),
        ('e\u0301.txt', b'x' * 8192, 'cover.jpg'), ('empty.md', b'', None), ('missing.txt', None, None),
    ]):
        case = output / f'text-{index}'
        case.mkdir(exist_ok=True)
        if data is not None: (case / name).write_bytes(data)
        if cover: (case / cover).write_bytes(b'cover')
        for skip in (0, 1):
            pair(case, name, {}, f'missing-no-build-{skip}', build_missing=False, skip=skip)
            seed = pair(case, name, {}, f'cold-{skip}', skip=skip)
            pair(case, name, seed, f'warm-missing-html-{skip}', skip=skip)
            if data is not None:
                tag = b'<!-- MD_CACHE_VERSION: 1 -->' if name.lower().endswith('.md') else b'<!-- TXT_CACHE_VERSION: 1 -->'
                valid = {**seed, 'html/0.html': (tag + b'\n<html></html>').hex()}
                pair(case, name, valid, f'warm-tag-{skip}', skip=skip)
    case = output / 'protected'
    case.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'test/epubs/crosspoint-table-edge-cases.epub', case / 'source.epub')
    for index, env in enumerate([{'BOOK_KEY_ERROR': '1'}, {'BOOK_PROTECTED': '1'},
                                 {'BOOK_PROTECTED': '1', 'BOOK_KEY_ERROR': '1'}]):
        for skip in (0, 1): pair(case, 'source.epub', {}, f'{index}-{skip}', skip=skip, env=env)
    original = (ROOT / 'test/epubs/crosspoint-table-edge-cases.epub').read_bytes()
    for name, data in [('missing', None), ('empty', b''), ('truncated', original[:-37]),
                       ('invalid', b'not an EPUB archive')]:
        case = output / f'source-{name}'
        case.mkdir(exist_ok=True)
        if data is not None: (case / 'source.epub').write_bytes(data)
        for skip in (0, 1): pair(case, 'source.epub', {}, f'failure-{skip}', skip=skip)
    case = output / 'query-cap'
    case.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'test/epubs/crosspoint-table-edge-cases.epub', case / 'source.epub')
    count = QUERY_CAP + 4
    header = struct.pack('<BIHH', 10, 29, count, 0) + bytes(20)
    start = 29 + 4 * count
    book = header + b''.join(struct.pack('<I', start + 10 * index) for index in range(count))
    book += struct.pack('<IIh', 0, 0, -1) * count
    pair(case, 'source.epub', {'book.bin': book.hex()}, 'capped', skip=1)
    assert all(query['cap_hit'] for query in results[-1]['queries'])
    print('52 alternate/source-failure native/Rust loads and one shared-cap comparison pass.')


if __name__ == '__main__': main()

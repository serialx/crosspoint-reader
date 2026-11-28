"""Keep historical identity-path Pages and real-parent image bytes independently checked."""
import argparse
import copy
from functools import lru_cache
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
HISTORICAL = tuple(f'test_tables/public/{config}' for config in range(4))
REFERENCE = json.loads(gzip.decompress((HERE / 'page_image_parent.json.gz').read_bytes()))


@lru_cache(maxsize=1)
def v73_reference():
    return json.loads(gzip.decompress((ROOT / 'test/page_golden/v73-changes.json.gz').read_bytes()))


def layout_pages(pages, update):
    wanted = (f'{spine}/{index}' for spine, count in update['chapters'].items() for index in range(count))
    return {key: update['pages'][key] if key in update['pages'] else pages[key] for key in wanted}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check_parent_case(key, records, cache):
    """Compare all 28 image cases to the real parent's complete Pages and cache files."""
    if key not in REFERENCE['cases']:
        return
    expected = REFERENCE['cases'][key]
    pages = {name: {field: page[field] for field in
                   ('raw_bytes', 'raw_sha256', 'wire_bytes', 'wire_sha256')}
             for name, page in records['pages'].items()}
    reference = v73_reference()
    parent_pages = expected['pages']
    if key in reference['cases']:
        parent_pages = {name: {field: page[field] for field in
                        ('raw_bytes', 'raw_sha256', 'wire_bytes', 'wire_sha256')}
                        for name, page in layout_pages(parent_pages, reference['cases'][key]).items()}
    if pages != parent_pages:
        raise ValueError(f'Parent production Page bytes mismatch: {key}')
    files = {str(p.relative_to(cache)): sha(p.read_bytes()) for p in cache.rglob('*') if p.is_file()}
    if files != {**expected['cache'], **reference['sections'][key]}:
        raise ValueError(f'Parent production cache mismatch: {key}')


def production_expectation(historical, path):
    """Four historical stub Pages have a separate native Page replay below."""
    if sha(path.read_bytes()) != REFERENCE['historical_golden_sha256']:
        raise ValueError('Historical Page golden changed')
    expected = copy.deepcopy(historical)
    for key in HISTORICAL:
        expected['cases'][key]['pages']['0/0'] = REFERENCE['cases'][key]['historical_replacement']
    for key, update in v73_reference()['cases'].items():
        case = expected['cases'][key]
        case['pages'] = layout_pages(case['pages'], update)
        case['chapters'] = update['chapters']
        case['counts'] = update['counts']
    return expected


def historical_bytes(page):
    # The immutable diagnostic sample contains the entire single-image element.
    elements = [line.split('\t') for line in page['sample'] if line.startswith('element\t')]
    if len(elements) != 1 or elements[0][1] != '2':
        raise ValueError('Historical image fixture shape changed')
    raw = b'\x01\x00\x02' + bytes.fromhex(elements[0][3]) + b'\x00\x00\x00\x00'
    if len(raw) != page['raw_bytes'] or sha(raw) != page['raw_sha256'] or sha(raw) != page['wire_sha256']:
        raise ValueError('Historical image bytes disagree with frozen hashes')
    return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('page_golden', ROOT / 'test/page_golden/run.py')
    page = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(page)
    golden = ROOT / 'test/page_golden/golden.json.gz'
    if sha(golden.read_bytes()) != REFERENCE['historical_golden_sha256']:
        raise ValueError('Historical Page golden changed')
    expected = json.loads(gzip.decompress(golden.read_bytes()))
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    font = work / 'public.cpfont'
    font.write_bytes(gzip.decompress((ROOT / 'test/text_golden/fonts/NotoSans_14.cpfont.gz').read_bytes()))
    for key in HISTORICAL:
        config = key.rsplit('/', 1)[-1]
        destination = work / config
        destination.mkdir(exist_ok=True)
        old = expected['cases'][key]['pages']['0/0']
        (destination / 'historical-page.bin').write_bytes(historical_bytes(old))
        records = destination / 'records.tsv'
        with records.open('wb') as output, (destination / 'stderr.log').open('wb') as errors:
            subprocess.run([str(args.binary.resolve()), str(destination), config, str(font),
                            '--historical-image-path'], stdout=output, stderr=errors, check=True)
        actual = page.parse_records(records)
        page.compare({'cases': {key: {'pages': {'0/0': old}}}},
                     {'cases': {key: {'pages': actual['pages']}}})
        print(f'{key}: historical 68-byte image Page, records and pixels unchanged')


if __name__ == '__main__':
    main()

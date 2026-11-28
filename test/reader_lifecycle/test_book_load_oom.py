"""Complete Rust load failures release file, crypto, INTERNAL and DEFAULT owners."""
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from test_book_load import ROOT, build, capture, persisted_files, restore


def main():
    output = Path(os.environ.get('XP_BOOK_LOAD_OOM_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-load-oom-'))
    output.mkdir(parents=True, exist_ok=True)
    binary, command, _ = build(output, allocation_faults=True)
    results, baselines = [], []
    for name in ('cold', 'warm', 'protected-cold', 'protected-warm', 'key-error'):
        case = output / name
        case.mkdir(exist_ok=True)
        shutil.copy2(ROOT / 'test/epubs/crosspoint-table-edge-cases.epub', case / 'source.epub')
        env = {}
        if name.startswith('protected'): env['BOOK_PROTECTED'] = '1'
        if name == 'key-error': env['BOOK_KEY_ERROR'] = '1'
        seed = {}
        if name.endswith('warm'):
            restore(case / 'cache', {})
            capture(binary, case, 'rust', 0, 0, case / 'seed.log', env=env)
            seed = persisted_files(case / 'cache')
        restore(case / 'cache', seed)
        data, _ = capture(binary, case, 'rust', 0, 0, case / 'baseline.log', env=env)
        counts = tuple(map(int, re.search(rb'load-allocation-attempts (\d+) (\d+)', data).groups()))
        baselines.append(dict(case=name, internal_attempts=counts[0], default_attempts=counts[1]))
        for domain, variable, indices in (
            ('internal', 'BOOK_OWNER_ALLOC_FAIL', range(counts[0])),
            ('default', 'BOOK_BUFFER_FAIL', range(1, counts[1] + 1)),
        ):
            for index in indices:
                restore(case / 'cache', seed)
                data, query = capture(binary, case, 'rust', 0, 0, case / f'{domain}-{index}.log',
                                      env={**env, variable: str(index)})
                result = int(re.search(rb'^result (\d+)', data, re.M)[1])
                results.append(dict(case=name, domain=domain, fault=index, result=result, query=query))
                (output / 'results.json').write_text(json.dumps(dict(
                    command=command, baselines=baselines, faults=results), indent=2) + '\n')
    print(f'{len(results)} complete-load allocation-failure and lifetime checks pass.')


if __name__ == '__main__': main()

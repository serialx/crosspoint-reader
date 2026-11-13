"""Compare reader/native decode cleanup under strict file and allocation faults."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('image_run', ROOT / 'run.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def main():
    binary = Path(sys.argv[1]).resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix='reader-image-failure-') as temporary:
        directory = Path(temporary)
        with tarfile.open(ROOT / 'corpus.tar.gz') as archive:
            archive.extractall(directory, filter='data')
        cases = [('none', 0), ('source-open', 0), ('cache-open', 0), ('close', 0)]
        cases += [(kind, index) for kind, count in [('write', 6), ('seek', 3), ('read', 8), ('allocation', 14)]
                  for index in range(count)]
        for name in ['RGBA-37x29.png', 'gif-37x29-0-0.gif', 'gray-37x29.jpg']:
            for target in [0, 28]:
                jpeg_faults = [('heap', 36 * 1024 - 1), ('heap', 36 * 1024),
                               ('native-allocation', 0), ('native-allocation', 1)] if name.endswith('.jpg') else []
                for fault, index in cases + jpeg_faults:
                    pair = []
                    for legacy in [True, False]:
                        env = dict(os.environ, READER_IMAGE_FAULT=fault, READER_IMAGE_FAULT_INDEX=str(index))
                        if legacy:
                            env['READER_IMAGE_LEGACY'] = '1'
                        else:
                            env.pop('READER_IMAGE_LEGACY', None)
                        result = subprocess.run(
                            [str(binary), str(directory / name), str(directory / 'cache.px2'), '2', str(target)],
                            env=env, capture_output=True)
                        assert result.returncode == 0, (name, target, fault, index, legacy,
                                                        result.returncode, result.stderr.decode())
                        actual = oracle.unpack(result.stdout)
                        pair.append({**{key: actual[key] for key in ['frame', 'cache', 'cache_bytes', 'cache_exists']},
                                     'success': actual['status'][15]})
                    assert pair[0] == pair[1], (name, target, fault, index, pair)
                    if fault == 'cache-open':
                        assert pair[1]['success'] == int(not name.endswith('.gif')) and not pair[1]['cache_exists'], pair
                    results.append(dict(image=name, target=target, fault=fault, index=index, **pair[1]))
    binary.parent.joinpath('reader-failure-results.json').write_text(
        json.dumps(dict(cases=len(results), results=results), indent=2) + '\n')
    print(len(results), 'strict-file/allocation differential cases passed')


if __name__ == '__main__':
    main()

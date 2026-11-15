"""Compare native/Rust format, validity and dimensions for existing image fixtures."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
from png_cases import cases, png

ROOT = Path(__file__).resolve().parent


def inputs():
    with tarfile.open(ROOT / 'corpus.tar.gz') as archive:
        corpus = {entry.name: archive.extractfile(entry).read()
                  for entry in archive.getmembers() if entry.isfile()}
    for name, data in sorted(corpus.items()):
        yield 'corpus/' + name, name, data
    for name, data in cases().items():
        yield 'png-fault/' + name, name + '.png', data
    spec = importlib.util.spec_from_file_location('gif_probe_inputs', ROOT.parent / 'gif_support/oracle.py')
    gif = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gif)
    for name, data in gif.fixtures().items():
        yield 'gif-fault/' + name, name + '.gif', bytes(data)
    # Selection follows the extension even when it disagrees with the signature.
    for source in ['RGBA-37x29.png', 'gif-37x29-0-0.gif', 'gray-37x29.jpg']:
        for name in ['image.PNG', 'image.GiF', 'image.JPG', 'image.JPEG', 'image.svg',
                     'image', 'folder.png/image.svg', 'folder.jpg/image']:
            yield 'extension/' + source + '/' + name, name, corpus[source]
    for width, height in [(2048, 4095), (2048, 4096), (2049, 4096)]:
        name = f'area-{width}-{height}.png'
        yield 'limit/' + name, name, png(width=width, height=height, color=0)


def main():
    binary = Path(sys.argv[1]).resolve()
    report = binary.parent / 'reader-probe-corpus-results.json'
    results, differences = {}, {}
    counts = {}
    with tempfile.TemporaryDirectory(prefix='reader-probe-corpus-') as temporary:
        directory = Path(temporary)
        for key, name, data in inputs():
            group = key.split('/')[0]
            counts[group] = counts.get(group, 0) + 1
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            pair = []
            for legacy in [True, False]:
                env = {k: v for k, v in os.environ.items() if not k.startswith('READER_IMAGE_')}
                env['READER_IMAGE_PROBE_TUPLE'] = '1'
                if legacy:
                    env['READER_IMAGE_LEGACY'] = '1'
                result = subprocess.run([str(binary), str(path), str(directory / 'unused.cache'), '2', '0'],
                                        env=env, capture_output=True, timeout=10)
                assert result.returncode == 0 and len(result.stdout) == 16, (key, legacy, result.returncode,
                                                                            result.stderr.decode(errors='replace'))
                pair.append(struct.unpack('<IIII', result.stdout))
            results[key] = dict(sha256=hashlib.sha256(data).hexdigest(), native=pair[0], rust=pair[1])
            if pair[0] != pair[1]:
                differences[key] = results[key]
                break
            if not pair[1][1]:
                assert pair[1][2:] == (0x1234, 0x4321), (key, pair)
            path.unlink()
    report.write_text(json.dumps(dict(counts=counts, compared=len(results), differences=differences,
                                     fields=['format', 'valid', 'width', 'height'], results=results), indent=2) + '\n')
    print(f'{len(results)} native/Rust probe tuples compared; {len(differences)} differences; groups={counts}', flush=True)
    assert not differences, differences


if __name__ == '__main__':
    main()

"""Compare direct reader dimension probes against retained native shells."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent

def main():
    binary = Path(sys.argv[1]).resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix='reader-image-probe-') as temporary:
        directory = Path(temporary)
        with tarfile.open(ROOT / 'corpus.tar.gz') as archive:
            archive.extractall(directory, filter='data')
        for name in ['RGBA-37x29.png', 'gif-37x29-0-0.gif', 'gray-37x29.jpg']:
            faults = [('none', 0), ('source-open', 0), ('close', 0)]
            faults += [(kind, index) for kind, count in [('read', 8), ('seek', 4), ('allocation', 8)]
                       for index in range(count)]
            if name.endswith('.jpg'):
                faults += [('heap', 36 * 1024 - 1), ('heap', 36 * 1024),
                           ('native-allocation', 0), ('native-allocation', 1)]
            for fault, index in faults:
                pair = []
                diagnostics = []
                for legacy in [True, False]:
                    env = dict(os.environ, READER_IMAGE_PROBE_ONLY='1', IMAGE_DIAGNOSTIC_LOGS='1', READER_IMAGE_FAULT=fault,
                               READER_IMAGE_FAULT_INDEX=str(index))
                    if legacy:
                        env['READER_IMAGE_LEGACY'] = '1'
                    else:
                        env.pop('READER_IMAGE_LEGACY', None)
                    result = subprocess.run([str(binary), str(directory / name), str(directory / 'probe.px2'), '2', '0'],
                                            env=env, capture_output=True)
                    assert result.returncode == 0, (name, fault, index, legacy, result.returncode, result.stderr.decode())
                    assert len(result.stdout) == 12, result.stdout
                    pair.append(struct.unpack('<III', result.stdout))
                    diagnostics.append(result.stderr.decode())
                assert diagnostics[0] == diagnostics[1], (name, fault, index, diagnostics)
                assert pair[0] == pair[1], (name, fault, index, pair)
                if pair[1][0] == 0:
                    assert pair[1][1:] == (0x1234, 0x4321), pair
                results.append(dict(image=name, fault=fault, index=index, result=pair[1]))
    binary.parent.joinpath('reader-probe-failure-results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(len(results), 'strict dimension-probe comparisons passed')

if __name__ == '__main__':
    main()

"""Compare direct reader dimension probes against retained native shells."""
import json
from fault_accounting import read_report, summarize
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import zlib

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
                faults_hit = {}
                diagnostics = []
                for legacy in [True, False]:
                    env = dict(os.environ, READER_IMAGE_PROBE_ONLY='1', IMAGE_DIAGNOSTIC_LOGS='1', READER_IMAGE_FAULT=fault,
                               READER_IMAGE_FAULT_INDEX=str(index))
                    report_path = directory / 'fault-report.json'
                    env['READER_IMAGE_FAULT_REPORT'] = str(report_path)
                    if legacy:
                        env['READER_IMAGE_LEGACY'] = '1'
                    else:
                        env.pop('READER_IMAGE_LEGACY', None)
                    result = subprocess.run([str(binary), str(directory / name), str(directory / 'probe.px2'), '2', '0'],
                                            env=env, capture_output=True)
                    assert result.returncode == 0, (name, fault, index, legacy, result.returncode, result.stderr.decode())
                    faults_hit['native' if legacy else 'reader'] = read_report(report_path, fault, index)
                    assert len(result.stdout) == 12, result.stdout
                    pair.append(struct.unpack('<III', result.stdout))
                    diagnostics.append(result.stderr.decode())
                assert diagnostics[0] == diagnostics[1], (name, fault, index, diagnostics)
                assert pair[0] == pair[1], (name, fault, index, pair)
                if pair[1][0] == 0:
                    assert pair[1][1:] == (0x1234, 0x4321), pair
                results.append(dict(image=name, fault=fault, index=index, faults=faults_hit, result=pair[1]))
        # Preserve outputs and diagnostics for malformed or oversized headers.
        for name in ['RGBA-37x29.png', 'gif-37x29-0-0.gif', 'gray-37x29.jpg']:
            source = (directory / name).read_bytes()
            suffix = Path(name).suffix
            malformed = []
            for width, height in [(0, 1), (1, 0), (32768, 1), (1, 32768), (32767, 257), (4096, 4096)]:
                data = bytearray(source)
                if suffix == '.png':
                    data[16:24] = struct.pack('>II', width, height)
                    data[29:33] = struct.pack('>I', zlib.crc32(data[12:29]))
                elif suffix == '.gif':
                    data[6:10] = struct.pack('<HH', width, height)
                else:
                    marker = next(i for i in range(len(data) - 9)
                                  if data[i:i + 2] in [b'\xff\xc0', b'\xff\xc1', b'\xff\xc2'])
                    data[marker + 5:marker + 9] = struct.pack('>HH', height, width)
                malformed.append((f'{width}x{height}', data))
            malformed.extend((f'truncated-{length}', source[:length]) for length in [0, 1, 4, 8])
            for label, data in malformed:
                path = directory / ('malformed' + suffix)
                path.write_bytes(data)
                pair, diagnostics = [], []
                for legacy in [True, False]:
                    env = dict(os.environ, READER_IMAGE_PROBE_ONLY='1', IMAGE_DIAGNOSTIC_LOGS='1')
                    env.pop('READER_IMAGE_FAULT', None)
                    if legacy:
                        env['READER_IMAGE_LEGACY'] = '1'
                    else:
                        env.pop('READER_IMAGE_LEGACY', None)
                    result = subprocess.run([str(binary), str(path), str(directory / 'probe.px2'), '2', '0'],
                                            env=env, capture_output=True)
                    assert result.returncode == 0, (name, label, legacy, result.stderr)
                    pair.append(struct.unpack('<III', result.stdout))
                    diagnostics.append(result.stderr.decode())
                assert pair[0] == pair[1] == (0, 0x1234, 0x4321), (name, label, pair)
                assert diagnostics[0] == diagnostics[1], (name, label, diagnostics)
                results.append(dict(image=name, malformed=label, result=pair[1]))
    binary.parent.joinpath('reader-probe-failure-results.json').write_text(json.dumps(results, indent=2) + '\n')
    # Pin coverage as well as outcomes: losing a reachable injection must fail.
    assert summarize(results) == {side: dict(injected_settings=16, fault_hits=16, unreachable_settings=52)
                                  for side in ('native', 'reader')}, summarize(results)
    print('actual fault accounting:', json.dumps(summarize(results), sort_keys=True))
    print(len(results), 'strict dimension-probe comparisons passed')

if __name__ == '__main__':
    main()

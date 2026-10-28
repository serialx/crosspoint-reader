#!/usr/bin/env python3
"""Bound first-frame reads and preserve animation/post-process diagnostic order."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from diagnostics import gif, normalize
from pipeline_diagnostics import selected


def fixtures():
    small = gif.SMALL
    animated = small[:-1] + small[25:]
    yield 'single', small
    yield 'animated', animated
    for size in (1023, 1024, 1275, 1276):
        tail = b'\xff' * size
        blocks = b''.join(bytes([len(tail[i:i+255])]) + tail[i:i+255]
                          for i in range(0, size, 255))
        yield f'boundary-{size}', small[:-2] + blocks + b'\0;'
    for stop, frame in [('rows', small), ('eoi', small[:44] + b'\x04\x00\x01\x04\x04\0;')]:
        for size in (65536, 1048576, 16777216):
            tail = b'\xff' * size
            blocks = b''.join(bytes([len(tail[i:i+255])]) + tail[i:i+255]
                              for i in range(0, size, 255))
            data = frame[:-2] + blocks + b'\0;'
            yield f'{stop}-{size}', data
            yield f'{stop}-{size}-truncated', data[:-12]


def probe(binary, path, mode, callback):
    result = subprocess.run([str(binary), str(path), str(mode), str(callback)],
                            capture_output=True, check=True, text=True, timeout=10)
    lines = result.stdout.splitlines()
    _, ok, calls, size, pixels = lines[-1].split()
    return dict(ok=int(ok), calls=int(calls), bytes=int(size), pixels=pixels,
                events=[list(map(int, line.split()[1:])) for line in lines[:-1]])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('--base-probe', type=Path)
    parser.add_argument('--base-bmp', type=Path)
    parser.add_argument('--reference', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    records = {}
    with tempfile.TemporaryDirectory(prefix='image-diagnostics-') as tmp:
        root = Path(tmp)
        for name, data in fixtures():
            path = root / (name + '.gif')
            path.write_bytes(data)
            for mode in (0, 1):
                enabled = probe(build / 'gif_read_probe', path, mode, 1)
                disabled = probe(build / 'gif_read_probe', path, mode, 0)
                assert not disabled['events']
                assert {k: v for k, v in enabled.items() if k != 'events'} == {
                    k: v for k, v in disabled.items() if k != 'events'}
                # Header/palette reads plus at most two 1 KiB compressed refills.
                assert enabled['calls'] <= 21 and enabled['bytes'] <= 2200, (name, enabled)
                assert enabled['ok'] == 1
                record = dict(candidate=enabled, null_callback=disabled)
                if args.base_probe:
                    for callback in (0, 1):
                        base = probe(args.base_probe.resolve(), path, mode, callback)
                        assert (base['ok'], base['pixels']) == (enabled['ok'], enabled['pixels'])
                        record[f'base-{callback}'] = base
                if name.startswith('boundary-'):
                    ends = [event[1] for event in enabled['events'] if event[0] == 35]
                    assert len(ends) == 1 and ends[0] < len(data) - 10
                    record['fixture_hex'] = data.hex()
                records[f'{name}/{mode}'] = record
            if args.reference:
                outputs = []
                for binary in ('diagnostic_gif_reference', 'gif_oracle'):
                    run = subprocess.run([str(build / binary), str(path), '4'], capture_output=True,
                                         check=True, env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'))
                    outputs.append(dict(output=hashlib.sha256(run.stdout).hexdigest(),
                                        logs=[normalize(s) for s in run.stderr.decode().splitlines() if not selected([s])]))
                if name.startswith('boundary-'):
                    # Approved 2026-10-28: the decoder read-ahead windows differ.
                    warning = 'INF GIF Animated GIF detected - using first frame only for BMP conversion'
                    assert outputs[0]['output'] == outputs[1]['output']
                    assert warning not in outputs[0]['logs']
                    assert outputs[1]['logs'].count(warning) == 1
                    assert outputs[0]['logs'] == [s for s in outputs[1]['logs'] if s != warning]
                else:
                    assert outputs[0] == outputs[1], (name, outputs)
                records[name + '/legacy'] = outputs
                if name.startswith('boundary-'):
                    frames = []
                    for binary in ('diagnostic_frame_reference', 'image_oracle'):
                        run = subprocess.run([str(build / binary), str(path), str(path)+'.pxc', '6', '0'],
                                             capture_output=True, check=True,
                                             env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'))
                        assert struct.unpack_from('<I', run.stdout, 60)[0] == 1
                        frames.append(dict(output=hashlib.sha256(run.stdout).hexdigest(),
                                           logs=[normalize(s) for s in run.stderr.decode().splitlines() if not selected([s])]))
                    warning = ("ERR IMG Warning: Unsupported feature 'animation (using first frame only)' "
                               "in image '<input>'. Image may not display correctly.")
                    assert frames[0]['output'] == frames[1]['output']
                    assert warning not in frames[0]['logs']
                    assert frames[1]['logs'].count(warning) == 1
                    assert frames[0]['logs'] == [s for s in frames[1]['logs'] if s != warning]
                    records[name + '/legacy-frame'] = frames

        # No crop/scaling: 32x16 canvas/output; the first frame is only 8x4.
        gap = bytearray(gif.SMALL)
        struct.pack_into('<HH', gap, 6, 32, 16)
        gap = gap[:-1] + gap[25:]
        path = root / 'gap.gif'
        path.write_bytes(gap)
        for label, limit in [('gap-failure', 78), ('success', 1000)]:
            variants = ['gif_oracle']
            if args.reference:
                variants.insert(0, 'diagnostic_gif_reference')
            if args.base_bmp:
                variants.append(str(args.base_bmp.resolve()))
            outputs = []
            for binary in variants:
                run = subprocess.run([str(build / binary), str(path), '6', str(limit)],
                                     capture_output=True, check=True,
                                     env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'))
                logs = [normalize(s) for s in run.stderr.decode().splitlines()]
                ok = struct.unpack_from('<I', run.stdout, 36)[0]
                item = dict(binary=binary, ok=ok, logs=logs,
                            output=hashlib.sha256(run.stdout).hexdigest())
                outputs.append(item)
                if binary in ('gif_oracle', 'diagnostic_gif_reference'):
                    assert ok == (label == 'success')
                    assert sum('Animated GIF detected' in s for s in logs) == ok
                    if ok:
                        assert 'Animated GIF detected' in logs[-2] and 'Successfully converted' in logs[-1]
                    else:
                        assert 'Short BMP row write' in logs[-2] and 'post-process failed' in logs[-1]
            if args.reference:
                assert {k:v for k,v in outputs[0].items() if k != 'binary'} == {
                    k:v for k,v in outputs[1].items() if k != 'binary'}
            records[label] = outputs
    if args.report:
        args.report.write_text(json.dumps(records, indent=2))
    print('GIF read bounds, null callbacks, pixels, and post-process ordering passed')


if __name__ == '__main__':
    main()

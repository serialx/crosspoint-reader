#!/usr/bin/env python3
"""Exercise the SDK recovery path and decode controller RAM after interrupted transfers."""
import argparse
import pathlib
import subprocess
import tempfile
from strip_contract import Command, Ram, events


def decode(text, controller):
    ram = Ram(controller)
    states = {}
    activations = {}
    stage = 'initial'
    count = 0
    for event in events(text):
        if isinstance(event, Command):
            if event.code == (0x12 if ram.x3 else 0x20):
                count += 1
            ram.command(event.code, event.data)
        elif event.startswith('step '):
            states[stage] = {p: bytes(b) for p, b in ram.planes.items()}
            activations[stage] = count
            stage, count = event, 0
        elif event.startswith('wait.end '):
            ram.pending = False
    states[stage] = {p: bytes(b) for p, b in ram.planes.items()}
    activations[stage] = count
    return states, activations


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--candidate-bin', type=pathlib.Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        script = pathlib.Path(tmp) / 'case.txt'
        def run(controller, content):
            script.write_text(content)
            return subprocess.check_output([str(args.candidate_bin.resolve()), controller, str(script), '--full'], text=True)
        for controller, bands, planes in [('uc8253', 7, 2), ('ssd1677', 6, 1)]:
            prefix = 'begin\nframe\nsource 80\ndisplay 0\ngray-base 0\ngray-full\ngray-show\n'
            baseline, base_activations = decode(run(controller, prefix + 'source 0\ncleanup\nframe 1\ndisplay 2\n'), controller)
            for failure in (0, (bands * planes) // 2, bands * planes - 1):
                raw = run(controller, prefix + f'source-fail {failure}\ncleanup\nsource 0\nframe 1\ndisplay 2\n')
                assert raw.count('source.failure ') == 1, 'failure did not fire'
                states, activations = decode(raw, controller)
                assert activations['step cleanup'] == 0, 'cleanup activated a BW waveform'
                assert states['step cleanup'] == baseline['step cleanup'], 'cleanup baseline differs'
                assert states['step display 2'] == baseline['step display 2'], 'next FAST baseline differs'
                assert activations['step display 2'] == base_activations['step display 2'], 'next FAST policy differs'
            print(f'{controller}: first/middle/last cleanup failure preserves grayscale and next FAST RAM')
            # Fault each later reseed/conditioning upload, after a full BW plane has reached RAM.
            for prefix, operation in [
                ('begin\nframe\nsource 80\n', 'display 0'),
                ('begin\nframe\nsource 80\ndisplay 0\nresync 2\n', 'display 0'),
                ('begin\nframe\nsource 80\ndisplay 0\n', 'gray-base 0'),
            ]:
                control = run(controller, prefix + operation + '\n')
                suffix = control.rsplit('step ' + operation + '\n', 1)[1]
                reads = suffix.count('source.read ')
                expected, _ = decode(run(controller, prefix + 'source 0\n' + operation + '\nframe 1\ndisplay 2\n'), controller)
                cases = sorted({0, bands - 1} | {n for start in range(bands, reads, bands)
                    for n in (start, min(start + bands - 1, reads - 1))})
                for failure in cases:
                    raw = run(controller, prefix + f'source-fail {failure}\n' + operation + '\nsource 0\nframe 1\ndisplay 2\n')
                    assert raw.count('source.failure ') == 1
                    states, _ = decode(raw, controller)
                    # Recovery may use a HALF waveform, but both baselines must describe the target BW.
                    assert states['step ' + operation] == expected['step ' + operation], (controller, operation, failure)
                    assert states['step display 2'] == expected['step display 2'], (controller, 'next FAST', failure)
                print(f'{controller}: {operation}, {reads} callbacks, {len(cases)} early/late recovery cases')



if __name__ == '__main__':
    main()

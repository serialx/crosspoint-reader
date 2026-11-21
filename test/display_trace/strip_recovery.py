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


def check_x3_cleanup_recovery(run):
    prefix = 'begin\nframe\nsource 80\ndisplay 0\ngray-base 0\ngray-full\ngray-show\n'
    buffered = 'source 0\ncleanup\n'
    followup = 'frame 1\ndisplay 2\ngray-base 1\n'
    baseline, counts = decode(run('uc8253', prefix + buffered + followup), 'uc8253')
    gray, clean = baseline['step gray-show'], baseline['step cleanup']
    assert gray[0x10] != gray[0x13] and all(gray[plane] != clean[plane] for plane in gray), (
        'gray input must differ between planes and from the cleanup bitmap')
    assert counts['step cleanup'] == 0, 'buffered cleanup activated a waveform'
    assert counts['step display 2'] == 1, 'warm Fast must leave gray on the glass'
    assert counts['step gray-base 1'] == 2, 'buffered cleanup lost the Half scrub before the settle'

    waveform_mismatches = []
    for failure in (None, 0, 7, 13):
        setup = '' if failure is None else f'source-fail {failure}\n'
        raw = run('uc8253', prefix + setup + 'cleanup\nsource 0\n' + followup)
        cleanup = raw.split('step cleanup\n', 1)[1].split('step source 0\n', 1)[0]
        assert cleanup.count('source.read ') == (14 if failure is None else failure), (
            failure, 'cleanup stopped at the wrong strip')
        assert cleanup.count('source.failure ') == int(failure is not None), (failure, 'fault did not fire')
        states, activations = decode(raw, 'uc8253')
        for step in ('step cleanup', 'step display 2', 'step gray-base 1'):
            assert states[step] == baseline[step], (failure, step, 'RAM differs from buffered path')
            if activations[step] != counts[step]:
                waveform_mismatches.append((failure, step, activations[step], counts[step]))
    assert not waveform_mismatches, ('waveform count differs from buffered path', waveform_mismatches)
    print('uc8253: buffered, streamed and first/middle/last cleanup failures preserve the Half scrub and settle')


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--candidate-bin', type=pathlib.Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        script = pathlib.Path(tmp) / 'case.txt'
        def run(controller, content):
            script.write_text(content)
            return subprocess.check_output([str(args.candidate_bin.resolve()), controller, str(script), '--full'], text=True)
        check_x3_cleanup_recovery(run)
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

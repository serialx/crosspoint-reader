#!/usr/bin/env python3
"""Paired strip contract and negative checks against the C++ oracle runner."""
import argparse
import copy
import pathlib
import re
import subprocess

from strip_contract import Command, Ram, compare, events

HERE = pathlib.Path(__file__).resolve().parent


def rejected(reference, mutated, controller):
    try:
        before = Ram(controller).run(events(reference))
        after = Ram(controller).run(mutated)
        assert before.policy == after.policy and before.checkpoints == after.checkpoints
    except AssertionError:
        return
    raise AssertionError("mutation was not detected")


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--reference-bin', type=pathlib.Path, required=True)
    parser.add_argument('--candidate-bin', type=pathlib.Path, required=True)
    args = parser.parse_args()
    for controller in ('uc8253', 'ssd1677'):
        def run(binary):
            return subprocess.check_output([str(binary.resolve()), controller, str(HERE / 'strip-scenarios.txt'), '--full'], text=True)
        reference, candidate = run(args.reference_bin), run(args.candidate_bin)
        points, reads = compare(reference, candidate, controller)
        original = list(events(candidate))
        first_source = next(i for i, e in enumerate(original) if isinstance(e, str) and e.startswith('source.read '))
        plane = next(i for i in range(first_source, len(original)) if isinstance(original[i], Command) and original[i].code in ((0x10, 0x13) if controller == 'uc8253' else (0x24, 0x26)))
        changed = copy.deepcopy(original)
        changed[plane].data = bytes([changed[plane].data[0] ^ 1]) + changed[plane].data[1:]
        rejected(reference, changed, controller)
        changed = copy.deepcopy(original)
        changed[plane].data = changed[plane].data[:-1]
        rejected(reference, changed, controller)
        changed = copy.deepcopy(original)
        stride = 99 if controller == 'uc8253' else 100
        changed[plane].data = b''.join(reversed([changed[plane].data[i:i+stride] for i in range(0, len(changed[plane].data), stride)]))
        rejected(reference, changed, controller)
        changed = copy.deepcopy(original)
        del changed[plane]
        rejected(reference, changed, controller)
        changed = copy.deepcopy(original)
        changed.insert(plane + 1, Command(0x12 if controller == 'uc8253' else 0x20, b''))
        rejected(reference, changed, controller)
        changed = copy.deepcopy(original)
        wait = next(i for i, e in enumerate(changed) if isinstance(e, str) and e.startswith('wait.begin '))
        changed[wait] += ' changed-policy'
        rejected(reference, changed, controller)
        # Missing old-frame/RED reseed must be observable after the last activation.
        changed = copy.deepcopy(original)
        last = max(i for i, e in enumerate(changed) if isinstance(e, Command) and e.code in ((0x10, 0x13) if controller == 'uc8253' else (0x24, 0x26)))
        del changed[last]
        rejected(reference, changed, controller)
        # Register/window corruption and a retained last band must be rejected.
        changed = copy.deepcopy(original)
        window = next(i for i in range(first_source, plane) if isinstance(changed[i], Command) and changed[i].code == (0x90 if controller == 'uc8253' else 0x45))
        changed[window].data = bytes([changed[window].data[0] ^ 1]) + changed[window].data[1:]
        rejected(reference, changed, controller)
        changed = copy.deepcopy(original)
        activation = next(i for i in range(plane, len(changed)) if isinstance(changed[i], Command) and changed[i].code == (0x12 if controller == 'uc8253' else 0x20))
        if controller == 'uc8253':
            changed[activation:activation] = [Command(0x91, b''), copy.deepcopy(original[window])]
        else:
            changed.insert(activation, copy.deepcopy(original[window]))
        rejected(reference, changed, controller)
        changed = copy.deepcopy(original)
        changed.insert(plane, Command(0x91 if controller == 'uc8253' else 0x4e, b''))
        rejected(reference, changed, controller)
        # Split one payload across two CS ownership intervals without changing bytes.
        split_at = candidate.index('source.read ')
        match = re.search(r'spi.bytes n=(\d+) hex=([0-9a-f]+)', candidate[split_at:])
        while match and int(match[1]) < stride:
            split_at += match.end()
            match = re.search(r'spi.bytes n=(\d+) hex=([0-9a-f]+)', candidate[split_at:])
        assert match
        payload = bytes.fromhex(match[2]); cut = len(payload) // 2
        split = (f'spi.bytes n={cut} hex={payload[:cut].hex()}\n'
                 'pin.write 21 1\nspi.txn.end\nspi.txn.begin hz=10000000 order=1 mode=0\npin.write 21 0\n'
                 f'spi.bytes n={len(payload)-cut} hex={payload[cut:].hex()}')
        changed_raw = candidate[:split_at+match.start()] + split + candidate[split_at+match.end():]
        rejected(reference, list(events(changed_raw)), controller)
        # CS-low source callback: validate the raw transport parser too.
        unsafe = candidate.replace('source.read ', 'spi.txn.begin hz=10000000 order=1 mode=0\npin.write 21 0\nsource.read ', 1)
        try:
            list(events(unsafe))
        except AssertionError:
            pass
        else:
            raise AssertionError('CS-low source read accepted')
        print(f'{controller}: {points} RAM checkpoints; {reads} source boundaries; 12 mutations rejected')


if __name__ == '__main__':
    main()

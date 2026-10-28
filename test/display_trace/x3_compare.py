#!/usr/bin/env python3
"""Compare the UC8253 adapter against the C++ driver for the extended matrix."""
import argparse
import pathlib
import subprocess

from oracle import compare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin-dir', type=pathlib.Path, required=True)
    args = parser.parse_args()
    passed = True
    for buffers in ('single', 'dual'):
        for variant in range(3):
            expected = subprocess.check_output(
                [str(args.bin_dir / f'x3_driver_reference_{buffers}'), str(variant)])
            actual = subprocess.check_output(
                [str(args.bin_dir / f'x3_driver_trace_{buffers}'), str(variant)])
            passed &= compare(expected, actual, f'x3-{buffers}-variant-{variant}')
    if not passed:
        raise SystemExit(1)
    print('6/6 extended X3 driver traces identical')


if __name__ == '__main__':
    main()

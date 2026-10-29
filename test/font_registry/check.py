"""Check release-before-growth and optionally compare every byte to the old C++ executable."""
import argparse
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('candidate')
p.add_argument('--oracle')
args = p.parse_args()


def run(binary, failure=0, budget=0):
    result = subprocess.run([binary, str(failure), str(budget)], capture_output=True)
    assert result.returncode in (0, 77), (binary, result.returncode, result.stderr)
    return result.returncode, result.stdout


base = run(args.oracle or args.candidate)
count, peak, live = map(int, re.search(rb'summary requests=(\d+) peak=(\d+) live=(\d+)', base[1]).groups())
assert live == 0
for failure, budget in [(i, 0) for i in range(count + 2)] + [(0, peak - 1), (0, peak), (0, peak + 1)]:
    result = run(args.candidate, failure, budget)
    if args.oracle:
        expected = run(args.oracle, failure, budget)
        assert result == expected, (failure, budget, result, expected)
    if result[0] == 0:
        text = result[1].decode()
        assert 'result 1 families=9' in text
        assert 'vector=1' not in text
        assert text.endswith('live=0\n')
        # Once filename collection has ended, no container allocation may
        # overlap discarded names (family vector growth and shrink).
        last_name = text.rfind(' name live=')
        for line in text[last_name:].splitlines()[1:]:
            if line.startswith('alloc '):
                assert line.endswith('names=0'), line
print(f'PASS: {count + 2} failure positions, 3 peak budgets; requests={count}, peak={peak} bytes')

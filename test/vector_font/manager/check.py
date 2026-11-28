"""Assert the real FreeType manager's behavior and font-heap counts/peaks agree."""
import difflib
import json
import subprocess
import sys

parent, candidate, font = sys.argv[1:]
results = []
for executable in (parent, candidate):
    for arg, code in (("--close-default", -6), ("--close-moved", -6), ("--close-closed", 0)):
        result = subprocess.run([executable, arg], capture_output=True, text=True)
        assert result.returncode == code, (executable, arg, result.returncode, result.stderr)
        if code:
            assert "impl != nullptr" in result.stderr, result.stderr
for mode, label in enumerate(("resident-psram", "stream-prefix", "stream-internal-no-ui", "stream-pressure",
                               "resident-six-mib", "prefix-headroom-equal", "prefix-headroom-plus-one",
                               "internal-headroom-equal", "internal-headroom-minus-one", "stream-resident-stream",
                               "empty", "open-failure", "negative-read", "short-read")):
    traces = []
    for executable in (parent, candidate):
        result = subprocess.run([executable, font, str(mode)], capture_output=True, text=True)
        assert result.returncode == 0, (executable, mode, result.returncode, result.stdout, result.stderr)
        assert not result.stderr, result.stderr
        traces.append(result.stdout.splitlines())
    # STATE includes requests, live bytes and peak; TOTAL repeats count and peak.
    # Equality here asserts allocation parity, not merely a printed diagnostic.
    assert traces[0] == traces[1], "\n".join(difflib.unified_diff(*traces, fromfile="native", tofile="rust"))
    if mode < 10:
        assert any(line.startswith("STATE SCAN ") for line in traces[0])
        assert sum(line.startswith("STATE VARIANT ") for line in traces[0]) == 6
    else:
        assert any(line.startswith("STATE FAILED_RETRY ") for line in traces[0])
    if mode == 3:
        assert any(line.startswith("STATE PRESSURE ") for line in traces[0])
    total = list(map(int, traces[0][-1].split()[1:]))
    assert total[2] == total[3], "sources must close after the final face"
    results.append(dict(case=label, requests=total[0], peak=total[1], opens=total[2], closes=total[3],
                        reads=total[4], seeks=total[5], equal=True))
print(json.dumps(results, indent=2))

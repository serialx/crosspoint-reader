"""Preserve the inherited PsramAlloc failure after a successful preflight."""
import resource
import signal
import subprocess
import sys

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for owner in ("native-abort", "rust-abort"):
    result = subprocess.run([sys.argv[1], sys.argv[2], owner], capture_output=True)
    assert result.returncode == -signal.SIGABRT, (owner, result.returncode, result.stderr)
    # CHECK(false) means the injected allocation unexpectedly returned to the caller.
    assert b"check failed" not in result.stderr, (owner, result.stderr)
print("native and Rust preserve SIGABRT after cache allocation failure")

"""Every physical drawing perturbation must be detected by the frozen native oracle."""
import subprocess
import sys

for control in ('help-offset', 'icon-tint', 'icon-offset', 'header-mode'):
    result = subprocess.run([sys.argv[1], control], capture_output=True, text=True)
    assert result.returncode == 1 and 'pixel mismatch' in result.stderr, (control, result.returncode, result.stderr)
    print(control + ': detected')

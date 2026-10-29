"""Settings arena recovery uses the production adapter and family loop."""
from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / 'ui_runtime/gate_adoption.py'),
                sys.argv[1]], check=True)

"""Text Settings keeps the frozen SDK oracle and compares its Rust replacement."""
from pathlib import Path
import subprocess
import sys
subprocess.run([sys.executable, str(Path(__file__).parent / "text_settings/run.py")], check=True)

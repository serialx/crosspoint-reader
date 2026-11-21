"""Check the shipping link flag against an archive with and without the parser alias."""
import configparser
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
symbol = "crosspoint_checked_webserver_parser"
settings = configparser.ConfigParser(interpolation=None, strict=False)
settings.read(root / "platformio.ini")
flag = f"-Wl,--require-defined={symbol}"
assert flag in settings["base"]["build_flags"].split()
patch = (root / "scripts/webserver-parser.patch").read_text().splitlines()
start = next(i for i, line in enumerate(patch) if line.startswith('+asm(".global ' + symbol))
alias = "\n".join(line[1:] for line in patch[start:start + 2])
prefix = "riscv32-esp-elf-"
compiler = shutil.which(prefix + "g++")
if compiler is None:
    raise SystemExit("Add the PlatformIO RISC-V toolchain bin directory to PATH")
tools = Path(compiler).parent
with tempfile.TemporaryDirectory() as tmp:
    work = Path(tmp)
    source = work / "Parsing.cpp"
    stub = """
class NetworkClient {};
class WebServer { bool _parseRequest(NetworkClient&); };
bool WebServer::_parseRequest(NetworkClient&) { return true; }
"""
    for patched in (False, True):
        source.write_text(stub + (alias if patched else ""))
        subprocess.run([compiler, "-c", source, "-o", "Parsing.o"], cwd=work, check=True)
        subprocess.run([tools / (prefix + "ar"), "rcs", "WebServer.a", "Parsing.o"], cwd=work, check=True)
        link = subprocess.run([compiler, "-nostdlib", "-Wl,-e,0", flag, "WebServer.a", "-o", "image"],
                              cwd=work, capture_output=True, text=True)
        if patched:
            link.check_returncode()
            symbols = subprocess.check_output([tools / (prefix + "nm"), "image"], cwd=work, text=True)
            addresses = {name: address for address, _, name in (line.split() for line in symbols.splitlines())}
            assert addresses[symbol] == addresses["_ZN9WebServer13_parseRequestER13NetworkClient"]
        else:
            assert link.returncode and symbol in link.stderr, link.stderr
print("Stock archive rejected and patched parser entry linked")

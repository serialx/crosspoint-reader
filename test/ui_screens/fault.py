"""One explicit emulator-only reset-cause injection; never connects to hardware."""
import subprocess
import tempfile
import time
from pathlib import Path


def run_crash(cmd, build, case, stdout, stderr, gdb):
    with tempfile.TemporaryDirectory(prefix='ui-oracle-gdb-') as temporary:
        socket = Path(temporary) / 'qemu.sock'
        cmd += ['--qemu-arg=-gdb', '--qemu-arg=unix:' + str(socket) + ',server=on,wait=off', '--qemu-arg=-S']
        script = case / 'reset-cause.gdb'
        script.write_text(f'''set pagination off
set confirm off
set remotetimeout 30
file {build / 'firmware.elf'}
target remote {socket}
hbreak HalSystem::isRebootFromPanic()
ignore 1 1
commands
silent
set $a0 = 1
set $pc = $ra
disable 1
detach
quit
end
continue
''')
        with subprocess.Popen(cmd, stdout=stdout, stderr=stderr) as process:
            try:
                deadline = time.monotonic() + 30
                while not socket.exists():
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Emulator GDB socket did not become ready')
                    time.sleep(0.05)
                with (case / 'gdb.log').open('w') as log:
                    subprocess.run([str(gdb), '--batch', '-x', str(script)], stdout=log, stderr=log, timeout=60, check=True)
                return process.wait(timeout=120)
            except BaseException:
                process.terminate()
                process.wait(timeout=20)
                raise

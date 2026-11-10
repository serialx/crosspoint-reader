"""Reject safe extraction of the production !Unpin native-handle holder."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = '''
mod parser { pub trait Reader { fn seek(&mut self, offset:u32)->bool; fn read(&mut self,b:&mut[u8])->usize; } }
#[path = "IO_PATH"] mod io;
fn extract<P: io::Physical + Unpin>(files: std::pin::Pin<&mut io::Files<P>>) {
    let _ = std::pin::Pin::into_inner(files);
}
'''.replace('IO_PATH', str(root / 'rust/crates/xp-core/src/sdfont/io.rs'))
with tempfile.TemporaryDirectory(prefix='sdfont-pinning-') as temp:
    path = Path(temp) / 'extract.rs'
    path.write_text(source)
    result = subprocess.run(['rustc', '--edition=2021', '--crate-type=lib', str(path),
                             '--out-dir', temp], capture_output=True, text=True)
    assert result.returncode != 0, 'safe extraction unexpectedly compiled'
    assert 'PhantomPinned' in result.stderr and 'Unpin' in result.stderr, result.stderr
    print(result.stderr)
    print('PASS: safe extraction rejected by the production PhantomPinned field')

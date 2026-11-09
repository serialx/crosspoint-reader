"""Link standalone C++ regression harnesses to the production Rust facade."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def rust_archive():
    """Host xp-ffi archive selected by the test build."""
    archive = os.environ.get('XP_TEST_RUST_ARCHIVE')
    if archive is None:
        target = ROOT / 'build/rust-host'
        subprocess.run(['cargo', 'build', '--manifest-path', str(ROOT / 'rust/Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--features', 'app-test',
                        '--release', '--locked'], check=True)
        archive = str(target / 'release/libxp_ffi.a')
    return archive


def rust_link_flags(archive=None):
    archive = rust_archive() if archive is None else archive
    # This helper links app-test archives; select the matching native fixture ABI.
    return ['-DXP_APP_TEST', '-I', str(ROOT / 'test/font_cache/legacy'),
            '-I', str(ROOT / 'rust/crates/xp-ffi/include'),
            '-I', str(ROOT / 'test/reader_lifecycle'),
            str(ROOT / 'test/cmake/xp_ffi_host.cpp'), archive]


def generated_i18n_directory(archive):
    """Directory holding the I18nKeys.h the given archive was generated with.

    Numeric StrId values only match the archive when C++ code includes this
    header instead of a separately generated one (P16-O5a-HF1).
    """
    headers = sorted((Path(archive).resolve().parent / 'build').glob('xp-core-*/out/I18nKeys.h'))
    assert headers, ('missing generated I18nKeys.h for archive', str(archive))
    contents = {path.read_bytes() for path in headers}
    assert len(contents) == 1, ('conflicting generated I18nKeys.h files', [str(path) for path in headers])
    return headers[0].parent

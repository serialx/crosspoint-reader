"""Generate deterministic inputs; goldens must be recorded with the C inflater."""
from pathlib import Path
import zlib

root = Path(__file__).with_name('fixtures')
payload = (b'CrossPoint inflate window boundary\n' * 3000) + bytes(range(256)) * 5
for name, data, level, strategy in [
    ('stored', payload, 0, zlib.Z_DEFAULT_STRATEGY),
    ('fixed', payload, 6, zlib.Z_FIXED),
    ('dynamic', payload, 6, zlib.Z_DEFAULT_STRATEGY),
    ('empty', b'', 6, zlib.Z_DEFAULT_STRATEGY),
]:
    for wrapped in (False, True):
        compressor = zlib.compressobj(level, zlib.DEFLATED, 15 if wrapped else -15, 8, strategy)
        (root / (name + ('.zlib' if wrapped else '.raw'))).write_bytes(compressor.compress(data) + compressor.flush())
(root / 'plain.bin').write_bytes(payload)

"""Reconstruct the native predecessor for compatibility oracles after Rust adoption."""
from pathlib import Path
import hashlib
import prepare

HERE = Path(__file__).resolve().parent
METHODS = ['onEnter', 'onExit', 'loop', 'render', 'rustSceneFailed', 'rustPublished', 'rustSceneCurrent', 'buildRustScene', 'rustResources']
SHA256 = "1f4b7734676aabfd878c95039b19497043c106ebe5e873356a5ddc3e6f17cbba"

def source(root):
    frozen = HERE / "native_oracle/wifi_pre_owner_roots.cpp.txt"
    data = frozen.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA256
    text = (root / "src/activities/network/WifiSelectionActivity.cpp").read_text()
    for name in METHODS:
        current = prepare.extract(text, "WifiSelectionActivity", name)
        original = prepare.extract(data.decode(), "WifiSelectionActivity", name)
        text = text.replace(current, original, 1)
    return text

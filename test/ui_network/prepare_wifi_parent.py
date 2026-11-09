"""Hash-checked native predecessor for compatibility fixtures only."""
from pathlib import Path
import hashlib

HERE = Path(__file__).resolve().parent
IDENTITIES = {
    'wifi_pre_owner.cpp.txt': '344c75de244a233e2170b984dd9d9215eb03657ea7b9baed1dfe54c4512d5882',
    'wifi_pre_owner.h.txt': '42aa1c8b1d0fbc33e210f8ba3626a466a134745d5360746325cc7676d78671f3',
}

def frozen(name):
    data = (HERE / "native_oracle" / name).read_bytes()
    assert hashlib.sha256(data).hexdigest() == IDENTITIES[name]
    return data.decode().replace("xp_ui_network_wifi_", "xp_test_network_wifi_")

def source(root):
    return frozen("wifi_pre_owner.cpp.txt")

def header():
    return frozen("wifi_pre_owner.h.txt")

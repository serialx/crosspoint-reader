"""Derive Rust features from the SDK's platform-independent panel selection."""

import subprocess

DRIVERS = {
    "FREEINK_DRIVER_UC8253_X3": "uc8253",
    "FREEINK_DRIVER_UC8279": "uc8279-x3",
    "FREEINK_DRIVER_SSD1677": "ssd1677",
    "FREEINK_DRIVER_UC8179": "uc8179",
    "FREEINK_DRIVER_UC8279_X4": "uc8279-x4",
    "FREEINK_DRIVER_SSD2677": "ssd2677",
    "FREEINK_DRIVER_PAPER_MONO": "paper-mono",
    "FREEINK_DRIVER_UC8279C": "uc8279c-a4",
}


def display_features(defines, compiler, header):
    values = {item[0]: str(item[1]) if len(item) > 1 else "1"
              for item in (d if isinstance(d, (tuple, list)) else (d,) for d in defines)}
    if values.get("FREEINK_RUST_PANEL", "0") == "0":
        return []
    source = []
    for macro, feature in DRIVERS.items():
        condition = macro
        # These configurations deliberately retain the SDK's C++ SSD1677 driver.
        if feature == "ssd1677":
            condition += " && !defined(FREEINK_SSD1677_CONFIG) && !defined(SSD1677_PROBE_DEBUG)"
        source += ["#if " + condition, "display-" + feature, "#endif"]
    command = [str(compiler), "-E", "-P", "-x", "c++", "-include", str(header), "-"]
    command += ["-D" + key + "=" + value for key, value in values.items()]
    result = subprocess.run(command, input="\n".join(source) + "\n", text=True,
                            capture_output=True, check=True)
    return sorted(result.stdout.split())

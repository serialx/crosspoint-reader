"""Select Rust panel policy for the controllers enabled by SDK board flags."""

CONTROLLERS = {
    "FREEINK_DEVICE_X3": ("uc8253", "uc8279-x3"),
    "FREEINK_DEVICE_X4": ("ssd1677", "uc8179", "uc8279-x4"),
    "FREEINK_DEVICE_X4PRO": ("ssd1677", "uc8179", "uc8279-x4"),
    "FREEINK_DEVICE_X4CLASSIC": ("ssd1677", "uc8179", "uc8279-x4"),
    "FREEINK_DEVICE_STICKY": ("ssd1677", "ssd2677"),
    "FREEINK_DEVICE_PICCO": ("ssd1677", "ssd2677"),
    "FREEINK_DEVICE_METALIO_EINK4": ("ssd1677",),
    "FREEINK_DEVICE_PAPERMONO": ("paper-mono",),
    "FREEINK_DEVICE_EEGO_A4": ("uc8279c-a4",),
}


def display_features(defines):
    values = {item[0]: str(item[1]) if len(item) > 1 else "1"
              for item in (d if isinstance(d, (tuple, list)) else (d,) for d in defines)}
    strips = values.get("XP_STRIP_RENDER", "0") != "0"
    if values.get("FREEINK_RUST_PANEL", "0") == "0":
        if strips:
            raise ValueError("XP_STRIP_RENDER requires FREEINK_RUST_PANEL")
        return []
    selected = {controller for board, controllers in CONTROLLERS.items()
                if values.get(board, "0") != "0" for controller in controllers}
    # These SDK configurations deliberately retain the C++ SSD1677 driver.
    if "FREEINK_SSD1677_CONFIG" in values or "SSD1677_PROBE_DEBUG" in values:
        selected.discard("ssd1677")
    return ["display-" + controller for controller in sorted(selected)] + (["reader-strips"] if strips else [])

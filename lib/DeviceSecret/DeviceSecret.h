#pragma once

#include <xp_hal.h>

#include <cstdint>

// Random 32-byte per-device secret, created on first use and kept in NVS: it
// survives SD swaps and reformats (only a full flash erase replaces it) and
// never leaves the device. Feeds plugin device IDs and the book-key wrap; each
// use derives its own value so none reveals another. False when NVS is
// unavailable: an unstable secret is worse than none.
bool deviceSecret(uint8_t (&out)[32]);

// The borrower holds the shared lock until releaseDeviceSecret().
XpHalSecret* borrowDeviceSecret(uint32_t* handle, XpHalSecretServices* services);
void releaseDeviceSecret();

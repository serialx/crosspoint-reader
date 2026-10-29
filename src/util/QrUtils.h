#pragma once

#include <GfxRenderer.h>

#include <string>

#include "components/themes/BaseTheme.h"

namespace QrUtils {

// Caller owns the packed row-major bitmap; no allocation. Size is zero on failure.
bool encodeQrBitmap(const std::string& text, uint8_t* bitmap, size_t capacity, uint8_t& modules);

// Renders a QR code with the given text payload within the specified bounding box.
void drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload);

}  // namespace QrUtils

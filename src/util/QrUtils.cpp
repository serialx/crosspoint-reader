#include "QrUtils.h"

#include <Memory.h>
#include <qrcode.h>
#include <xp_ffi.h>

#include "Logging.h"
namespace {
bool encode(void*, const uint8_t* text, size_t len, uint8_t version, uint8_t* modules) {
  QRCode code;
  return qrcode_initBytes(&code, modules, version, ECC_LOW, const_cast<uint8_t*>(text), static_cast<uint16_t>(len)) ==
         0;
}
void draw(void* context, int32_t x, int32_t y, int32_t size) {
  static_cast<const GfxRenderer*>(context)->fillRect(x, y, size, size, true);
}
}  // namespace
bool QrUtils::encodeQrBitmap(const std::string& text, uint8_t* bitmap, size_t capacity, uint8_t& modules) {
  return xp_assets_qr_bitmap(reinterpret_cast<const uint8_t*>(text.data()), text.size(), bitmap, capacity, &modules,
                             nullptr, encode);
}
void QrUtils::drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& text) {
  uint8_t version = 0;
  size_t length = 0;
  const size_t bytes = xp_assets_qr_plan(reinterpret_cast<const uint8_t*>(text.data()), text.size(), &version, &length);
  const auto bitmap = makeUniqueNoThrow<uint8_t[]>(bytes);
  if (!bitmap) {
    LOG_ERR("QR", "Failed to allocate QR bitmap (%u bytes)", static_cast<unsigned>(bytes));
    return;
  }
  uint8_t size = 0;
  if (!encodeQrBitmap(text, bitmap.get(), bytes, size)) {
    LOG_ERR("QR", "Text too large for QR Code version %d", version);
    return;
  }
  xp_assets_qr_draw(bitmap.get(), bytes, size, bounds.x, bounds.y, bounds.width, bounds.height,
                    const_cast<GfxRenderer*>(&renderer), draw);
}

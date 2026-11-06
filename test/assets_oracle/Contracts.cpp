#include <QrUtils.h>
#include <xp_ffi.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" size_t asset_allocation_attempts();
extern "C" size_t asset_platform_live();
extern "C" size_t asset_platform_largest();
extern "C" size_t asset_live_bytes();
extern "C" size_t asset_peak_bytes();
extern "C" void asset_reset_peak();
static size_t expectedDrawLive;
void qrScratchLifetime() {
  struct Case {
    size_t payload, bitmap, peak;
  };
  for (const auto& item : {Case{1, 137, 496}, Case{115, 407, 1524}, Case{396, 1177, 4554}, Case{1067, 2347, 9096},
                           Case{2953, 3917, 15276}}) {
    const std::string text(item.payload, 'x');
    GfxRenderer renderer;
    expectedDrawLive = item.bitmap;
    renderer.beforeFillRect = [] { assert(asset_live_bytes() == expectedDrawLive); };
    const Rect bounds{0, 0, 300, 300};
    assert(asset_live_bytes() == 0);
    asset_reset_peak();
    const auto before = asset_allocation_attempts();
    QrUtils::drawQrCode(renderer, bounds, text);
    assert(!renderer.rectangles.empty());
    assert(asset_allocation_attempts() == before + 2);
    assert(asset_peak_bytes() == item.peak && asset_live_bytes() == 0);
    for (size_t failure = 0; failure < 2; ++failure) {
      renderer.rectangles.clear();
      const auto index = std::to_string(asset_allocation_attempts() + failure);
      assert(setenv("ASSET_FAIL_ALLOC", index.c_str(), 1) == 0);
      QrUtils::drawQrCode(renderer, bounds, text);
      assert(renderer.rectangles.empty() && asset_live_bytes() == 0);
      assert(unsetenv("ASSET_FAIL_ALLOC") == 0);
    }
  }
}
struct Sink {
  size_t calls = 0, fail = SIZE_MAX;
  std::vector<uint8_t> bytes;
  static bool write(void* raw, const uint8_t* bytes, size_t size) {
    auto& self = *static_cast<Sink*>(raw);
    if (self.calls++ == self.fail) return false;
    self.bytes.insert(self.bytes.end(), bytes, bytes + size);
    return true;
  }
};
void jpegPlatformBuffers() {
  static const std::array<uint8_t, 2048 * 16> grayscale{};
  XpAssetJpegConfig config{2048, 16, false, false, false};
  auto run = [&]() {
    Sink sink;
    const XpAssetJpegServices services{
        &sink, [](void* ctx, const uint8_t* bytes, size_t len) { Sink::write(ctx, bytes, len); }, [](void*) {},
        [](void*, uint32_t, int32_t, int32_t, int32_t, int32_t) {},
        [](void*, void* state, XpAssetJpegBlock block) {
          return block(state, 0, 0, 2048, 16, 2048, grayscale.data());
        }};
    return xp_assets_jpeg_bmp(2048, 16, false, &config, &services);
  };
  const size_t before = asset_allocation_attempts();
  assert(run());
  const size_t allocations = asset_allocation_attempts() - before;
  assert(allocations == 3 && asset_platform_largest() == 32768 && asset_platform_live() == 0);
  for (size_t i = 0; i < allocations; ++i) {
    const auto index = std::to_string(asset_allocation_attempts() + i);
    assert(setenv("ASSET_FAIL_ALLOC", index.c_str(), 1) == 0);
    assert(!run() && asset_platform_live() == 0);
    assert(unsetenv("ASSET_FAIL_ALLOC") == 0);
    assert(run() && asset_platform_live() == 0);
  }
}
int main() {
  jpegPlatformBuffers();
  qrScratchLifetime();
  std::string text = "https://crosspointreader.com";
  uint8_t version = 0;
  size_t payload = 0;
  const size_t capacity =
      xp_assets_qr_plan(reinterpret_cast<const uint8_t*>(text.data()), text.size(), &version, &payload);
  std::vector<uint8_t> bitmap(capacity + 2, 0xa5);
  uint8_t modules = 99;
  const size_t allocations = asset_allocation_attempts();
  assert(!QrUtils::encodeQrBitmap(text, bitmap.data() + 1, capacity - 1, modules));
  assert(modules == 0);
  for (uint8_t value : bitmap) assert(value == 0xa5);
  assert(QrUtils::encodeQrBitmap(text, bitmap.data() + 1, capacity, modules));
  assert(modules == 33 && bitmap.front() == 0xa5 && bitmap.back() == 0xa5);
  assert(asset_allocation_attempts() == allocations + 1);
  GfxRenderer ownedQr, borrowedQr;
  const Rect bounds{10, 20, 300, 300};
  QrUtils::drawQrCode(ownedQr, bounds, text);
  const auto beforeBorrowed = asset_allocation_attempts();
  QrUtils::drawQrBytes(borrowedQr, bounds, reinterpret_cast<const uint8_t*>(text.data()), text.size());
  assert(borrowedQr.rectangles == ownedQr.rectangles);
  assert(asset_allocation_attempts() == beforeBorrowed + 2);  // Transient bitmap and encoder scratch.
  borrowedQr.rectangles.clear();
  const auto failure = std::to_string(asset_allocation_attempts());
  assert(setenv("ASSET_FAIL_ALLOC", failure.c_str(), 1) == 0);
  QrUtils::drawQrBytes(borrowedQr, bounds, reinterpret_cast<const uint8_t*>(text.data()), text.size());
  assert(borrowedQr.rectangles.empty());
  assert(unsetenv("ASSET_FAIL_ALLOC") == 0);
  const auto scratchFailure = std::to_string(asset_allocation_attempts() + 1);
  assert(setenv("ASSET_FAIL_ALLOC", scratchFailure.c_str(), 1) == 0);
  QrUtils::drawQrBytes(borrowedQr, bounds, reinterpret_cast<const uint8_t*>(text.data()), text.size());
  assert(borrowedQr.rectangles.empty());
  assert(unsetenv("ASSET_FAIL_ALLOC") == 0);
  std::array<uint8_t, 18> frame{};
  for (size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<uint8_t>(i * 19);
  Sink complete;
  assert(xp_assets_screenshot(frame.data(), frame.size(), 16, 9, &complete, Sink::write) == 0);
  assert(complete.calls == 17 && complete.bytes.size() == 126);
  for (size_t failure = 0; failure < complete.calls; ++failure) {
    Sink sink;
    sink.fail = failure;
    assert(xp_assets_screenshot(frame.data(), frame.size(), 16, 9, &sink, Sink::write) == 1);
    assert(sink.calls == failure + 1);
    const size_t prefix = failure == 0 ? 0 : 62 + (failure - 1) * 4;
    assert(sink.bytes.size() == prefix);
    if (prefix) assert(std::memcmp(sink.bytes.data(), complete.bytes.data(), prefix) == 0);
  }
  Sink invalid;
  assert(xp_assets_screenshot(frame.data(), frame.size() - 1, 16, 9, &invalid, Sink::write) == 1);
  assert(invalid.calls == 0);
}

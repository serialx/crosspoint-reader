#include <ImagePipeline.h>

#include <array>
#include <cstdlib>
extern "C" void xp_test_fail_allocation_after(size_t);

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const int failure = std::atoi(argv[1]);
  GfxRenderer renderer;
  RenderConfig config{};
  config.maxWidth = 4;
  config.maxHeight = 4;
  config.useExactDimensions = true;
  config.cachePath = argv[2];
  const bool jpeg = failure == 10 || failure == 16;
  const bool gif = failure == 14 || failure == 15;
  if (failure == 10) {
    config.maxWidth = 32767;
    config.maxHeight = 32767;
  }
  if (failure == 15 || failure == 16) {
    config.maxWidth = 1;
    config.maxHeight = 1;
    config.useExactDimensions = false;
  }
  ImagePipeline pipeline(renderer, config);
  if (!pipeline.init(failure >= 15 ? 1 : 4, failure >= 15 ? 100 : 4,
                     jpeg  ? ImagePipeline::Kind::Jpeg
                     : gif ? ImagePipeline::Kind::Gif
                           : ImagePipeline::Kind::Png))
    return failure >= 15 ? 0 : 3;
  if (failure == 11 || failure == 14) xp_test_fail_allocation_after(0);
  const bool prepared = pipeline.prepare();
  xp_test_fail_allocation_after(SIZE_MAX);
  if (!prepared) return 0;
  if (failure == 12) xp_test_fail_allocation_after(0);
  if (failure == 0) HalFileTest::failNextWrite = true;
  const bool started = pipeline.beginCache();
  xp_test_fail_allocation_after(SIZE_MAX);
  if (!started) return 0;
  const std::array<uint8_t, 4> pixels{};
  XpImageRow row{0, 0, 4, 1, 4, pixels.data(), pixels.size(), nullptr, 0, 0, 0, 8, 0};
  if (failure == 1) HalFileTest::failNextWrite = true;
  for (int y = 0; y < 3; ++y) {
    row.y = y;
    pipeline.row(row);
  }
  if (failure == 2) HalFileTest::failNextWrite = true;
  if (failure == 3) HalFileTest::failNextClose = true;
  bool success;
  pipeline.finish(success);
}

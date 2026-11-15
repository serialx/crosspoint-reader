#include <GfxRenderer.h>
#include <GifToFramebufferConverter.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  GfxRenderer renderer;
  GifToFramebufferConverter gif;
  RenderConfig config{};
  config.maxWidth = 528;
  config.maxHeight = 700;
  config.useDithering = true;
  const int count = std::atoi(argv[2]);
  bool success = false;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < count; ++i) {
    renderer.calls.clear();
    success = gif.decodeToFramebuffer(argv[1], renderer, config);
  }
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::printf("%d %.6f\n", success, elapsed / count);
}

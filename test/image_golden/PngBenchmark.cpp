#include <GfxRenderer.h>
#include <PngToFramebufferConverter.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const int repeats = std::atoi(argv[2]);
  if (repeats <= 0) return 2;
  GfxRenderer renderer;
  RenderConfig config{};
  config.maxWidth = 528;
  config.maxHeight = 792;
  config.useDithering = true;
  PngToFramebufferConverter decoder;
  double micros = 0;
  bool ok = false;
  for (int i = 0; i < repeats; ++i) {
    renderer.calls.clear();
    const auto start = std::chrono::steady_clock::now();
    ok = decoder.decodeToFramebuffer(argv[1], renderer, config);
    micros += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    if (!ok) break;
  }
  std::printf("{\"success\":%s,\"repeats\":%d,\"us\":%.3f}\n", ok ? "true" : "false", repeats, micros / repeats);
  return ok ? 0 : 1;
}

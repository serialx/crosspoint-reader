int main() {
  for (int flags = 0; flags < 8; ++flags)
    for (int status = 0; status < 5; ++status) {
      GfxRenderer renderer;
      HomeCoverCache c(renderer);
      allocationFailure = flags & 1;
      restoreFailure = flags & 2;
      copyFailure = flags & 4;
      bitmapStatus = status;
      c.begin();
      for (int step = 0; step < 12; ++step) {
        if (step == 4) renderer.orientation = 1;
        if (step == 6) c.invalidate(0);
        if (step == 9) c.invalidate();
        c.prepare();
        fui::Rect r{int16_t(step == 3 ? 21 : 20), 10, int16_t(step == 5 ? 301 : 100), int16_t(step == 5 ? 400 : 150)};
        const int index = step == 11 ? 7 : step == 8 ? 1 : 0;
        paintTrace = 0;
        bool painted = c.paint(r, index, status == 4 ? "" : "cover.bmp");
        std::printf("cache %d %d %d: %d %d %zu %zu %d", flags, status, step, painted, paintTrace, c.coverCacheCapacity,
                    c.coverCacheUsed, c.coverCacheOrientation);
        for (auto& s : c.cachedCovers)
          std::printf(" %d,%d,%d,%d,%zu,%zu,%d", s.rect.x, s.rect.y, s.rect.width, s.rect.height, s.offset, s.bytes,
                      s.valid);
        std::puts("");
      }
    }
}

const auto fillHighlight = [&](const int x, const int y, const int width, const int height) {
  struct Context {
    GfxRenderer& renderer;
    int x, y, width, height;
  } context{renderer, x, y, width, height};
  const XpUiReaderClipServices services{
      &context,
      [](void* raw, uint8_t field, uint32_t) -> uint32_t {
        const auto& c = *static_cast<Context*>(raw);
        switch (static_cast<XpUiReaderClipQuery>(field)) {
          case XP_UI_READER_CLIP_QUERY_HAS_SECTION:
          case XP_UI_READER_CLIP_QUERY_HAS_CLIPPINGS:
          case XP_UI_READER_CLIP_QUERY_PAGE_COUNT:
          case XP_UI_READER_CLIP_QUERY_ELEMENT_COUNT:
          case XP_UI_READER_CLIP_QUERY_WORD_COUNT:
          case XP_UI_READER_CLIP_QUERY_VISIBLE:
          case XP_UI_READER_CLIP_QUERY_CLIP_COUNT:
          case XP_UI_READER_CLIP_QUERY_SOURCE_END:
            return 1;
          case XP_UI_READER_CLIP_QUERY_LINE_X:
            return c.x;
          case XP_UI_READER_CLIP_QUERY_LINE_Y:
            return c.y;
          case XP_UI_READER_CLIP_QUERY_A4:
            return BoardConfig::isEegoA4();
          case XP_UI_READER_CLIP_QUERY_BW:
            return c.renderer.getRenderMode() == GfxRenderer::BW;
          default:
            return 0;
        }
      },
      [](void* raw, uint8_t command, int32_t a, int32_t b) -> uint32_t {
        auto& c = *static_cast<Context*>(raw);
        switch (static_cast<XpUiReaderClipCommand>(command)) {
          case XP_UI_READER_CLIP_COMMAND_ELEMENT_BEGIN:
          case XP_UI_READER_CLIP_COMMAND_BLOCK_BEGIN:
          case XP_UI_READER_CLIP_COMMAND_SIGNATURE:
            return 1;
          case XP_UI_READER_CLIP_COMMAND_MEASURE:
            return c.width;
          case XP_UI_READER_CLIP_COMMAND_LINE_HEIGHT:
            return c.height;
          case XP_UI_READER_CLIP_COMMAND_PIXEL:
            c.renderer.drawPixel(a, b, false);
            return 0;
          default:
            return 0;
        }
      },
      [](void* raw, const XpUiReaderClipRect* r) {
        static_cast<Context*>(raw)->renderer.fillRectDither(r->x, r->y, r->width, r->height, Color::LightGray);
      },
      [](void*, uint32_t, XpUiReaderClipRecord* out) { *out = {0, 0, 0, 1, 0, 0, 1, 0, 1}; }};
  const XpUiReaderClipInput input{};
  xp_ui_reader_clipping(&services, XP_UI_READER_CLIP_OPERATION_DRAW, &input);
};

// Persistence fixture; the complete native lifetime binding is tested by reader_book_lifetime.
void EpubReaderActivity::loadProgress() {
  struct Context {
    EpubReaderActivity& o;
    std::optional<HalFile> file;
    uint8_t bytes[10]{};
  } context{*this, {}, {}};
  const XpUiReaderLifetimeServices services{
      &context,
      [](void* p, uint8_t field) -> uint32_t {
        assert(field == XP_UI_READER_LIFETIME_FIELD_SPINE);
        return static_cast<Context*>(p)->o.currentSpineIndex;
      },
      [](void* p, uint8_t field, uint32_t value) {
        auto& o = static_cast<Context*>(p)->o;
        switch (field) {
          case XP_UI_READER_LIFETIME_FIELD_SPINE: o.currentSpineIndex = value; break;
          case XP_UI_READER_LIFETIME_FIELD_NEXT_PAGE: o.nextPageNumber = value; break;
          case XP_UI_READER_LIFETIME_FIELD_CACHED_SPINE: o.cachedSpineIndex = value; break;
          case XP_UI_READER_LIFETIME_FIELD_CACHED_COUNT: o.cachedChapterTotalPageCount = value; break;
          case XP_UI_READER_LIFETIME_FIELD_CACHED_OFFSET: o.cachedVisibleTextOffset = value; break;
          default: assert(false);
        }
      },
      [](void* p, uint8_t query, uint32_t argument) -> uint32_t {
        assert(query == XP_UI_READER_LIFETIME_QUERY_PROGRESS_BYTE && argument < 10);
        return static_cast<Context*>(p)->bytes[argument];
      },
      [](void* p, uint8_t command, int32_t, int32_t) -> uint32_t {
        auto& c = *static_cast<Context*>(p);
        switch (command) {
          case XP_UI_READER_LIFETIME_COMMAND_OPEN_PROGRESS:
            c.file.emplace();
            return Storage.openFileForRead("ERS", c.o.epub->getCachePath() + "/progress.bin", *c.file);
          case XP_UI_READER_LIFETIME_COMMAND_READ_PROGRESS: return c.file->read(c.bytes, sizeof(c.bytes));
          case XP_UI_READER_LIFETIME_COMMAND_DROP_FILE: c.file.reset(); break;
          case XP_UI_READER_LIFETIME_COMMAND_LOG_SENTINEL:
            LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
            break;
          case XP_UI_READER_LIFETIME_COMMAND_LOG_PROGRESS:
            LOG_DBG("ERS", "Loaded cache: %d, %d", c.o.currentSpineIndex, c.o.nextPageNumber);
            break;
          default: assert(false);
        }
        return 0;
      }};
  xp_ui_reader_book_lifetime(&services, XP_UI_READER_LIFETIME_OPERATION_PROGRESS);
  assert(!context.file);
}

// Persistence fixture adapter. The complete native binding is tested by reader_navigation.
void EpubReaderActivity::restoreSavedPosition() {
  struct Context {
    EpubReaderActivity& o;
    std::optional<RenderLock> lock;
  } context{*this, {}};
  const XpUiReaderNavServices services{
      &context,
      [](void* p, uint8_t field) -> uint32_t {
        auto& o = static_cast<Context*>(p)->o;
        if (field == XP_UI_READER_NAV_FIELD_FOOTNOTE_DEPTH) return o.footnoteDepth;
        assert(field >= XP_UI_READER_NAV_FIELD_SAVED_SPINE0 && field <= XP_UI_READER_NAV_FIELD_SAVED_PAGE2);
        const int slot = field - XP_UI_READER_NAV_FIELD_SAVED_SPINE0;
        return slot % 2 ? o.savedPositions[slot / 2].pageNumber : o.savedPositions[slot / 2].spineIndex;
      },
      [](void* p, uint8_t field, uint32_t value) {
        auto& o = static_cast<Context*>(p)->o;
        switch (field) {
          case XP_UI_READER_NAV_FIELD_FOOTNOTE_DEPTH: o.footnoteDepth = value; break;
          case XP_UI_READER_NAV_FIELD_SPINE: o.currentSpineIndex = value; break;
          case XP_UI_READER_NAV_FIELD_NEXT_PAGE: o.nextPageNumber = value; break;
          case XP_UI_READER_NAV_FIELD_CACHED_COUNT:
            assert(!value);
            o.clearDeferredReposition();
            break;
          case XP_UI_READER_NAV_FIELD_HAS_CACHED_OFFSET: assert(!value); break;
          default: assert(false);
        }
      },
      [](void*, uint8_t, uint32_t) -> uint32_t { assert(false); return 0; },
      [](void* p, uint8_t command, int32_t a, int32_t) -> uint32_t {
        auto& c = *static_cast<Context*>(p);
        auto& o = c.o;
        switch (command) {
          case XP_UI_READER_NAV_COMMAND_LOCK: assert(!c.lock); c.lock.emplace(); break;
          case XP_UI_READER_NAV_COMMAND_UNLOCK: assert(c.lock); c.lock.reset(); break;
          case XP_UI_READER_NAV_COMMAND_DROP_SECTION: o.section.reset(); break;
          case XP_UI_READER_NAV_COMMAND_UPDATE: o.requestUpdate(); break;
          case XP_UI_READER_NAV_COMMAND_LOG_RESTORED: {
            const auto& position = o.savedPositions[a];
            LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", a, position.spineIndex, position.pageNumber);
            break;
          }
          default: assert(false);
        }
        return 0;
      },
      nullptr, 0};
  xp_ui_reader_navigation(&services, XP_UI_READER_NAV_OPERATION_RESTORE, 0, 0);
  assert(!context.lock);
}

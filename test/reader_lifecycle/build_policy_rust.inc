// M1 fixture leaves; reader_build_policy tests the complete production binding.
bool EpubReaderActivity::runBuildPolicy(uint8_t operation) const {
  struct Context {
    EpubReaderActivity& o;
    std::optional<RenderLock> lock;
  } context{const_cast<EpubReaderActivity&>(*this), {}};
  const XpUiReaderBuildServices services{
      &context,
      [](void* p, uint8_t field) -> uint32_t {
        auto& o = static_cast<Context*>(p)->o;
        switch (field) {
          case XP_UI_READER_BUILD_FIELD_HEAP_PAUSED: return o.buildHeapPaused;
          case XP_UI_READER_BUILD_FIELD_OVERLAY: return static_cast<uint32_t>(o.overlay);
          case XP_UI_READER_BUILD_FIELD_PENDING_TURN: return o.pendingManualTurn;
          case XP_UI_READER_BUILD_FIELD_SPINE: return o.currentSpineIndex;
          case XP_UI_READER_BUILD_FIELD_FAILED_SPINE: return o.backgroundBuildFailedSpine;
          case XP_UI_READER_BUILD_FIELD_HAS_SECTION: return bool(o.section);
          case XP_UI_READER_BUILD_FIELD_BUILDING: return o.section->isBuilding();
          case XP_UI_READER_BUILD_FIELD_COMPLETE: return o.section->isBuildComplete();
          case XP_UI_READER_BUILD_FIELD_PAGE: return o.section->currentPage;
          default: assert(false); return 0;
        }
      },
      [](void* p, uint8_t field, uint32_t value) {
        auto& o = static_cast<Context*>(p)->o;
        switch (field) {
          case XP_UI_READER_BUILD_FIELD_HEAP_PAUSED: o.buildHeapPaused = value; break;
          case XP_UI_READER_BUILD_FIELD_FAILED_SPINE: o.backgroundBuildFailedSpine = value; break;
          case XP_UI_READER_BUILD_FIELD_NEXT_PAGE: o.nextPageNumber = value; break;
          case XP_UI_READER_BUILD_FIELD_BUFFER_STALE: o.pageBufferStale = value; break;
          default: assert(false);
        }
      },
      [](void* p, uint8_t command, int32_t button, int32_t) -> uint32_t {
        auto& c = *static_cast<Context*>(p);
        auto& o = c.o;
        switch (command) {
          case XP_UI_READER_BUILD_COMMAND_TRY_LOCK: c.lock.emplace(RenderLock::Mode::Try); return c.lock->ownsLock();
          case XP_UI_READER_BUILD_COMMAND_UNLOCK: c.lock.reset(); break;
          case XP_UI_READER_BUILD_COMMAND_RADIO_HEAP: return bleturner::holdsHeap();
          case XP_UI_READER_BUILD_COMMAND_RAW: return buttonContactClosed();
          case XP_UI_READER_BUILD_COMMAND_BUTTON: return button == 0 && o.anyButtonPressed();
          case XP_UI_READER_BUILD_COMMAND_READ_FREE: return ESP.getFreeHeap();
          case XP_UI_READER_BUILD_COMMAND_READ_BLOCK: return ESP.getMaxAllocHeap();
          case XP_UI_READER_BUILD_COMMAND_RELEASE_STRIPS: return false;
          case XP_UI_READER_BUILD_COMMAND_LOAN_COUNT: return o.renderer.frameBufferLoanCount();
          case XP_UI_READER_BUILD_COMMAND_BUILD_SOME: return o.section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK, &buttonContactClosed);
          case XP_UI_READER_BUILD_COMMAND_LOG_BUILD_FAILED: LOG_ERR("ERS", "Background section build failed"); break;
          case XP_UI_READER_BUILD_COMMAND_DROP_SECTION: o.section.reset(); break;
          case XP_UI_READER_BUILD_COMMAND_REPOSITION: return o.applyDeferredReposition();
          case XP_UI_READER_BUILD_COMMAND_UPDATE: o.requestUpdate(); break;
          default: assert(false);
        }
        return 0;
      }};
  const bool result = xp_ui_reader_build_policy(&services, operation);
  assert(!context.lock);
  return result;
}

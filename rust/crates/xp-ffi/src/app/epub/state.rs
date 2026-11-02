#[derive(Clone, Copy, Default)]
pub(super) struct Position {
    pub(super) spine: i32,
    pub(super) page: i32,
}
#[derive(Clone, Copy, Default)]
pub(super) struct CachedPosition {
    pub(super) spine: i32,
    pub(super) pages: i32,
    pub(super) offset: Option<u32>,
}
#[derive(Clone, Copy, Default)]
pub(super) struct DeferredPosition {
    pub(super) page: Option<u16>,
    pub(super) offset: Option<u32>,
    pub(super) percent: bool,
    pub(super) spine_progress: f32,
}
#[derive(Clone, Copy, Default)]
pub(super) struct History {
    pub(super) positions: [Position; 3],
    pub(super) depth: u8,
}
#[derive(Clone, Copy, Default, PartialEq, Eq)]
pub(super) enum Overlay {
    #[default]
    Page,
    Toolbar,
    Contents,
    Text,
    More,
}
#[derive(Clone, Copy, Default)]
pub(super) enum LoadFailure {
    #[default]
    None,
    Protected,
    Expired,
    Unverified,
}
#[derive(Clone, Copy)]
pub(super) enum Notice {
    BookmarkAdded,
    BookmarkRemoved,
    Dictionary,
    FontDamaged,
    ClipSaved,
    ClipRemoved,
    ClipLimit,
    ClipFailed,
    ClipDeleteFailed,
    ClipExportFailed,
}
#[derive(Clone, Copy)]
pub(super) struct TimedNotice {
    pub(super) message: Notice,
    pub(super) since: u32,
}
#[derive(Clone, Copy, Default)]
pub(super) struct SessionAccounting {
    pub(super) anchor_ms: u32,
    pub(super) anchor_epoch: i64,
    pub(super) start_epoch: i64,
    pub(super) duration: u32,
    pub(super) anchor_progress: u16,
    pub(super) start_progress: u16,
    pub(super) end_progress: u16,
    pub(super) has_anchor: bool,
    pub(super) pending_forward: bool,
}

impl SessionAccounting {
    pub(super) fn note(&mut self, forward: bool, succeeded: bool) {
        self.pending_forward = forward && succeeded;
    }

    pub(super) fn render(&mut self, now: u32, epoch: i64, progress: i32) {
        let progress = progress.clamp(0, 10000) as u16;
        if self.pending_forward && self.has_anchor && self.anchor_epoch > 0 {
            let elapsed = now.wrapping_sub(self.anchor_ms) / 1000;
            if elapsed >= 2 {
                if self.duration == 0 {
                    self.start_epoch = self.anchor_epoch;
                    self.start_progress = self.anchor_progress;
                    self.end_progress = self.anchor_progress;
                }
                self.duration = self.duration.wrapping_add(elapsed.min(1800));
                self.end_progress = self.end_progress.max(progress);
            }
        }
        self.anchor_ms = now;
        self.anchor_epoch = epoch;
        self.anchor_progress = progress;
        self.has_anchor = true;
        self.pending_forward = false;
    }

    pub(super) fn worthy(&self) -> bool {
        self.start_epoch > 0
            && self.duration >= 10
            && self.start_epoch.wrapping_add(i64::from(self.duration)) > self.start_epoch
    }
}
#[derive(Clone, Copy, Default)]
pub(super) struct BasePublication {
    pub(super) session: SessionAccounting,
    pub(super) refresh_counter: i32,
    pub(super) refresh_requests: u8,
    pub(super) end_ready: bool,
    pub(super) rendered: bool,
    pub(super) remembered: bool,
}
#[derive(Clone, Copy, Default)]
pub(super) enum ChildIntent {
    #[default]
    None,
    Menu,
    Chapter,
    Percent,
    TextSettings {
        from_overlay: bool,
    },
    Footnote {
        reopen_menu: bool,
    },
    Selection(super::child::SelectionCapture),
    Dictionary,
    Bookmarks,
    Clippings,
    Qr,
    Wifi,
}
#[derive(Clone, Copy, Default)]
pub(super) enum PopupIntent {
    #[default]
    None,
    FontSize,
    Spacing,
    Alignment,
    Orientation,
    AutoTurn,
    LoadFailure {
        offer_sync: bool,
    },
    RetryTime,
}
#[derive(Clone, Copy, Default)]
pub(super) struct SectionFacts {
    pub(super) pages: u16,
    pub(super) estimated_pages: i32,
    pub(super) building: bool,
    pub(super) complete: bool,
    pub(super) partial: bool,
}
#[derive(Clone, Copy, Default)]
pub(super) struct ResourceFacts {
    pub(super) book: bool,
    pub(super) section: Option<SectionFacts>,
    #[expect(dead_code, reason = "Read by the pending loop fact projection.")]
    pub(super) end_menu: bool,
    #[expect(dead_code, reason = "Read by the pending loop fact projection.")]
    pub(super) load_popup: bool,
    pub(super) spine_count: i32,
    pub(super) frame_stale: bool,
    pub(super) radio_released: bool,
}
#[derive(Clone, Copy, Default)]
pub(super) struct SceneGate {
    pub(super) suppress_buttons: u16,
    pub(super) failed: bool,
    pub(super) suppress_contact: bool,
}

// Shared scalar state only. Readers copy the required fields, never a whole State.
#[derive(Default)]
pub(super) struct State {
    pub(super) ready: bool,
    pub(super) base: BasePublication,
    pub(super) current: Position,
    pub(super) next_page: i32,
    pub(super) cached: CachedPosition,
    pub(super) visible_offset: Option<u32>,
    pub(super) deferred: DeferredPosition,
    pub(super) history: History,
    pub(super) last_saved: (i32, i32, i32),
    pub(super) idle_prewarm: Position,
    pub(super) last_turn_ms: u32,
    pub(super) turn_duration_ms: u32,
    pub(super) last_render_ms: u32,
    pub(super) manual_turn: i8,
    pub(super) manual_touch: bool,
    pub(super) auto_turn: bool,
    pub(super) screenshot: bool,
    pub(super) sync_save_error: bool,
    pub(super) page_retries: u8,
    #[expect(
        dead_code,
        reason = "Read by the pending input suppression owner port."
    )]
    pub(super) skip_button_frame: bool,
    pub(super) bookmark_notice: Option<TimedNotice>,
    pub(super) dictionary_notice: Option<TimedNotice>,
    pub(super) font_notice: Option<TimedNotice>,
    pub(super) clipping_notice: Option<TimedNotice>,
    pub(super) bookmarked: bool,
    pub(super) bookmark_removed: bool,
    pub(super) recents_removed: bool,
    pub(super) move_to_read: bool,
    pub(super) overlay: Overlay,
    pub(super) focused_tool: i32,
    pub(super) panel_index: i32,
    pub(super) panel_hold_jumped: bool,
    pub(super) panel_cursor: bool,
    pub(super) page_stored: bool,
    pub(super) refresh_pending: bool,
    pub(super) auto_option: u8,
    pub(super) link_note_drawn: Option<u8>,
    pub(super) viewport: (u16, u16),
    pub(super) link_margins: (i32, i32),
    pub(super) partial_start_failed: bool,
    pub(super) heap_paused: bool,
    pub(super) failed_build_spine: i32,
    pub(super) build_popup: bool,
    pub(super) orientation: u8,
    pub(super) failure: LoadFailure,
    pub(super) facts: ResourceFacts,
    pub(super) child: ChildIntent,
    pub(super) popup: PopupIntent,
    pub(super) scene_gate: SceneGate,
}

impl State {
    pub(super) fn new(fast_initial: bool, refresh_frequency: i32) -> Self {
        Self {
            base: BasePublication {
                refresh_counter: if fast_initial {
                    refresh_frequency.max(2)
                } else {
                    0
                },
                ..BasePublication::default()
            },
            last_saved: (-1, -1, -1),
            idle_prewarm: Position {
                spine: -1,
                page: -1,
            },
            failed_build_spine: -1,
            ..Self::default()
        }
    }
}

pub(super) struct RenderFrame {
    pub(super) base: BasePublication,
}

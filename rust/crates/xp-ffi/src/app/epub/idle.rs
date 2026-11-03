use super::{abi, EpubSession};
use core::mem::MaybeUninit;
use xp_ui::{
    model::BACK,
    reader::{
        idle,
        input::{ButtonQuery, Query},
        GO_HOME_MS,
    },
};

impl EpubSession {
    pub(super) fn try_idle_loop(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            return unsafe { (hooks.idle)(hooks.context) != 0 };
        }
        if unsafe { (self.leaves.render_busy)() } == 0 {
            return false;
        }
        let book = self.loop_book();
        let spine_count = book.map_or(0, |book| unsafe {
            (super::session_book::spine_count)(book.as_ptr())
        });
        let popup = self.loop_failure_popup();
        let load_popup =
            popup.is_some_and(|popup| unsafe { (self.leaves.popup_active)(popup.as_ptr()) != 0 });
        let mut input = MaybeUninit::<abi::XpEpubInputFacts>::uninit();
        unsafe {
            (self.leaves.input_facts)(input.as_mut_ptr());
        }
        let input = unsafe { input.assume_init() };
        let settings = self.settings(false);
        let threshold = self.confirm_long_press_threshold();
        let sample = self.model(|state| idle::Snapshot {
            busy: 1,
            has_epub: u8::from(book.is_some()),
            spine_count,
            load_popup: u8::from(load_popup),
            end_options: u8::from(state.base.end_ready),
            touch: input.touch,
            remote: input.remote_click,
            home_action: input.home_action,
            edges: input.raw_edges,
            held_buttons: input.raw_held as u8,
            orientation: settings.orientation,
            long_button: settings.long_button,
            short_power: settings.short_power,
            tilt: settings.tilt,
            confirm_threshold: threshold,
            touch_controls: settings.touch_controls,
            remembered: u8::from(state.base.remembered),
            spine: state.current.spine,
            overlay: state.overlay as u8,
            automatic: u8::from(state.auto_turn),
            pending_turn: state.manual_turn,
            bookmark_notice: u8::from(state.bookmark_notice.is_some()),
            dictionary_notice: u8::from(state.dictionary_notice.is_some()),
            font_notice: u8::from(state.font_notice.is_some()),
            clipping_notice: u8::from(state.clipping_notice.is_some()),
            read_move: u8::from(state.move_to_read),
            recents_removed: u8::from(state.recents_removed),
            applied_orientation: state.orientation,
            footnote_depth: state.history.depth,
            reserved: [0; 2],
        });
        let plan = idle::plan(&sample);
        if plan & idle::ADMITTED == 0 {
            return false;
        }
        if plan & idle::SCREEN_LONG != 0 {
            let (mut x, mut y) = (0, 0);
            unsafe {
                (self.leaves.input_screen_long)(&mut x, &mut y);
            }
        }
        self.mapped_query(Query::BackGesture);
        self.mapped_button(BACK, ButtonQuery::Long, GO_HOME_MS);
        self.mapped_button(BACK, ButtonQuery::Release, 0);
        true
    }
}

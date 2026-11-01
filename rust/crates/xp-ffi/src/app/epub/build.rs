use super::{abi, app_abi, state::Overlay, EpubSession, ResourceAccess};

const ROOM_WAIT_MS: u32 = 5000;
const ROOM_STEP_MS: u32 = 50;
const MIN_FREE: u32 = 32 * 1024;
const MIN_BLOCK: u32 = 16 * 1024;
const PAGES_PER_TICK: i32 = 2;

impl EpubSession {
    fn radio_status(&self) -> abi::XpEpubRadio {
        let mut status = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.leaves.radio_status)(status.as_mut_ptr());
            status.assume_init()
        }
    }

    fn radio_ready(&self, asked: &mut bool) -> bool {
        if !self.model(|state| state.facts.radio_released)
            && (!*asked || self.radio_status().starting == 0)
        {
            let released = unsafe { (self.leaves.radio_before_build)() } != 0;
            self.model(|state| state.facts.radio_released = released);
        }
        *asked = true;
        let status = self.radio_status();
        status.running == 0 && status.stopping == 0 && status.starting == 0
    }

    pub(super) fn make_build_room(&self, access: &ResourceAccess<'_>) -> bool {
        unsafe { (self.leaves.strip_release_idle)(self.native(access)) };
        let mut asked = false;
        if self.radio_ready(&mut asked) {
            return true;
        }
        let started = unsafe { (self.runtime.now_ms)() };
        loop {
            if unsafe { (self.runtime.now_ms)() }.wrapping_sub(started) >= ROOM_WAIT_MS {
                unsafe {
                    (self.leaves.log)(1, app_abi::Text::from_bytes(b"ERS"), app_abi::Text::from_bytes(
                        b"Bluetooth radio still holds the heap after 5000 ms; chapter build refused"));
                }
                return false;
            }
            unsafe { (self.runtime.delay_ms)(ROOM_STEP_MS) };
            if self.radio_ready(&mut asked) {
                return true;
            }
        }
    }

    fn heap(&self) -> abi::XpEpubHeap {
        let mut heap = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.leaves.heap_snapshot)(heap.as_mut_ptr());
            heap.assume_init()
        }
    }

    pub(super) fn build_heap_gate(&self, access: &ResourceAccess<'_>) -> bool {
        let before = self.heap();
        let mut paused = before.free_bytes < MIN_FREE || before.largest_block < MIN_BLOCK;
        self.model(|state| state.heap_paused = paused);
        if paused && unsafe { (self.leaves.strip_release_idle)(self.native(access)) } != 0 {
            let after = self.heap();
            paused = after.free_bytes < MIN_FREE || after.largest_block < MIN_BLOCK;
            self.model(|state| state.heap_paused = paused);
            self.loop_log(
                format_args!(
                    "Released idle reader strips for build: heap {}/{} -> {}/{}",
                    before.free_bytes, before.largest_block, after.free_bytes, after.largest_block
                ),
                &[],
            );
        }
        !paused
    }

    pub(super) fn show_build_popup(&self, access: &ResourceAccess<'_>) {
        if !self.model(|state| state.build_popup) {
            return;
        }
        let mut geometry = core::mem::MaybeUninit::<abi::XpEpubGeometry>::uninit();
        unsafe { (self.leaves.render_geometry)(geometry.as_mut_ptr()) };
        if unsafe { (*geometry.as_ptr()).has_framebuffer } == 0 {
            return;
        }
        unsafe {
            let label = (self.resources.translate)(xp_core::i18n::ids::STR_INDEXING);
            (self.resources.popup_draw)(label);
        }
        let frame = unsafe { (*self.guarded(access)).active_frame };
        if let Some(frame) = frame {
            unsafe { (*frame.as_ptr()).base.refresh_counter = 1 };
        } else {
            self.model(|state| state.base.refresh_counter = 1);
        }
        self.model(|state| state.build_popup = false);
    }

    pub(super) fn buttons_pressed(&self) -> bool {
        (0..7).any(|button| unsafe { (self.leaves.input_pressed)(button) } != 0)
    }

    fn build_wanted_locked(&self, access: &ResourceAccess<'_>) -> bool {
        self.section(access).is_some_and(|section| {
            (unsafe { (self.leaves.section_building)(section.as_ptr()) } != 0)
                && unsafe { (self.leaves.radio_holds_heap)() } == 0
                && self.model(|state| state.current.spine != state.failed_build_spine)
        })
    }

    pub(super) fn build_wanted(&self) -> bool {
        let Some(building) = self.model(|state| state.facts.section.map(|facts| facts.building))
        else {
            return false;
        };
        #[cfg(feature = "app-test")]
        if let Some(observer) = self.building_observer {
            unsafe { (observer.read)(observer.context, u8::from(building)) };
        }
        building
            && unsafe { (self.leaves.radio_holds_heap)() } == 0
            && self.model(|state| state.current.spine != state.failed_build_spine)
    }

    pub(super) fn skip_loop_delay(&self) -> bool {
        self.model(|state| matches!(state.overlay, Overlay::Page) && !state.heap_paused)
            && self.build_wanted()
    }

    pub(super) fn background_build(&self) {
        let Some(access) = self.try_resources() else {
            return;
        };
        if self.model(|state| !matches!(state.overlay, Overlay::Page))
            || !self.build_wanted_locked(&access)
        {
            return;
        }
        if unsafe { (self.leaves.input_raw_contact)() } != 0
            || self.buttons_pressed()
            || self.model(|state| state.manual_turn != 0)
            || !self.build_heap_gate(&access)
        {
            return;
        }
        let before = unsafe { (self.leaves.loan_count)() };
        let section = self.section(&access).expect("background section");
        let built =
            unsafe { (self.leaves.section_build_more)(section.as_ptr(), PAGES_PER_TICK, 1) } != 0;
        self.publish_section(&access);
        if unsafe { (self.leaves.loan_count)() } != before {
            self.model(|state| state.facts.frame_stale = true);
            unsafe { (self.runtime.notify_render)() };
        }
        if !built {
            unsafe {
                (self.leaves.log)(
                    1,
                    app_abi::Text::from_bytes(b"ERS"),
                    app_abi::Text::from_bytes(b"Background section build failed"),
                )
            };
            self.model(|state| {
                state.failed_build_spine = state.current.spine;
                state.next_page = state.current.page;
            });
            self.drop_section(&access);
            unsafe { (self.runtime.notify_render)() };
        } else if unsafe { (self.leaves.section_complete)(section.as_ptr()) } != 0
            && self.apply_deferred(&access)
        {
            unsafe { (self.runtime.notify_render)() };
        }
    }
}

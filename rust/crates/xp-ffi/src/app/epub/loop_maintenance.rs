use super::session_section::Section as ReaderSection;
use super::{abi, EpubSession, ResourceAccess};
use core::mem::MaybeUninit;
use xp_ui::reader::{
    BACKGROUND_BUILD_MIN_MAX_ALLOC, PARTIAL_REBUILD_START_MARGIN, RENDER_MIN_FREE_HEAP,
};

struct Prewarm<'a, 'b>(&'a ResourceAccess<'b>);
impl Drop for Prewarm<'_, '_> {
    fn drop(&mut self) {
        let owner = self.0.owner;
        unsafe { (owner.leaves.fonts_prewarm_drop)(owner.native(self.0)) };
    }
}

impl EpubSession {
    pub(super) fn loop_radio_after(&self) {
        if !self.model(|state| state.facts.radio_released) {
            return;
        }
        let Some(access) = self.try_resources() else {
            return;
        };
        if !self
            .section(&access)
            .is_some_and(|section| unsafe { ReaderSection::building(section) })
        {
            self.model(|state| state.facts.radio_released = false);
            unsafe { (self.leaves.radio_after_paint)() };
        }
    }

    pub(super) fn loop_link_note(&self) {
        let Some(_access) = self.try_resources() else {
            return;
        };
        let note = self.radio_status().note;
        let repaint = self.loop_repaint_note(note);
        if repaint {
            self.request_update();
        }
    }

    fn loop_repaint_note(&self, note: u8) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            return unsafe { (hooks.link_repaint)(hooks.context, note) != 0 };
        }
        self.model(|state| {
            if state.link_note_drawn.is_some_and(|drawn| drawn != note) {
                state.link_note_drawn = None;
                true
            } else {
                false
            }
        })
    }

    fn loop_framebuffer(&self) -> bool {
        let value = self.overlay_geometry().has_framebuffer != 0;
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            unsafe { (hooks.framebuffer)(hooks.context, u8::from(value)) };
        }
        value
    }

    fn loop_font_cache(&self) -> bool {
        let value = self.overlay_geometry().has_font_cache != 0;
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            unsafe { (hooks.font_cache)(hooks.context, u8::from(value)) };
        }
        value
    }

    pub(super) fn loop_prewarm(&self) {
        let Some(access) = self.try_resources() else {
            return;
        };
        let Some(section) = self.section(&access) else {
            return;
        };
        if unsafe { ReaderSection::building(section) }
            || !self.loop_framebuffer()
            || self.model(|state| state.last_render_ms == 0)
            || unsafe { (self.runtime.now_ms)() }
                .wrapping_sub(self.model(|state| state.last_render_ms))
                <= 400
        {
            return;
        }
        let services = unsafe { (self.leaves.render_services)(self.native(&access)).as_ref() }
            .expect("physical renderer services");
        let read = services.read.expect("physical renderer query");
        if unsafe {
            read(
                services.context,
                xp_ui::reader::render::Query::FreeHeap as u8,
            )
        } <= RENDER_MIN_FREE_HEAP
            || unsafe {
                read(
                    services.context,
                    xp_ui::reader::render::Query::MaxBlock as u8,
                )
            } <= BACKGROUND_BUILD_MIN_MAX_ALLOC
        {
            return;
        }
        let (spine, idle) = self.model(|state| (state.current.spine, state.idle_prewarm));
        let page = self.model(|state| state.current.page);
        if idle.spine == spine && idle.page == page {
            return;
        }
        self.model(|state| state.idle_prewarm = super::state::Position { spine, page });
        let next = page.wrapping_add(1);
        if next >= i32::from(unsafe { ReaderSection::page_count(section) }) {
            return;
        }
        let Some(page) = (unsafe { self.load_page(section, next) }) else {
            return;
        };
        if !self.loop_font_cache() {
            return;
        }
        let started = unsafe { (self.runtime.now_ms)() };
        if unsafe { (self.leaves.fonts_prewarm_begin)(self.native(&access)) } == 0 {
            return;
        }
        let _prewarm = Prewarm(&access);
        unsafe {
            page.borrow()
                .paint(self.leaves, self.settings(true).font, 0, 0);
            (self.leaves.fonts_prewarm_finish_scan)(self.native(&access));
        }
        if unsafe { (self.leaves.log_enabled)(0) } != 0 {
            let elapsed = unsafe { (self.runtime.now_ms)() }.wrapping_sub(started);
            self.loop_log(
                format_args!("Idle prewarm: page {} in {}ms", next, elapsed),
                &[],
            );
        }
    }

    pub(super) fn loop_partial_build(&self) {
        let Some(access) = self.try_resources() else {
            return;
        };
        let Some(section) = self.section(&access) else {
            return;
        };

        if unsafe { ReaderSection::building(section) }
            || unsafe { !ReaderSection::partial(section) }
            || self.model(|state| state.viewport.0 == 0 || state.partial_start_failed)
            || unsafe { (self.leaves.radio_holds_heap)() } != 0
            || self
                .model(|state| state.current.page)
                .wrapping_add(PARTIAL_REBUILD_START_MARGIN)
                < i32::from(unsafe { ReaderSection::page_count(section) })
        {
            return;
        }
        unsafe { (self.leaves.strip_release_idle)(self.native(&access)) };
        let (width, height) = self.model(|state| state.viewport);
        let mut spec = MaybeUninit::<abi::XpEpubRenderSpec>::uninit();
        unsafe { (self.leaves.settings_render_spec)(width, height, spec.as_mut_ptr()) };
        let started =
            unsafe { ReaderSection::start(section, &spec.assume_init(), None, self.runtime) };
        self.publish_section(&access);
        if !started {
            self.model(|state| state.partial_start_failed = true);
            self.loop_log_parts(1, &[b"Failed to start deferred partial extension build"]);
        } else if unsafe { (self.leaves.log_enabled)(0) } != 0 {
            let page = self.model(|state| state.current.page);
            let count = unsafe { ReaderSection::page_count(section) };
            self.loop_log(
                format_args!(
                    "Reader near partial watermark ({}/{}), resuming extension build",
                    page, count
                ),
                &[],
            );
        }
    }
}

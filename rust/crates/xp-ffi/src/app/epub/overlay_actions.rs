use super::{abi, state::Overlay, EpubSession, ResourceAccess};
use core::{ffi::c_void, mem::MaybeUninit, ptr::NonNull};
use xp_ui::{
    model::{Input, BACK},
    reader::{
        input::Query,
        menu_parent::Action,
        render::{Kind, Op},
    },
    scene::Refresh,
};

impl EpubSession {
    pub(super) fn overlay_popup(&self) -> Option<NonNull<c_void>> {
        // The loop owns popup lifetime; render only observes it under RenderLock.
        unsafe { core::ptr::addr_of!((*self.guarded.get()).overlay_popup).read() }
    }

    fn overlay_popup_active(&self) -> bool {
        self.overlay_popup()
            .is_some_and(|popup| unsafe { (self.leaves.popup_active)(popup.as_ptr()) != 0 })
    }

    pub(super) fn dismiss_overlay_popup(&self) {
        if let Some(popup) = self.overlay_popup() {
            unsafe { (self.leaves.popup_dismiss)(popup.as_ptr()) };
        }
    }

    pub(super) fn overlay_popup_changed(&self) {
        if self.overlay_popup_active() {
            self.paint_overlay_popup();
            return;
        }
        let access = self.lock_resources();
        self.settle_overlay_refresh(&access);
        if self.model(|state| state.page_stored) {
            unsafe { (self.leaves.render_restore_bw)(0) };
            let stored = self.render_operation(
                &access,
                Op {
                    kind: Kind::StoreBw as u8,
                    ..Op::default()
                },
            ) != 0;
            self.model(|state| state.page_stored = stored);
            self.render_overlay(&access);
            self.push_overlay_refresh(&access);
        } else {
            self.request_update();
        }
    }

    pub(super) fn redraw_overlay(&self) {
        let access = self.lock_resources();
        self.settle_overlay_refresh(&access);
        self.render_overlay(&access);
        self.push_overlay_refresh(&access);
    }

    pub(super) fn dismiss_overlay_panel(&self) {
        self.model(|state| state.overlay = Overlay::Toolbar);
        if self.model(|state| state.page_stored) {
            {
                let access = self.lock_resources();
                self.settle_overlay_refresh(&access);
                unsafe { (self.leaves.render_restore_bw)(0) };
                let stored = self.render_operation(
                    &access,
                    Op {
                        kind: Kind::StoreBw as u8,
                        ..Op::default()
                    },
                ) != 0;
                self.model(|state| state.page_stored = stored);
            }
            self.redraw_overlay();
        } else {
            self.request_update();
        }
    }

    pub(super) fn cancel_rejected_input(&self, input: &Input) {
        if input.released & BACK != 0 {
            self.cancel_overlay();
        }
    }

    fn cancel_overlay(&self) {
        if self.model(|state| matches!(state.overlay, Overlay::Page))
            || self.loop_toolbar().is_none()
        {
            return;
        }
        if self.overlay_popup_active() {
            self.dismiss_overlay_popup();
            self.overlay_popup_changed();
        } else if self.model(|state| matches!(state.overlay, Overlay::Toolbar)) {
            self.close_overlay_to_page();
        } else {
            self.dismiss_overlay_panel();
        }
    }

    fn reset_home_input(&self) {
        if let Some(app) = super::super::app() {
            app.input.borrow_mut().reset();
        }
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_hooks {
            unsafe { (hooks.home_reset)(hooks.context) };
        }
    }

    // Only the loop installs/drops this slot; component methods publish their own data.
    pub(super) fn loop_toolbar(&self) -> Option<NonNull<c_void>> {
        unsafe { core::ptr::addr_of!((*self.guarded.get()).toolbar).read() }
    }

    pub(super) fn overlay_geometry(&self) -> abi::XpEpubGeometry {
        let mut value = MaybeUninit::uninit();
        unsafe {
            (self.leaves.render_geometry)(value.as_mut_ptr());
            value.assume_init()
        }
    }

    pub(super) fn mapped_query(&self, query: Query) -> u32 {
        let services = unsafe { &*(self.leaves.input_services)() };
        unsafe { services.read.expect("mapped input query")(services.context, query as u8) }
    }

    fn overlay_toc(&self, access: &ResourceAccess<'_>) -> i32 {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_hooks {
            return unsafe { (hooks.current_toc)(hooks.context) };
        }
        self.current_toc_index(access)
    }

    pub(super) fn build_more_actions(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_hooks {
            if let Some(build) = hooks.build_more {
                unsafe { build(hooks.context) };
                return true;
            }
        }
        let footnotes = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                return false;
            }
            unsafe { (self.leaves.metadata_footnotes)(self.native(&access)) != 0 }
        };
        let bookmarks = unsafe { core::ptr::addr_of!((*self.guarded.get()).bookmarks).read() };
        let bookmarks = bookmarks
            .is_some_and(|handle| unsafe { (self.leaves.bookmarks_count)(handle.as_ptr()) != 0 });
        let clippings = unsafe { (self.leaves.clippings_count)() != 0 };
        let (mut actions, count) = self.more_menu_items(footnotes, bookmarks, clippings);
        let mut retained = 0;
        for index in 0..count {
            let action = actions[index];
            if action != Action::Chapter as u8 && action != Action::Text as u8 {
                actions[retained] = action;
                retained += 1;
            }
        }
        let access = self.lock_resources();
        if self.model(|state| state.scene_gate.failed) {
            return false;
        }
        unsafe {
            (*self.guarded(&access)).more_actions = actions;
            (*self.guarded(&access)).more_count = retained as u8;
        }
        true
    }

    fn more_menu_items(
        &self,
        footnotes: bool,
        bookmarks: bool,
        clippings: bool,
    ) -> ([u8; 19], usize) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            let mut actions = [0; 19];
            let count = unsafe {
                (hooks.build_items)(
                    hooks.context,
                    u8::from(footnotes),
                    u8::from(bookmarks),
                    u8::from(clippings),
                    actions.as_mut_ptr(),
                )
            };
            assert!(count <= 19);
            return (actions, usize::from(count));
        }
        let mut actions = [0u8; 19];
        let mut count = 0;
        let mut add = |action: Action| {
            actions[count] = action as u8;
            count += 1;
        };
        if footnotes {
            add(Action::Footnotes);
        }
        if bookmarks {
            add(Action::Bookmarks);
        }
        add(Action::ToggleBookmark);
        add(Action::SaveClipping);
        if clippings {
            add(Action::ViewClippings);
        }
        add(Action::Night);
        if unsafe { (self.runtime.frontlight_present)() } != 0 {
            let mut input = MaybeUninit::<abi::XpEpubInputFacts>::uninit();
            unsafe { (self.leaves.input_facts)(input.as_mut_ptr()) };
            if unsafe { input.assume_init() }.slide_owns_light == 0 {
                add(Action::Frontlight);
            }
        }
        if self.radio_status().compiled_in != 0 {
            add(Action::Bluetooth);
        }
        if self.settings(false).dictionary_available != 0 {
            add(Action::Dictionary);
        }
        for action in [
            Action::Orientation,
            Action::Auto,
            Action::Percent,
            Action::Screenshot,
            Action::Qr,
            Action::Home,
            Action::Sync,
            Action::DeleteCache,
        ] {
            add(action);
        }
        (actions, count)
    }

    pub(super) fn open_overlay(&self, target: Overlay) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_input_hooks {
            self.model(|state| state.overlay = target);
            unsafe {
                (hooks.open)(hooks.context, target as u8);
            }
            return;
        }
        self.reset_home_input();
        let (previous, mut focus, mut index) =
            self.model(|state| (state.overlay, state.focused_tool, state.panel_index));
        let toolbar = if let Some(toolbar) = self.loop_toolbar() {
            toolbar
        } else {
            let Some(toolbar) = NonNull::new(unsafe { (self.leaves.toolbar_open)() }) else {
                self.request_update();
                return;
            };
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                unsafe { (self.leaves.toolbar_close)(toolbar.as_ptr()) };
                drop(access);
                self.request_update();
                return;
            }
            unsafe { (*self.guarded(&access)).toolbar = Some(toolbar) };
            toolbar
        };
        if matches!(previous, Overlay::Page) {
            unsafe { (self.leaves.toolbar_begin)(toolbar.as_ptr()) };
        }
        let cursor = self.mapped_query(Query::HasTouch) == 0;
        match target {
            Overlay::Toolbar => focus = 0,
            Overlay::Contents => {
                let access = self.lock_resources();
                if self.model(|state| state.scene_gate.failed) {
                    drop(access);
                    self.request_update();
                    return;
                }
                index = self.overlay_toc(&access);
                unsafe { (self.leaves.toolbar_reset_navigation)(toolbar.as_ptr(), index, index) };
            }
            Overlay::Text | Overlay::More => {
                index = 0;
                if matches!(target, Overlay::More) && !self.build_more_actions() {
                    self.request_update();
                    return;
                }
                unsafe { (self.leaves.toolbar_reset_navigation)(toolbar.as_ptr(), 0, 0) };
            }
            Overlay::Page => {}
        }
        self.model(|state| {
            state.overlay = target;
            state.focused_tool = focus;
            state.panel_index = index;
            state.panel_cursor = cursor;
            state.panel_hold_jumped = false;
        });
        if self.model(|state| state.facts.section.is_some() && !state.facts.frame_stale) {
            let access = self.lock_resources();
            self.settle_overlay_refresh(&access);
            if matches!(previous, Overlay::Page) {
                let stored = self.render_operation(
                    &access,
                    Op {
                        kind: Kind::StoreBw as u8,
                        ..Op::default()
                    },
                ) != 0;
                self.model(|state| state.page_stored = stored);
            } else if self.model(|state| state.page_stored) {
                unsafe { (self.leaves.render_restore_bw)(0) };
                let stored = self.render_operation(
                    &access,
                    Op {
                        kind: Kind::StoreBw as u8,
                        ..Op::default()
                    },
                ) != 0;
                self.model(|state| state.page_stored = stored);
            }
            self.render_overlay(&access);
            self.push_overlay_refresh(&access);
        } else {
            self.request_update();
        }
    }

    pub(super) fn close_overlay_to_page(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_input_hooks {
            self.model(|state| state.overlay = Overlay::Page);
            unsafe {
                (hooks.close)(hooks.context);
            }
            return;
        }
        self.reset_home_input();
        self.model(|state| state.overlay = Overlay::Page);
        self.dismiss_overlay_popup();
        if self.loop_toolbar().is_some() {
            let access = self.lock_resources();
            // Cancellation still tears down a toolbar after its scene was rejected.
            let toolbar = unsafe { (*self.guarded(&access)).toolbar.take() };
            if let Some(toolbar) = toolbar {
                unsafe { (self.leaves.toolbar_close)(toolbar.as_ptr()) };
            }
        }
        let geometry = self.overlay_geometry();
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_hooks {
            unsafe { (hooks.a4)(hooks.context, geometry.a4) };
        }
        if geometry.a4 != 0 {
            self.set_refresh_counter(1);
        }
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_hooks {
            unsafe { (hooks.xteink)(hooks.context, geometry.xteink_panel) };
        }
        if geometry.xteink_panel == 0 && self.model(|state| state.page_stored) {
            let access = self.lock_resources();
            self.settle_overlay_refresh(&access);
            unsafe { (self.leaves.render_restore_bw)(0) };
            self.model(|state| state.page_stored = false);
            self.render_operation(
                &access,
                Op {
                    kind: Kind::Display as u8,
                    mode: Refresh::Fast as u8,
                    ..Op::default()
                },
            );
        } else {
            {
                let access = self.lock_resources();
                self.discard_overlay_page(&access);
            }
            self.request_update();
        }
    }
}

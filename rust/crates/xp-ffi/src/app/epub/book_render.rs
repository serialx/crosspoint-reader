use super::{
    abi, app_abi,
    page::Page,
    state::{Notice, Overlay, TimedNotice},
    EpubSession, ResourceAccess,
};
use core::{ffi::c_void, fmt, mem::MaybeUninit, ptr::NonNull};
use xp_core::i18n::ids;
use xp_ui::{
    reader::{
        render::{Kind, Op},
        BUILD_POPUP_BYTE_THRESHOLD, BUILD_POPUP_DEADLINE_MS, BUILD_POPUP_PAGE_THRESHOLD,
        PARTIAL_REBUILD_START_MARGIN,
    },
    scene::{Command, Rect, Refresh},
};

struct Viewport {
    margins: [i32; 4],
    spec: abi::XpEpubRenderSpec,
}

struct RenderMessage {
    bytes: [u8; 256],
    len: usize,
}
impl RenderMessage {
    fn append(&mut self, value: &[u8]) {
        let count = value.len().min(self.bytes.len() - self.len);
        self.bytes[self.len..self.len + count].copy_from_slice(&value[..count]);
        self.len += count;
    }
}
impl fmt::Write for RenderMessage {
    fn write_str(&mut self, value: &str) -> fmt::Result {
        self.append(value.as_bytes());
        Ok(())
    }
}

struct BuildLoan<'a, 'b>(&'a ResourceAccess<'b>);
impl BuildLoan<'_, '_> {
    fn end(&self) {
        let owner = self.0.owner;
        unsafe { (owner.leaves.loan_end)(owner.native(self.0)) };
    }
}
impl Drop for BuildLoan<'_, '_> {
    fn drop(&mut self) {
        let owner = self.0.owner;
        unsafe { (owner.leaves.loan_drop)(owner.native(self.0)) };
    }
}

pub(super) unsafe extern "C" fn build_popup(context: *mut c_void) {
    let owner = &*context.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.show_build_popup(&access);
}

unsafe extern "C" fn indexing_popup(context: *mut c_void) {
    let owner = &*context.cast::<EpubSession>();
    if owner.book_geometry().has_framebuffer != 0 {
        owner.book_popup(ids::STR_INDEXING);
    }
}

impl EpubSession {
    fn book_log(&self, severity: u8, text: &[u8]) {
        if unsafe { (self.leaves.log_enabled)(severity) } != 0 {
            unsafe {
                (self.leaves.log)(
                    severity,
                    app_abi::Text::from_bytes(b"ERS"),
                    app_abi::Text::from_bytes(text),
                )
            };
        }
    }

    fn book_log_formatted(&self, prefix: &[&[u8]], arguments: fmt::Arguments<'_>) {
        if unsafe { (self.leaves.log_enabled)(0) } != 0 {
            self.book_format_log(prefix, arguments);
        }
    }

    // Both this buffer and the native logPrintf frame are charged to render stack.
    #[inline(never)]
    fn book_format_log(&self, prefix: &[&[u8]], arguments: fmt::Arguments<'_>) {
        let mut text = RenderMessage {
            bytes: [0; 256],
            len: 0,
        };
        for part in prefix {
            text.append(part);
        }
        let _ = fmt::write(&mut text, arguments);
        unsafe {
            (self.leaves.log)(
                0,
                app_abi::Text::from_bytes(b"ERS"),
                app_abi::Text::from_bytes(&text.bytes[..text.len]),
            )
        };
    }

    fn book_loan<'a, 'b>(&self, access: &'a ResourceAccess<'b>) -> BuildLoan<'a, 'b> {
        unsafe { (self.leaves.loan_begin)(self.native(access)) };
        BuildLoan(access)
    }

    #[inline(never)]
    fn book_loading_log(&self, access: &ResourceAccess<'_>) {
        unsafe extern "C" fn receive(context: *mut c_void, item: *const abi::XpEpubSpine) -> u8 {
            let owner = &*context.cast::<EpubSession>();
            let path = (*item).href.bytes().unwrap_or(&[]);
            let spine = owner.model(|state| state.current.spine);
            owner.book_log_formatted(
                &[b"Loading file: ", path],
                format_args!(", index: {}", spine),
            );
            1
        }
        unsafe {
            (self.leaves.book_spine)(
                self.book(access).unwrap().as_ptr(),
                self.model(|state| state.current.spine),
                receive,
                (self as *const Self).cast_mut().cast(),
            );
        }
    }

    fn book_initial_section(
        &self,
        access: &ResourceAccess<'_>,
        spec: &abi::XpEpubRenderSpec,
    ) -> bool {
        // The borrowed href is consumed by its log before construction; no second
        // unbounded path allocation is kept through the build.
        self.book_loading_log(access);
        let spine = self.model(|state| state.current.spine);
        let section = NonNull::new(unsafe {
            (self.leaves.section_open)(self.book(access).unwrap().as_ptr(), spine)
        });
        unsafe { (*self.guarded(access)).section = section };
        self.publish_section(access);
        self.model(|state| state.partial_start_failed = false);
        let Some(section) = section else {
            self.book_error(access, false);
            return false;
        };
        let cached = unsafe { (self.leaves.section_load_cache)(section.as_ptr(), spec) != 0 };
        self.publish_section(access);
        if cached {
            self.model(|state| {
                state.cached.pages = 0;
                state.cached.offset = None;
            });
        }
        let complete = cached && !self.book_partial(access);
        let explicit_offset = self.model(|state| state.deferred.offset);
        let offset = if explicit_offset.is_some() {
            explicit_offset
        } else if self.model(|state| state.deferred.page.is_some())
            || self.book_has_anchor(access)
            || self.model(|state| state.current.spine != state.cached.spine)
        {
            None
        } else {
            self.model(|state| state.cached.offset)
        };
        if !complete {
            if self.book_partial(access) {
                if unsafe { (self.leaves.log_enabled)(0) } != 0 {
                    self.book_log_formatted(
                        &[],
                        format_args!(
                            "Partial cache found ({} pages), resuming build...",
                            self.book_page_count(access)
                        ),
                    );
                }
            } else {
                self.book_log(0, b"Cache not found, building...");
            }
            if self.model(|state| state.deferred.percent) {
                self.book_popup(ids::STR_INDEXING);
                self.set_render_refresh_counter(access, 1);
                if !self.book_room(access) {
                    self.drop_section(access);
                    self.book_error(access, true);
                    return false;
                }
                let loan = self.book_loan(access);
                let created = unsafe {
                    (self.leaves.section_create_cache)(
                        section.as_ptr(),
                        spec,
                        indexing_popup,
                        (self as *const Self).cast_mut().cast(),
                    ) != 0
                };
                self.publish_section(access);
                if !created {
                    self.book_log(1, b"Failed to persist page data to SD");
                    self.drop_section(access);
                    loan.end();
                    self.book_error(access, false);
                    return false;
                }
                loan.end();
            } else {
                let target = self.model(|state| {
                    state
                        .deferred
                        .page
                        .map_or(state.next_page.max(0), i32::from)
                });
                let anchor = self.book_has_anchor(access);
                let covered = self.book_partial(access)
                    && if anchor {
                        self.book_anchor(access, true).is_some()
                    } else {
                        target.wrapping_add(PARTIAL_REBUILD_START_MARGIN)
                            < self.book_page_count(access)
                    };
                if covered {
                    if unsafe { (self.leaves.log_enabled)(0) } != 0 {
                        self.book_log_formatted(
                            &[],
                            format_args!(
                                "Partial covers target {} of {}; deferring extension build",
                                target,
                                self.book_page_count(access)
                            ),
                        );
                    }
                } else {
                    let book = self.book(access).unwrap().as_ptr();
                    let bytes = unsafe { (self.leaves.book_cumulative_size)(book, spine) }
                        .wrapping_sub(if spine > 0 {
                            unsafe { (self.leaves.book_cumulative_size)(book, spine - 1) }
                        } else {
                            0
                        });
                    let inflate =
                        unsafe { (self.leaves.section_html_cached)(section.as_ptr()) == 0 };
                    let popup = if anchor {
                        self.book_anchor(access, false).is_none()
                            && bytes > BUILD_POPUP_BYTE_THRESHOLD
                    } else {
                        target >= self.book_page_count(access)
                            && ((bytes > BUILD_POPUP_BYTE_THRESHOLD && inflate)
                                || target > BUILD_POPUP_PAGE_THRESHOLD)
                    };
                    if popup {
                        self.book_popup(ids::STR_INDEXING);
                        self.set_render_refresh_counter(access, 1);
                    }
                    self.model(|state| state.build_popup = !popup);
                    if self.book_geometry().has_ttf != 0 {
                        unsafe { (self.leaves.fonts_release_caches)() };
                    }
                    if !self.book_room(access) {
                        self.drop_section(access);
                        self.model(|state| state.build_popup = false);
                        self.book_error(access, true);
                        return false;
                    }
                    if unsafe { (self.leaves.log_enabled)(0) } != 0 {
                        let mut heap = MaybeUninit::<abi::XpEpubHeap>::uninit();
                        unsafe { (self.leaves.heap_snapshot)(heap.as_mut_ptr()) };
                        let heap = unsafe { heap.assume_init() };
                        self.book_log_formatted(
                            &[],
                            format_args!(
                                "Heap before section build: {} (max block {})",
                                heap.free_bytes, heap.largest_block
                            ),
                        );
                    }
                    let started_at = unsafe { (self.runtime.now_ms)() };
                    let started = {
                        let _loan = self.book_loan(access);
                        let started = unsafe {
                            (self.leaves.section_start_build)(
                                section.as_ptr(),
                                spec,
                                Some(build_popup),
                                (self as *const Self).cast_mut().cast(),
                            ) != 0
                        };
                        self.publish_section(access);
                        started
                    };
                    if !started {
                        self.book_log(1, b"Failed to start section build");
                        self.drop_section(access);
                        self.model(|state| state.build_popup = false);
                        self.book_error(access, false);
                        return false;
                    }
                    while !self.book_complete(access)
                        && if anchor {
                            self.book_anchor(access, false).is_none()
                        } else if let Some(offset) = offset {
                            unsafe {
                                (self.leaves.section_reached_offset)(section.as_ptr(), offset) == 0
                            }
                        } else {
                            self.book_page_count(access) <= target
                        }
                    {
                        if self.model(|state| state.build_popup)
                            && unsafe { (self.runtime.now_ms)() }.wrapping_sub(started_at)
                                >= BUILD_POPUP_DEADLINE_MS
                        {
                            self.show_build_popup(access);
                        }
                        if !self.book_build_more(access) {
                            self.book_log(1, b"Failed during incremental section build");
                            self.drop_section(access);
                            self.model(|state| state.build_popup = false);
                            self.book_error(access, false);
                            return false;
                        }
                    }
                    self.model(|state| state.build_popup = false);
                }
            }
        } else {
            self.book_log(0, b"Cache found, skipping build...");
        }
        self.model(|state| {
            state.current.page = state
                .deferred
                .page
                .take()
                .map_or(state.next_page.max(0), i32::from)
        });
        if let Some(offset) = offset {
            let mut page = 0;
            if unsafe {
                (self.leaves.section_page_for_offset)(section.as_ptr(), offset, 0, &mut page)
            } != 0
            {
                self.model(|state| state.current.page = i32::from(page));
                self.book_clear_deferred();
            }
        }
        if explicit_offset.is_some() {
            self.book_clear_deferred();
        }
        self.model(|state| state.deferred.offset = None);
        if self.book_has_anchor(access) {
            if let Some(page) = self.book_anchor(access, false) {
                self.model(|state| state.current.page = i32::from(page));
                let anchor = unsafe { &(*self.guarded(access)).anchor };
                self.book_log_formatted(
                    &[b"Resolved anchor '", anchor.bytes()],
                    format_args!("' to page {}", page),
                );
            }
            unsafe { (*self.guarded(access)).anchor.clear() };
        }
        if self.model(|state| state.deferred.percent) && self.book_page_count(access) > 0 {
            let count = self.book_page_count(access);
            self.model(|state| {
                let mut page = (state.deferred.spine_progress * count as f32) as i32;
                if page >= count {
                    page = count - 1;
                }
                state.current.page = page;
                state.deferred.percent = false;
            });
        }
        true
    }

    fn book_geometry(&self) -> abi::XpEpubGeometry {
        let mut geometry = MaybeUninit::uninit();
        unsafe {
            (self.leaves.render_geometry)(geometry.as_mut_ptr());
            geometry.assume_init()
        }
    }

    // Preparation ends before nested page drawing starts.
    #[inline(never)]
    fn book_viewport(&self, out: &mut MaybeUninit<Viewport>) {
        let mut margins = [0; 4];
        unsafe { (self.leaves.render_margins)(margins.as_mut_ptr()) };
        let margin = i32::from(self.settings(false).screen_margin);
        let geometry = self.book_geometry();
        for index in [0, 1, 3] {
            margins[index] = margins[index].wrapping_add(margin);
        }
        let status = geometry.status_height as u8;
        let extra = if self.model(|state| state.auto_turn)
            && (status == 0 || i32::from(status) == geometry.progress_height)
        {
            margin.max(i32::from(status.wrapping_add(geometry.status_margin as u8)))
        } else {
            margin.max(i32::from(status))
        };
        margins[2] = margins[2].wrapping_add(extra);
        let width = geometry
            .width
            .wrapping_sub(margins[3])
            .wrapping_sub(margins[1]) as u16;
        let height = geometry
            .height
            .wrapping_sub(margins[0])
            .wrapping_sub(margins[2]) as u16;
        self.model(|state| state.viewport = (width, height));
        unsafe {
            let ptr = out.as_mut_ptr();
            core::ptr::addr_of_mut!((*ptr).margins).write(margins);
            (self.leaves.settings_render_spec)(width, height, core::ptr::addr_of_mut!((*ptr).spec));
        }
    }

    fn book_popup(&self, label: u16) {
        unsafe { (self.resources.popup_draw)((self.resources.translate)(label)) };
    }

    fn book_clear(&self, access: &ResourceAccess<'_>) {
        self.render_operation(
            access,
            Op {
                kind: Kind::Clear as u8,
                value: 255,
                ..Op::default()
            },
        );
    }

    fn book_display(&self, access: &ResourceAccess<'_>) {
        self.render_operation(
            access,
            Op {
                kind: Kind::Display as u8,
                mode: Refresh::Fast as u8,
                ..Op::default()
            },
        );
    }

    fn book_error(&self, access: &ResourceAccess<'_>, radio: bool) {
        self.book_clear(access);
        let empty = app_abi::Text::from_bytes(&[]);
        let hints = [
            unsafe { (self.resources.translate)(ids::STR_BACK) },
            empty,
            empty,
            empty,
        ];
        unsafe { (self.resources.footer_draw)(hints.as_ptr()) };
        self.book_popup(if radio {
            ids::STR_BT_BUILD_REFUSED
        } else {
            ids::STR_INDEX_FAILED
        });
        self.model(|state| state.auto_turn = false);
    }

    #[inline(never)]
    fn book_error_text(&self, label: u16) {
        let geometry = self.book_geometry();
        let text = unsafe { (self.resources.translate)(label) };
        let width = unsafe { (self.resources.text_width)(text, geometry.ui_font_12, 1) };
        let command = Command {
            rect: Rect {
                x: ((geometry.width - width) / 2) as i16,
                y: 300,
                w: 0,
                h: 0,
            },
            font: geometry.ui_font_12,
            kind: xp_ui::scene::Kind::Text as u8,
            paint: 0,
            style: 1,
            ..Command::default()
        };
        let clip = Rect {
            x: 0,
            y: 0,
            w: geometry.width as i16,
            h: geometry.height as i16,
        };
        unsafe { (self.resources.text_draw)(&command, clip, text) };
    }

    fn book_sync_error(&self) {
        if self.model(|state| core::mem::take(&mut state.sync_save_error)) {
            self.book_popup(ids::STR_SAVE_PROGRESS_FAILED);
        }
    }

    fn book_room(&self, access: &ResourceAccess<'_>) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            return unsafe { (hook.room)(hook.context) != 0 };
        }
        self.make_build_room(access)
    }

    fn book_status(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.status)(hook.context) };
            return;
        }
        self.render_status_bar(access);
    }

    fn book_clear_deferred(&self) {
        self.clear_deferred();
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.clear)(hook.context) };
        }
    }

    fn book_apply_deferred(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.deferred)(hook.context) };
            return;
        }
        self.apply_deferred(access);
    }

    fn book_bookmark(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.bookmark)(hook.context) };
            return;
        }
        self.update_bookmark_flag(access);
    }

    fn book_settle(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.settle)(hook.context) };
            return;
        }
        self.settle_overlay_refresh(access);
    }

    fn book_contents(&self, access: &ResourceAccess<'_>, page: Page, viewport: &Viewport) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe {
                (hook.contents)(
                    hook.context,
                    page.handle.as_ptr(),
                    viewport.margins.as_ptr(),
                )
            };
            drop(page);
            return;
        }
        self.render_contents(
            access,
            page,
            viewport.spec.font,
            viewport.margins,
            cfg!(feature = "reader-strips"),
        );
    }

    fn book_save(&self, access: &ResourceAccess<'_>, spine: i32, page: i32, count: i32) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            return unsafe { (hook.save)(hook.context, spine, page, count) != 0 };
        }
        self.save_progress(access, spine, page, count)
    }

    fn book_uses_toolbar(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            return unsafe { (hook.toolbar)(hook.context) != 0 };
        }
        self.settings(false).menu_style == 1
    }

    fn book_overlay(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.overlay)(hook.context) };
            return;
        }
        self.render_overlay(access);
    }

    fn book_push(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.push)(hook.context) };
            return;
        }
        self.push_overlay_refresh(access);
    }

    fn book_page_count(&self, access: &ResourceAccess<'_>) -> i32 {
        unsafe {
            (self.leaves.section_page_count)(self.section(access).expect("book section").as_ptr())
                .into()
        }
    }

    fn book_building(&self, access: &ResourceAccess<'_>) -> bool {
        unsafe {
            (self.leaves.section_building)(self.section(access).expect("book section").as_ptr())
                != 0
        }
    }

    fn book_partial(&self, access: &ResourceAccess<'_>) -> bool {
        unsafe {
            (self.leaves.section_partial)(self.section(access).expect("book section").as_ptr()) != 0
        }
    }

    fn book_complete(&self, access: &ResourceAccess<'_>) -> bool {
        unsafe {
            (self.leaves.section_complete)(self.section(access).expect("book section").as_ptr())
                != 0
        }
    }

    fn book_build_more(&self, access: &ResourceAccess<'_>) -> bool {
        let result = unsafe {
            (self.leaves.section_build_more)(
                self.section(access).expect("book section").as_ptr(),
                8,
                0,
            ) != 0
        };
        self.publish_section(access);
        result
    }

    fn book_anchor(&self, access: &ResourceAccess<'_>, cached: bool) -> Option<u16> {
        let anchor = unsafe { (*self.guarded(access)).anchor.wire() };
        let leaf = if cached {
            self.leaves.section_cached_anchor
        } else {
            self.leaves.section_find_anchor
        };
        let mut page = 0;
        (unsafe { leaf(self.section(access)?.as_ptr(), anchor, &mut page) } != 0).then_some(page)
    }

    fn book_has_anchor(&self, access: &ResourceAccess<'_>) -> bool {
        !unsafe { &(*self.guarded(access)).anchor }
            .bytes()
            .is_empty()
    }

    fn book_build_to_page(&self, access: &ResourceAccess<'_>) -> bool {
        while !self.book_complete(access)
            && self.model(|state| state.current.page) >= self.book_page_count(access)
        {
            if !self.book_build_more(access) {
                self.book_log(1, b"Failed during incremental section build");
                self.drop_section(access);
                self.book_error(access, false);
                return false;
            }
        }
        true
    }

    fn book_extend(&self, access: &ResourceAccess<'_>, spec: &abi::XpEpubRenderSpec) -> bool {
        if (self.book_partial(access) || self.book_building(access))
            && self.model(|state| state.current.page) >= self.book_page_count(access)
            && !self.book_room(access)
        {
            self.drop_section(access);
            self.book_error(access, true);
            return false;
        }
        if self.book_partial(access)
            && self.model(|state| state.current.page) >= self.book_page_count(access)
        {
            self.book_popup(ids::STR_INDEXING);
            self.set_render_refresh_counter(access, 1);
        }
        while self.book_partial(access)
            && self.model(|state| state.current.page) >= self.book_page_count(access)
        {
            if !self.book_building(access) {
                let started = unsafe {
                    (self.leaves.section_start_build)(
                        self.section(access).unwrap().as_ptr(),
                        spec,
                        None,
                        core::ptr::null_mut(),
                    ) != 0
                };
                self.publish_section(access);
                if !started {
                    self.book_log(1, b"Failed to start partial extension build");
                    self.drop_section(access);
                    self.book_error(access, false);
                    return false;
                }
            }
            if !self.book_build_to_page(access) {
                return false;
            }
        }
        !self.book_building(access) || self.book_build_to_page(access)
    }

    fn book_render_page(&self, access: &ResourceAccess<'_>, viewport: &Viewport) -> bool {
        let section = self.section(access).expect("book section");
        let index = self.model(|state| state.current.page);
        let page = unsafe {
            Page::from_raw(
                (self.leaves.section_load_page)(section.as_ptr(), index),
                self.leaves,
            )
        };
        if page.as_ref().is_none_or(|page| unsafe { (self.leaves.metadata_assign)(self.native(access), page.handle.as_ptr()) } == 0) {
            self.book_log(1, if page.is_none() { b"Failed to load page from SD - clearing section cache" } else { b"Failed to copy page metadata - clearing section cache" });
            let give_up = self.model(|state| {
                state.auto_turn = false;
                state.page_retries = state.page_retries.wrapping_add(1);
                state.page_retries > 3
            });
            unsafe { (self.leaves.section_abandon_build)(section.as_ptr()); (self.leaves.section_clear_cache)(section.as_ptr()); }
            self.drop_section(access);
            if give_up {
                self.book_log(1, b"Page load retry limit reached, aborting");
                self.model(|state| state.page_retries = 0);
                self.book_clear(access);
                self.book_error_text(ids::STR_PAGE_LOAD_ERROR);
                self.book_display(access);
            } else { self.request_update(); }
            self.book_sync_error();
            return false;
        }
        let page = page.unwrap();
        let mut info = MaybeUninit::<abi::XpEpubPageInfo>::uninit();
        unsafe { (self.leaves.page_info)(page.handle.as_ptr(), info.as_mut_ptr()) };
        self.model(|state| {
            state.page_retries = 0;
            state.visible_offset = Some(unsafe { (*info.as_ptr()).visible_offset });
            state.link_margins = (viewport.margins[3], viewport.margins[0]);
        });
        self.discard_overlay_page(access);
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.discard)(hook.context) };
        }
        let start = unsafe { (self.runtime.now_ms)() };
        self.book_contents(access, page, viewport);
        if unsafe { (self.leaves.log_enabled)(0) } != 0 {
            let elapsed = unsafe { (self.runtime.now_ms)() }.wrapping_sub(start) as i32;
            self.book_log_formatted(&[], format_args!("Rendered page in {}ms", elapsed));
        }
        let now = unsafe { (self.runtime.now_ms)() };
        self.model(|state| state.last_render_ms = now);
        self.mark_page_rendered(access);
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.book_render_hooks {
            unsafe { (hook.mark)(hook.context) };
        }
        true
    }

    fn book_notices(&self) {
        self.book_sync_error();
        if self.model(|state| core::mem::take(&mut state.screenshot)) {
            unsafe { (self.runtime.screenshot)() };
        }
        if self.model(|state| state.bookmark_notice.is_some()) {
            self.book_popup(if self.model(|state| state.bookmark_removed) {
                ids::STR_BOOKMARK_REMOVED
            } else {
                ids::STR_BOOKMARK_ADDED
            });
        }
        if self.model(|state| state.dictionary_notice.is_some()) {
            self.book_popup(ids::STR_DICT_NO_DICT_SET);
        }
        if unsafe { (self.leaves.fonts_take_corrupt_notice)() } != 0 {
            let since = unsafe { (self.runtime.now_ms)() };
            self.model(|state| {
                state.font_notice = Some(TimedNotice {
                    message: Notice::FontDamaged,
                    since,
                })
            });
        }
        if self.model(|state| state.font_notice.is_some()) {
            self.book_popup(ids::STR_FONT_DAMAGED);
        }
        if let Some(message) =
            self.model(|state| state.clipping_notice.map(|notice| notice.message))
        {
            self.book_popup(match message {
                Notice::ClipSaved => ids::STR_CLIPPING_SAVED,
                Notice::ClipRemoved => ids::STR_CLIPPING_REMOVED,
                Notice::ClipLimit => ids::STR_CLIPPING_LIMIT_REACHED,
                Notice::ClipFailed => ids::STR_CLIPPING_FAILED,
                Notice::ClipDeleteFailed => ids::STR_CLIPPING_DELETE_FAILED,
                Notice::ClipExportFailed => ids::STR_CLIPPING_EXPORT_FAILED,
                _ => unreachable!("clipping notice"),
            });
        }
    }

    pub(super) fn render_book(&self, access: &ResourceAccess<'_>) {
        unsafe { (self.leaves.metadata_clear)(self.native(access)) };
        let Some(book) = self.book(access) else {
            return;
        };
        unsafe { (self.leaves.radio_settle_start)() };
        self.book_settle(access);
        self.model(|state| state.current.spine = state.current.spine.max(0));
        let count = unsafe { (self.leaves.book_spine_count)(book.as_ptr()) };
        self.model(|state| state.current.spine = state.current.spine.min(count));
        if self.model(|state| state.current.spine) == count {
            return;
        }
        let mut viewport = MaybeUninit::uninit();
        self.book_viewport(&mut viewport);
        let viewport = unsafe { viewport.assume_init() };
        if self.section(access).is_none() && !self.book_initial_section(access, &viewport.spec) {
            return;
        }
        if !self.book_extend(access, &viewport.spec) {
            return;
        }
        if !self.book_building(access)
            && self.book_page_count(access) > 0
            && self.model(|state| state.current.page) >= self.book_page_count(access)
        {
            let page = self.book_page_count(access) - 1;
            self.model(|state| state.current.page = page);
        }
        self.book_apply_deferred(access);
        self.book_clear(access);
        if self.book_page_count(access) == 0 {
            self.book_log(0, b"No pages to render");
            self.book_error_text(ids::STR_EMPTY_CHAPTER);
            self.book_status(access);
            self.book_display(access);
            self.model(|state| {
                state.facts.frame_stale = false;
                state.auto_turn = false;
            });
            self.book_sync_error();
            return;
        }
        let page = self.model(|state| state.current.page);
        if page < 0 || page as u32 >= self.book_page_count(access) as u32 {
            if unsafe { (self.leaves.log_enabled)(0) } != 0 {
                self.book_log_formatted(
                    &[],
                    format_args!(
                        "Page out of bounds: {} (max {})",
                        page,
                        self.book_page_count(access)
                    ),
                );
            }
            self.book_error_text(ids::STR_OUT_OF_BOUNDS);
            self.book_status(access);
            self.book_display(access);
            self.model(|state| state.auto_turn = false);
            self.book_sync_error();
            return;
        }
        self.book_bookmark(access);
        if !self.book_render_page(access, &viewport) {
            return;
        }
        let (spine, page, saved) =
            self.model(|state| (state.current.spine, state.current.page, state.last_saved));
        if spine != saved.0 || page != saved.1 || self.book_page_count(access) != saved.2 {
            let section = self.section(access).unwrap();
            let estimated = unsafe { (self.leaves.section_estimated_pages)(section.as_ptr()) };
            if self.book_save(access, spine, page, estimated) {
                let page = self.model(|state| state.current.page);
                let estimated = unsafe { (self.leaves.section_estimated_pages)(section.as_ptr()) };
                self.model(|state| state.last_saved = (state.current.spine, page, estimated));
            }
        }
        self.book_notices();
        self.model(|state| state.facts.frame_stale = false);
        if self.model(|state| !matches!(state.overlay, Overlay::Page)) && self.book_uses_toolbar() {
            let stored = self.render_operation(
                access,
                Op {
                    kind: Kind::StoreBw as u8,
                    ..Op::default()
                },
            ) != 0;
            self.model(|state| state.page_stored = stored);
            self.book_overlay(access);
            if let Some(popup) = unsafe { (*self.guarded(access)).overlay_popup } {
                if unsafe { (self.leaves.popup_active)(popup.as_ptr()) } != 0 {
                    unsafe { (self.leaves.popup_paint)(popup.as_ptr()) };
                }
            }
            self.book_push(access);
        }
    }
}

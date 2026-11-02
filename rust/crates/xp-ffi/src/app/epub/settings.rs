extern crate alloc;

use super::{abi, app_abi, state::PopupIntent, EpubSession, ResourceAccess};
use alloc::vec::Vec;
use core::{ffi::c_void, mem::MaybeUninit};
use xp_core::i18n::ids;

const PAGE_TURN_RATES: [u32; 5] = [1, 1, 3, 6, 12];
const SPACING_IDS: [u16; 4] = [
    ids::STR_TIGHT,
    ids::STR_NORMAL,
    ids::STR_WIDE,
    ids::STR_EXTRA_WIDE,
];
const ALIGNMENT_IDS: [u16; 5] = [
    ids::STR_JUSTIFY,
    ids::STR_ALIGN_LEFT,
    ids::STR_CENTER,
    ids::STR_ALIGN_RIGHT,
    ids::STR_BOOK_S_STYLE,
];

pub(super) enum Labels<'a> {
    Sizes(&'a [u8]),
    Translated(&'a [u16]),
    Borrowed(&'a [app_abi::Text]),
}

pub(super) struct LabelSource<'a> {
    pub(super) owner: &'a EpubSession,
    pub(super) labels: Labels<'a>,
}

pub(super) unsafe extern "C" fn label_at(
    context: *mut c_void,
    index: u32,
    sink: app_abi::TextSink,
    sink_context: *mut c_void,
) -> u8 {
    let source = &*context.cast::<LabelSource<'_>>();
    match source.labels {
        Labels::Sizes(sizes) => {
            let Some(&size) = sizes.get(index as usize) else {
                return 0;
            };
            let mut label = [0u8; 7];
            let mut length = 0;
            if size >= 100 {
                label[length] = b'0' + size / 100;
                length += 1;
            }
            if size >= 10 {
                label[length] = b'0' + (size / 10) % 10;
                length += 1;
            }
            label[length] = b'0' + size % 10;
            length += 1;
            label[length..length + 3].copy_from_slice(b" pt");
            sink(
                sink_context,
                app_abi::Text::from_bytes(&label[..length + 3]),
            )
        }
        Labels::Translated(ids) => {
            let Some(&id) = ids.get(index as usize) else {
                return 0;
            };
            sink(sink_context, (source.owner.resources.translate)(id))
        }
        Labels::Borrowed(rows) => rows
            .get(index as usize)
            .map_or(0, |&text| sink(sink_context, text)),
    }
}

unsafe extern "C" fn text_choice(context: *mut c_void, index: i32) {
    (&*context.cast::<EpubSession>()).choose_text_setting(index);
}

fn nearest_size(sizes: &[u8], current: u8) -> u8 {
    let Some(&first) = sizes.first() else {
        return current;
    };
    let mut best = first;
    for &size in &sizes[1..] {
        if size.abs_diff(current) < best.abs_diff(current) {
            best = size;
        }
    }
    best
}

impl EpubSession {
    pub(super) fn cache_current_position(&self, access: &ResourceAccess<'_>) {
        let Some(section) = self.section(access) else {
            return;
        };
        self.remember_offset(access);
        let pages = unsafe { (self.leaves.section_page_count)(section.as_ptr()) };
        self.model(|state| {
            state.cached.spine = state.current.spine;
            state.cached.pages = i32::from(pages);
            state.next_page = state.current.page;
        });
    }

    pub(super) fn apply_reader_initial_orientation(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.orientation)(hook.context) };
            return;
        }
        self.apply_initial_orientation();
        let orientation = self.settings(false).orientation;
        self.model(|state| state.orientation = orientation);
    }

    pub(super) fn apply_orientation(&self, orientation: u8) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.orientation)(hooks.context, orientation) };
            return;
        }
        if self.settings(false).orientation == orientation
            && self.model(|state| state.orientation) == orientation
        {
            return;
        }
        let access = self.lock_resources();
        self.cache_current_position(&access);
        if self.settings(false).orientation != orientation {
            unsafe {
                (self.leaves.settings_orientation)(orientation);
                (self.leaves.settings_save)();
            }
        }
        unsafe { (self.leaves.render_orientation)(self.settings(false).orientation) };
        self.model(|state| state.orientation = orientation);
        self.drop_section(&access);
    }

    pub(super) fn toggle_auto_page_turn(&self, choice: u8) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.automatic)(hooks.context, choice) };
            return;
        }
        let rate = PAGE_TURN_RATES.get(usize::from(choice)).copied();
        if choice == 0 || rate.is_none() {
            self.model(|state| state.auto_turn = false);
            return;
        }
        let now = unsafe { (self.runtime.now_ms)() };
        self.model(|state| {
            state.last_turn_ms = now;
            state.turn_duration_ms = 60000 / rate.expect("valid automatic turn rate");
            state.auto_turn = true;
        });
        let mut geometry = MaybeUninit::<abi::XpEpubGeometry>::uninit();
        unsafe { (self.leaves.render_geometry)(geometry.as_mut_ptr()) };
        let geometry = unsafe { geometry.assume_init() };
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.settings_overrides {
            unsafe { (hook.status_height)(hook.context, geometry.status_height) };
        }
        let reflow = if geometry.status_height == 0 {
            true
        } else {
            #[cfg(feature = "app-test")]
            if let Some(hook) = self.settings_overrides {
                unsafe { (hook.progress_height)(hook.context, geometry.progress_height) };
            }
            geometry.status_height == geometry.progress_height
        };
        if reflow {
            let access = self.lock_resources();
            self.cache_current_position(&access);
            self.drop_section(&access);
        }
    }

    pub(super) fn apply_text_settings(&self) {
        {
            let access = self.lock_resources();
            self.cache_current_position(&access);
            self.drop_section(&access);
        }
        unsafe { (self.leaves.settings_save)() };
        let _access = self.lock_resources();
        unsafe { (self.leaves.fonts_ensure_loaded)() };
    }

    pub(super) fn apply_text_setting_live(&self) {
        self.apply_text_settings();
        {
            let access = self.lock_resources();
            self.discard_overlay_page(&access);
        }
        unsafe { (self.runtime.notify_render)() };
    }

    fn font_choices(&self, family: app_abi::Text) -> Option<Vec<u8>> {
        unsafe extern "C" fn copy(context: *mut c_void, text: app_abi::Text) -> u8 {
            let Some(bytes) = text.bytes() else { return 0 };
            let sizes = &mut *context.cast::<Vec<u8>>();
            if sizes.try_reserve_exact(bytes.len()).is_err() {
                return 0;
            }
            sizes.extend_from_slice(bytes);
            1
        }
        let mut sizes = Vec::new();
        // This replaces the sizes captured by the old native popup callback.
        let loaded =
            unsafe { (self.leaves.font_sizes)(family, copy, (&mut sizes as *mut Vec<u8>).cast()) };
        (loaded != 0).then_some(sizes)
    }

    fn show_text_choices(
        &self,
        access: &ResourceAccess<'_>,
        title: u16,
        selected: i32,
        labels: Labels<'_>,
    ) {
        let count = match labels {
            Labels::Sizes(rows) => rows.len(),
            Labels::Translated(rows) => rows.len(),
            Labels::Borrowed(rows) => rows.len(),
        };
        let mut source = LabelSource {
            owner: self,
            labels,
        };
        let popup = unsafe {
            (*self.guarded(access))
                .overlay_popup
                .expect("overlay popup")
        };
        unsafe {
            (self.leaves.popup_show)(
                popup.as_ptr(),
                (self.resources.translate)(title),
                app_abi::Text::from_bytes(&b"\0"[..0]),
                count as u32,
                selected,
                label_at,
                (&mut source as *mut LabelSource<'_>).cast(),
                text_choice,
                (self as *const Self).cast_mut().cast(),
            );
        }
    }

    fn text_popup_access(&self) -> Option<ResourceAccess<'_>> {
        let access = self.lock_resources();
        if self.model(|state| state.scene_gate.failed) {
            drop(access);
            self.request_update();
            return None;
        }
        Some(access)
    }

    pub(super) fn show_text_row_popup(&self, row: i32) {
        match row {
            1 => {
                let settings = self.settings(false);
                let Some(sizes) = self.font_choices(settings.font_family) else {
                    return;
                };
                if sizes.is_empty() || sizes.len() > i32::MAX as usize {
                    return;
                }
                #[cfg(feature = "app-test")]
                if let Some(hook) = self.settings_overrides {
                    unsafe { (hook.snap)(hook.context, settings.font_size) };
                }
                let current = nearest_size(&sizes, settings.font_size);
                let mut selected = 0;
                for (index, &size) in sizes.iter().enumerate() {
                    if size == current {
                        selected = index as i32;
                    }
                }
                {
                    let Some(access) = self.text_popup_access() else {
                        return;
                    };
                    self.model(|state| state.popup = PopupIntent::FontSize);
                    self.show_text_choices(
                        &access,
                        ids::STR_FONT_SIZE,
                        selected,
                        Labels::Sizes(&sizes),
                    );
                    // Only the loop reads or replaces this retained callback payload.
                    unsafe { (*self.guarded(&access)).font_sizes = sizes };
                }
            }
            2 => {
                let selected = usize::from(self.settings(false).line_spacing) % SPACING_IDS.len();
                let Some(access) = self.text_popup_access() else {
                    return;
                };
                self.model(|state| state.popup = PopupIntent::Spacing);
                self.show_text_choices(
                    &access,
                    ids::STR_LINE_SPACING,
                    selected as i32,
                    Labels::Translated(&SPACING_IDS),
                );
                unsafe { (*self.guarded(&access)).font_sizes = Vec::new() };
            }
            3 => {
                let selected = usize::from(self.settings(false).alignment) % ALIGNMENT_IDS.len();
                let Some(access) = self.text_popup_access() else {
                    return;
                };
                self.model(|state| state.popup = PopupIntent::Alignment);
                self.show_text_choices(
                    &access,
                    ids::STR_PARA_ALIGNMENT,
                    selected as i32,
                    Labels::Translated(&ALIGNMENT_IDS),
                );
                unsafe { (*self.guarded(&access)).font_sizes = Vec::new() };
            }
            _ => return,
        }
        self.paint_overlay_popup();
    }

    pub(super) fn choose_text_setting(&self, index: i32) {
        match self.model(|state| state.popup) {
            PopupIntent::FontSize => {
                let size = usize::try_from(index).ok().and_then(|index| unsafe {
                    let sizes = &(*self.guarded.get()).font_sizes;
                    sizes.get(index).copied()
                });
                let Some(size) = size else { return };
                unsafe { (self.leaves.settings_font_size)(size) };
            }
            PopupIntent::Spacing => unsafe { (self.leaves.settings_line_spacing)(index as u8) },
            PopupIntent::Alignment => unsafe { (self.leaves.settings_alignment)(index as u8) },
            _ => return,
        }
        self.apply_text_setting_live();
    }
}

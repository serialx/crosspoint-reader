use super::session_section::Section as ReaderSection;
use super::{abi, app_abi, state::Overlay, EpubSession, ResourceAccess};
use core::{ffi::c_void, mem::MaybeUninit};
use xp_core::i18n::ids;

#[derive(Clone, Copy)]
enum RowPart {
    TextName,
    TextValue,
    MoreName,
    MoreValue,
}
struct Rows<'a, 'b> {
    access: &'a ResourceAccess<'b>,
    mode: Overlay,
}

fn number(out: &mut [u8], value: i32) -> usize {
    let mut digits = [0u8; 10];
    let mut remaining = value.unsigned_abs();
    let mut count = 0;
    loop {
        digits[count] = b'0' + (remaining % 10) as u8;
        count += 1;
        remaining /= 10;
        if remaining == 0 {
            break;
        }
    }
    let mut used = 0;
    if value < 0 {
        out[0] = b'-';
        used = 1;
    }
    for digit in digits[..count].iter().rev() {
        out[used] = *digit;
        used += 1;
    }
    used
}

unsafe extern "C" fn row(
    context: *mut c_void,
    index: i32,
    sink: abi::XpEpubToolbarRowSink,
    sink_context: *mut c_void,
) -> u8 {
    let rows = &*context.cast::<Rows<'_, '_>>();
    rows.access
        .owner
        .fill_overlay_row(rows.access, rows.mode, index, sink, sink_context)
}

impl EpubSession {
    pub(super) fn publish_reader_scene(&self) {
        self.model(|state| state.scene_gate.failed = false);
    }

    pub(super) fn reject_reader_scene(&self) {
        self.model(|state| {
            state.scene_gate.failed = true;
            state.scene_gate.suppress_buttons = u16::MAX;
            state.scene_gate.suppress_contact = true;
            state.panel_hold_jumped = false;
        });
    }

    fn overlay_chapter_title<R>(
        &self,
        access: &ResourceAccess<'_>,
        consume: impl FnOnce(app_abi::Text) -> R,
    ) -> Option<R> {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.overlay_view_hooks {
            return super::book::with_text(
                |sink, context| unsafe { (hook.chapter)(hook.context, sink, context) },
                consume,
            );
        }
        self.current_chapter_title(access, consume)
    }

    fn overlay_row_text<R>(
        &self,
        access: &ResourceAccess<'_>,
        part: RowPart,
        index: i32,
        consume: impl FnOnce(app_abi::Text) -> R,
    ) -> Option<R> {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.overlay_view_hooks {
            let read = match part {
                RowPart::TextName => hook.text_name,
                RowPart::TextValue => hook.text_value,
                RowPart::MoreName => hook.more_name,
                RowPart::MoreValue => hook.more_value,
            };
            return super::book::with_text(
                |sink, context| unsafe { read(hook.context, index, sink, context) },
                consume,
            );
        }
        let value = match part {
            RowPart::TextName => self.text_row_name(access, index),
            RowPart::TextValue => self.text_row_value(access, index),
            RowPart::MoreName => self.more_row_name(access, index),
            RowPart::MoreValue => self.more_row_value(access, index),
        };
        Some(consume(value.wire()))
    }

    fn overlay_checkbox(&self, access: &ResourceAccess<'_>, mode: Overlay, index: i32) -> (u8, u8) {
        match mode {
            Overlay::Text if index == 4 => (1, u8::from(self.settings(false).focus_reading != 0)),
            Overlay::More | Overlay::Page => {
                let Some(action) = self.more_action(access, index) else {
                    return (0, 0);
                };
                match action {
                    3 => (1, u8::from(self.settings(false).screen_inverted != 0)),
                    5 => (1, u8::from(self.settings(false).ble_enabled != 0)),
                    4 => (
                        1,
                        u8::from(unsafe { (self.runtime.frontlight_state)() } != 0),
                    ),
                    _ => (0, 0),
                }
            }
            _ => (0, 0),
        }
    }

    fn fill_overlay_row(
        &self,
        access: &ResourceAccess<'_>,
        mode: Overlay,
        index: i32,
        sink: abi::XpEpubToolbarRowSink,
        context: *mut c_void,
    ) -> u8 {
        if matches!(mode, Overlay::Contents) {
            struct TocRow {
                sink: abi::XpEpubToolbarRowSink,
                context: *mut c_void,
            }
            unsafe extern "C" fn fill(raw: *mut c_void, item: *const abi::XpEpubToc) -> u8 {
                let call = &*raw.cast::<TocRow>();
                let item = &*item;
                let row = abi::XpEpubToolbarRow {
                    label: item.title,
                    value: app_abi::Text::from_bytes(&[]),
                    label_spaces: if item.level > 1 {
                        (item.level - 1) as u32 * 2
                    } else {
                        0
                    },
                    toggle: 0,
                    checked: 0,
                };
                (call.sink)(call.context, &row)
            }
            let Some(book) = self.book(access) else {
                return 0;
            };
            let mut call = TocRow { sink, context };
            return unsafe {
                (super::session_book::toc)(
                    book.as_ptr(),
                    index,
                    fill,
                    (&mut call as *mut TocRow).cast(),
                )
            };
        }
        let (name, value) = if matches!(mode, Overlay::Text) {
            (RowPart::TextName, RowPart::TextValue)
        } else {
            (RowPart::MoreName, RowPart::MoreValue)
        };
        let (toggle, checked) = self.overlay_checkbox(access, mode, index);
        self.overlay_row_text(access, name, index, |label| {
            self.overlay_row_text(access, value, index, |value| {
                let row = abi::XpEpubToolbarRow {
                    label,
                    value,
                    label_spaces: 0,
                    toggle,
                    checked,
                };
                unsafe { sink(context, &row) }
            })
            .unwrap_or(0)
        })
        .unwrap_or(0)
    }

    fn paint_overlay_model(
        &self,
        access: &ResourceAccess<'_>,
        mode: Overlay,
        model: &abi::XpEpubToolbarModel,
    ) {
        let toolbar = self.toolbar(access).expect("overlay toolbar");
        let mut rows = Rows { access, mode };
        if unsafe {
            (self.leaves.toolbar_paint)(
                toolbar.as_ptr(),
                model,
                row,
                (&mut rows as *mut Rows<'_, '_>).cast(),
            )
        } != 0
        {
            self.publish_reader_scene();
        } else {
            self.reject_reader_scene();
            unsafe {
                (self.leaves.log)(
                    1,
                    app_abi::Text::from_bytes(b"RUI"),
                    app_abi::Text::from_bytes(b"Cannot build reader toolbar"),
                )
            };
        }
    }

    pub(super) fn render_overlay(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_hooks {
            unsafe { (hooks.paint)(hooks.context) };
            return;
        }
        let Some(book) = self.book(access) else {
            return;
        };
        let Some(section) = self.section(access) else {
            return;
        };
        if self.toolbar(access).is_none() {
            return;
        }
        let (mode, cursor, focus) =
            self.model(|state| (state.overlay, state.panel_cursor, state.focused_tool));
        let empty = app_abi::Text {
            data: core::ptr::null(),
            len: 0,
        };
        let mut model = abi::XpEpubToolbarModel {
            chapter: empty,
            page_info: empty,
            panel_title: empty,
            progress_permille: 0,
            item_count: 0,
            selected: -1,
            active_tool: if matches!(mode, Overlay::Toolbar) && !cursor {
                -1
            } else {
                focus
            },
            panel: u8::from(!matches!(mode, Overlay::Toolbar)),
            dense: 0,
        };
        if matches!(mode, Overlay::Toolbar) {
            let painted = self.overlay_chapter_title(access, |chapter| {
                let count = unsafe { ReaderSection::estimated_pages(section) };
                let (page, spine) =
                    self.model(|state| (state.current.page.wrapping_add(1), state.current.spine));
                let fraction = if count > 0 {
                    page as f32 / count as f32
                } else {
                    0.0
                };
                let progress =
                    unsafe { (super::session_book::progress)(book.as_ptr(), spine, fraction) };
                let mut info = [0; 40];
                let mut used = number(&mut info, page);
                info[used] = b'/';
                used += 1;
                used += number(&mut info[used..], count);
                info[used..used + 3].copy_from_slice(b"   ");
                used += 3;
                used += number(
                    &mut info[used..],
                    ((progress * 100.0 + 0.5) as i32).clamp(0, 100),
                );
                info[used] = b'%';
                used += 1;
                model.chapter = chapter;
                model.page_info = app_abi::Text::from_terminated(&info[..=used]);
                model.progress_permille = (progress * 1000.0 + 0.5) as i32;
                self.paint_overlay_model(access, mode, &model);
            });
            if painted.is_none() {
                self.reject_reader_scene();
            }
            return;
        }
        let mut input = MaybeUninit::<abi::XpEpubInputFacts>::uninit();
        unsafe { (self.leaves.input_facts)(input.as_mut_ptr()) };
        model.dense = u8::from(unsafe { input.assume_init() }.touch == 0);
        model.selected = if cursor {
            self.model(|state| state.panel_index)
        } else {
            -1
        };
        let (title, count) = match mode {
            Overlay::Contents => (ids::STR_TOOL_CONTENTS, unsafe {
                (super::session_book::toc_count)(book.as_ptr())
            }),
            Overlay::Text => (ids::STR_TOOL_TEXT, 5),
            Overlay::Page | Overlay::More => {
                (ids::STR_TOOL_MORE, i32::from(self.more_count(access)))
            }
            Overlay::Toolbar => unreachable!(),
        };
        model.item_count = count;
        model.panel_title = unsafe { (self.resources.translate)(title) };
        self.paint_overlay_model(access, mode, &model);
    }
}

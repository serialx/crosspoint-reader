//! Reader-only C wire boundary. The shared runtime remains ABI 2.
use crate::reader_chrome::hits::Layout;
use crate::reader_chrome::{
    list::{self, Resources, Row, Screen, Theme},
    ListModel,
};
use crate::scene::Error;
use crate::{
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest},
    reader_chrome::{self, FootnoteState},
    scene::{Arena, Rect, Refresh, MAX_COMMAND_BYTES, MAX_HITS},
};
use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Rows {
    pub context: *mut c_void,
    pub window: Option<unsafe extern "C" fn(*mut c_void, u16, u16) -> u8>,
    pub row: Option<unsafe extern "C" fn(*mut c_void, u16, *mut Row) -> u8>,
    pub text_width: Option<unsafe extern "C" fn(*mut c_void, u32, i32, u8) -> i32>,
    pub text_height: Option<unsafe extern "C" fn(*mut c_void, u32, i32, u8, u8, i16) -> i32>,
    pub line_height: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
    pub count: u16,
    pub window_capacity: u16,
    pub generation: u32,
}

struct ResourceAdapter {
    rows: Rows,
    origin: *const Rows,
}
impl ResourceAdapter {
    fn unchanged(&self) -> Result<(), Error> {
        // No shared reference to the descriptor survives a callback.
        let generation = unsafe { core::ptr::addr_of!((*self.origin).generation).read() };
        if generation == self.rows.generation {
            Ok(())
        } else {
            Err(Error::StaleResource)
        }
    }
    fn measured(&self, value: i32) -> Result<i32, Error> {
        self.unchanged()?;
        if (0..=32767).contains(&value) {
            Ok(value)
        } else {
            Err(Error::Invalid)
        }
    }
}
impl Resources for ResourceAdapter {
    fn window(&mut self, first: u16, requested: u16) -> Result<(), Error> {
        if requested == 0
            || requested > self.rows.window_capacity
            || u32::from(first) + u32::from(requested) > u32::from(self.rows.count)
        {
            return Err(Error::Invalid);
        }
        let callback = self.rows.window.ok_or(Error::Invalid)?;
        if unsafe { callback(self.rows.context, first, requested) } == 0 {
            return Err(Error::Invalid);
        }
        self.unchanged()
    }
    fn row(&mut self, index: u16) -> Result<Row, Error> {
        let callback = self.rows.row.ok_or(Error::Invalid)?;
        let mut row = Row::default();
        if index >= self.rows.count || unsafe { callback(self.rows.context, index, &mut row) } == 0
        {
            return Err(Error::Invalid);
        }
        self.unchanged()?;
        if row.label == 0 || row.toggle > 1 || row.checked > 1 {
            return Err(Error::Invalid);
        }
        Ok(row)
    }
    fn width(&mut self, handle: u32, font: i32, bold: u8) -> Result<i32, Error> {
        let callback = self.rows.text_width.ok_or(Error::Invalid)?;
        self.measured(unsafe { callback(self.rows.context, handle, font, bold) })
    }
    fn height(
        &mut self,
        handle: u32,
        font: i32,
        bold: u8,
        lines: u8,
        width: i16,
    ) -> Result<i32, Error> {
        let callback = self.rows.text_height.ok_or(Error::Invalid)?;
        self.measured(unsafe { callback(self.rows.context, handle, font, bold, lines, width) })
    }
    fn line(&mut self, font: i32) -> Result<i32, Error> {
        let callback = self.rows.line_height.ok_or(Error::Invalid)?;
        let value = self.measured(unsafe { callback(self.rows.context, font) })?;
        if value == 0 || value > 8192 {
            return Err(Error::Invalid);
        }
        Ok(value)
    }
}

/// # Safety
/// A non-null state is aligned and writable for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_list_init(state: *mut ListModel, selected: i32) {
    if let Some(state) = unsafe { state.as_mut() } {
        *state = ListModel::new(selected);
    }
}
/// # Safety
/// A non-null state is aligned and writable for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_list_select(
    state: *mut ListModel,
    selected: i32,
    count: u16,
) {
    if let Some(state) = unsafe { state.as_mut() } {
        state
            .list
            .select(selected.clamp(0, (i32::from(count) - 1).max(0)));
    }
}
/// # Safety
/// A non-null state is aligned and readable for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_list_interest(
    state: *const ListModel,
    kind: u8,
    now: u32,
) -> Interest {
    match (unsafe { state.as_ref() }, Screen::from_byte(kind)) {
        (Some(state), Some(kind)) => state.interest(kind, now),
        _ => Interest::default(),
    }
}
/// # Safety
/// State is writable, input is readable, and both are aligned and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_list_update(
    state: *mut ListModel,
    input: *const Input,
    count: u16,
    present: u8,
    kind: u8,
    long_press: u8,
    swipe_row: i32,
) -> Effect {
    match (
        unsafe { state.as_mut() },
        unsafe { input.as_ref() },
        Screen::from_byte(kind),
    ) {
        (Some(state), Some(input), Some(kind)) => state.update(
            *input,
            count,
            present != 0,
            kind,
            long_press != 0,
            swipe_row,
        ),
        _ => Effect::default(),
    }
}
/// # Safety
/// State is writable and next is readable, aligned and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_list_publish(
    state: *mut ListModel,
    next: *const ListModel,
    scroll: i32,
) {
    if let (Some(state), Some(next)) = (unsafe { state.as_mut() }, unsafe { next.as_ref() }) {
        state.list.publish(&next.list, scroll);
    }
}

/// # Safety
/// Descriptors are aligned and live. Callback data stays valid for the call,
/// with no reentry. Arrays and writable outputs are exclusive and disjoint
/// from all descriptors. Scratch may change on failure; outputs do not.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_list_view(
    state: *const ListModel,
    kind: u8,
    theme: *const Theme,
    rows: *const Rows,
    buffer: *const Buffer,
    next_state: *mut ListModel,
    output: *mut SceneView,
    layout_output: *mut Layout,
) -> u8 {
    let (Some(state), Some(kind), Some(theme), Some(buffer)) = (
        unsafe { state.as_ref() },
        Screen::from_byte(kind),
        unsafe { theme.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if rows.is_null()
        || next_state.is_null()
        || output.is_null()
        || layout_output.is_null()
        || buffer.commands.is_null()
        || usize::from(buffer.command_capacity) > MAX_COMMAND_BYTES / 32
        || buffer.hit_capacity != 0
    {
        return 0;
    }
    let rows_copy = unsafe { rows.read() };
    if rows_copy.window.is_none()
        || rows_copy.row.is_none()
        || rows_copy.text_width.is_none()
        || rows_copy.text_height.is_none()
        || rows_copy.line_height.is_none()
        || rows_copy.window_capacity == 0
        || rows_copy.window_capacity > 64
    {
        return 0;
    }
    let mut resources = ResourceAdapter {
        rows: rows_copy,
        origin: rows,
    };
    let mut next = *state;
    for pass in 0..9 {
        let commands = unsafe {
            core::slice::from_raw_parts_mut(buffer.commands, usize::from(buffer.command_capacity))
        };

        let mut layout = Layout::new(
            if kind == Screen::Panel { 6 } else { 1 },
            resources.rows.generation,
        );
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut layout) else {
            return 0;
        };
        let result = list::view(
            &mut arena,
            next.list,
            kind,
            *theme,
            &mut resources,
            rows_copy.count,
            rows_copy.window_capacity,
            rows_copy.generation,
            next.revealed,
        );
        let list_next = match result {
            Ok(value) => value,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        next.list = list_next;
        if list_next.follow != 0 && pass < 8 && matches!(kind, Screen::Chapters | Screen::Clippings)
        {
            continue;
        }
        if resources.unchanged().is_err() {
            return 0;
        }
        let Ok(scene) = arena.finish(Refresh::Fast, rows_copy.generation) else {
            return 0;
        };
        unsafe {
            next_state.write(next);
            output.write(SceneView {
                commands: buffer.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: rows_copy.generation,
                command_count: scene.commands.len() as u16,
                text_len: 0,
                interaction_count: 0,
                refresh: 1,
                reserved: 0,
            });
            layout_output.write(layout);
        }
        return 1;
    }
    0
}

const _: () = {
    assert!(core::mem::size_of::<FootnoteState>() == 8);
    assert!(core::mem::align_of::<FootnoteState>() == 4);
    assert!(core::mem::offset_of!(FootnoteState, selected) == 0);
    assert!(core::mem::offset_of!(FootnoteState, last_move_ms) == 4);
};

/// # Safety
/// A non-null state points to an aligned, readable screen state for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_footnote_interest(
    state: *const FootnoteState,
    now: u32,
) -> Interest {
    unsafe { state.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}

/// # Safety
/// State is writable and input is readable, live, aligned and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_footnote_update(
    state: *mut FootnoteState,
    input: *const Input,
    count: u16,
) -> Effect {
    match (unsafe { state.as_mut() }, unsafe { input.as_ref() }) {
        (Some(state), Some(input)) => state.update(*input, count),
        _ => Effect::default(),
    }
}

/// # Safety
/// Descriptors and output are live and aligned. Buffer arrays are exclusive
/// and disjoint from every descriptor. Outputs remain unchanged on failure.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_footnote_view(
    screen: Rect,
    link: Rect,
    margin_left: i16,
    margin_top: i16,
    font: i32,
    number_handle: u32,
    buffer: *const Buffer,
    snapshot: *mut Rect,
    output: *mut SceneView,
) -> u8 {
    let (Some(buffer), Some(snapshot), Some(output)) = (
        unsafe { buffer.as_ref() },
        unsafe { snapshot.as_mut() },
        unsafe { output.as_mut() },
    ) else {
        return 0;
    };
    if buffer.commands.is_null()
        || usize::from(buffer.command_capacity) > MAX_COMMAND_BYTES / 32
        || usize::from(buffer.hit_capacity) > MAX_HITS
        || buffer.hit_capacity != 0 && buffer.hits.is_null()
    {
        return 0;
    }
    let commands = unsafe {
        core::slice::from_raw_parts_mut(buffer.commands, usize::from(buffer.command_capacity))
    };
    let hits = if buffer.hit_capacity == 0 {
        &mut []
    } else {
        unsafe { core::slice::from_raw_parts_mut(buffer.hits, usize::from(buffer.hit_capacity)) }
    };
    let Ok(mut arena) = Arena::new(commands, &mut [], hits) else {
        return 0;
    };
    let Ok(rect) = reader_chrome::footnote_view(
        &mut arena,
        screen,
        link,
        margin_left,
        margin_top,
        font,
        number_handle,
    ) else {
        return 0;
    };
    let Ok(scene) = arena.finish(Refresh::None, 1) else {
        return 0;
    };
    *snapshot = rect;
    *output = SceneView {
        commands: buffer.commands,
        text: core::ptr::null(),
        interactions: buffer.hits,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: 0,
        generation: 1,
        refresh: 0,
        reserved: 0,
    };
    1
}

const _: () = {
    assert!(reader_chrome::ACTIONS > crate::model::EffectKind::Scroll as u8);
    assert!(reader_chrome::DELETE > reader_chrome::ACTIONS);
    assert!(core::mem::size_of::<ListModel>() == 48);
    assert!(core::mem::align_of::<ListModel>() == 4);
    assert!(core::mem::offset_of!(ListModel, list) == 0);
    assert!(core::mem::offset_of!(ListModel, revealed) == 40);
    assert!(core::mem::offset_of!(ListModel, long_press_consumed) == 44);
    assert!(core::mem::offset_of!(ListModel, reserved) == 45);
    assert!(core::mem::size_of::<Theme>() == 72);
    assert!(core::mem::align_of::<Theme>() == 4);
    assert!(core::mem::offset_of!(Theme, screen) == 0);
    assert!(core::mem::offset_of!(Theme, safe) == 8);
    assert!(core::mem::offset_of!(Theme, body_font) == 16);
    assert!(core::mem::offset_of!(Theme, small_font) == 20);
    assert!(core::mem::offset_of!(Theme, title_font) == 24);
    assert!(core::mem::offset_of!(Theme, fixed_title_font) == 28);
    assert!(core::mem::offset_of!(Theme, help_font) == 32);
    assert!(core::mem::offset_of!(Theme, row_min) == 36);
    assert!(core::mem::offset_of!(Theme, row_gap) == 38);
    assert!(core::mem::offset_of!(Theme, row_inset) == 40);
    assert!(core::mem::offset_of!(Theme, row_padding) == 42);
    assert!(core::mem::offset_of!(Theme, scroll_width) == 44);
    assert!(core::mem::offset_of!(Theme, top_padding) == 46);
    assert!(core::mem::offset_of!(Theme, header_height) == 48);
    assert!(core::mem::offset_of!(Theme, spacing) == 50);
    assert!(core::mem::offset_of!(Theme, tab_height) == 52);
    assert!(core::mem::offset_of!(Theme, header_padding) == 54);
    assert!(core::mem::offset_of!(Theme, space_sm) == 56);
    assert!(core::mem::offset_of!(Theme, space_md) == 58);
    assert!(core::mem::offset_of!(Theme, space_lg) == 60);
    assert!(core::mem::offset_of!(Theme, row_radius) == 62);
    assert!(core::mem::offset_of!(Theme, selection) == 63);
    assert!(core::mem::offset_of!(Theme, scroll_left) == 64);
    assert!(core::mem::offset_of!(Theme, title_bold) == 65);
    assert!(core::mem::offset_of!(Theme, control_radius) == 66);
    assert!(core::mem::offset_of!(Theme, sheet_radius) == 67);
    assert!(core::mem::offset_of!(Theme, touch) == 68);
    assert!(core::mem::offset_of!(Theme, orientation) == 69);
    assert!(core::mem::size_of::<Row>() == 20);
    assert!(core::mem::align_of::<Row>() == 4);
    assert!(core::mem::offset_of!(Row, label) == 0);
    assert!(core::mem::offset_of!(Row, subtitle) == 4);
    assert!(core::mem::offset_of!(Row, value) == 8);
    assert!(core::mem::offset_of!(Row, icon) == 12);
    assert!(core::mem::offset_of!(Row, toggle) == 16);
    assert!(core::mem::offset_of!(Row, checked) == 17);
    assert!(core::mem::offset_of!(Row, reserved) == 18);
    assert!(core::mem::size_of::<FootnoteState>() == 8);
    assert!(core::mem::align_of::<FootnoteState>() == 4);
    assert!(core::mem::offset_of!(FootnoteState, selected) == 0);
    assert!(core::mem::offset_of!(FootnoteState, last_move_ms) == 4);
    assert!(core::mem::size_of::<Rows>() == core::mem::size_of::<usize>() * 6 + 8);
    assert!(core::mem::align_of::<Rows>() == core::mem::align_of::<usize>());
    assert!(core::mem::offset_of!(Rows, context) == 0);
    assert!(core::mem::offset_of!(Rows, window) == core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Rows, row) == core::mem::size_of::<usize>() * 2);
    assert!(core::mem::offset_of!(Rows, text_width) == core::mem::size_of::<usize>() * 3);
    assert!(core::mem::offset_of!(Rows, text_height) == core::mem::size_of::<usize>() * 4);
    assert!(core::mem::offset_of!(Rows, line_height) == core::mem::size_of::<usize>() * 5);
    assert!(core::mem::offset_of!(Rows, count) == core::mem::size_of::<usize>() * 6);
    assert!(core::mem::offset_of!(Rows, window_capacity) == core::mem::size_of::<usize>() * 6 + 2);
    assert!(core::mem::offset_of!(Rows, generation) == core::mem::size_of::<usize>() * 6 + 4);
};

/// # Safety
/// State and input are live, aligned, writable/readable respectively and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_end_update(
    state: *mut crate::model::ListState,
    input: *const Input,
    count: u16,
    use_press: u8,
) -> Effect {
    match (unsafe { state.as_mut() }, unsafe { input.as_ref() }) {
        (Some(state), Some(input)) => {
            reader_chrome::end_update(state, *input, count, use_press != 0)
        }
        _ => Effect::default(),
    }
}
/// # Safety
/// Descriptors and arrays are live/aligned and callback data remains borrowed
/// for this call. Writable arrays and outputs are exclusive and disjoint from
/// descriptors. Scratch may change on failure; outputs do not.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_toolbar_view(
    state: *const crate::model::ListState,
    model: *const reader_chrome::toolbar::Model,
    theme: *const Theme,
    rows: *const Rows,
    buffer: *const Buffer,
    next_state: *mut crate::model::ListState,
    output: *mut SceneView,
    layout_output: *mut Layout,
) -> u8 {
    let (Some(state), Some(model), Some(theme), Some(buffer)) = (
        unsafe { state.as_ref() },
        unsafe { model.as_ref() },
        unsafe { theme.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if rows.is_null()
        || next_state.is_null()
        || output.is_null()
        || layout_output.is_null()
        || buffer.commands.is_null()
        || usize::from(buffer.command_capacity) > MAX_COMMAND_BYTES / 32
        || buffer.hit_capacity != 0
    {
        return 0;
    }
    let row_copy = unsafe { rows.read() };
    if row_copy.window.is_none()
        || row_copy.row.is_none()
        || row_copy.text_width.is_none()
        || row_copy.text_height.is_none()
        || row_copy.line_height.is_none()
        || row_copy.window_capacity == 0
        || row_copy.window_capacity > 64
    {
        return 0;
    }
    let mut resources = ResourceAdapter {
        rows: row_copy,
        origin: rows,
    };
    let mut next = *state;
    for pass in 0..4 {
        let commands = unsafe {
            core::slice::from_raw_parts_mut(buffer.commands, usize::from(buffer.command_capacity))
        };

        let mut layout = Layout::new(6, resources.rows.generation);
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut layout) else {
            return 0;
        };
        let result = reader_chrome::toolbar::view(
            &mut arena,
            next,
            *model,
            *theme,
            &mut resources,
            row_copy.count,
            row_copy.window_capacity,
            row_copy.generation,
        );
        let result = match result {
            Ok(value) => value,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        next = result;
        if next.follow != 0 && model.panel != 0 && pass < 3 {
            continue;
        }
        if resources.unchanged().is_err() {
            return 0;
        }
        let Ok(scene) = arena.finish(Refresh::None, row_copy.generation) else {
            return 0;
        };
        unsafe {
            next_state.write(next);
            output.write(SceneView {
                commands: buffer.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: row_copy.generation,
                command_count: scene.commands.len() as u16,
                text_len: 0,
                interaction_count: 0,
                refresh: 0,
                reserved: 0,
            });
            layout_output.write(layout);
        }
        return 1;
    }
    0
}
/// # Safety
/// State and permille are exclusive writable values. Layout/input are readable,
/// aligned and disjoint from the outputs.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_toolbar_route(
    state: *mut reader_chrome::toolbar::Contact,
    layout: *const Layout,
    input: *const Input,
    permille: *mut i32,
) -> Effect {
    let (Some(state), Some(layout), Some(input), Some(permille)) = (
        unsafe { state.as_mut() },
        unsafe { layout.as_ref() },
        unsafe { input.as_ref() },
        unsafe { permille.as_mut() },
    ) else {
        return Effect::default();
    };
    if !layout.valid() {
        return Effect::default();
    }
    state.route(layout, *input, permille)
}
/// # Safety
/// Layout is live/readable and output is live/writable, aligned and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_hit(
    layout: *const Layout,
    x: i16,
    y: i16,
    output: *mut crate::scene::Interaction,
) -> u8 {
    let (Some(layout), Some(output)) = (unsafe { layout.as_ref() }, unsafe { output.as_mut() })
    else {
        return 0;
    };
    let Some(hit) = layout.hit(x, y) else {
        return 0;
    };
    *output = hit;
    1
}
const _: () = {
    use reader_chrome::toolbar::{Contact, Model};
    assert!(core::mem::size_of::<Model>() == 24);
    assert!(core::mem::align_of::<Model>() == 4);
    assert!(core::mem::offset_of!(Model, panel) == 0);
    assert!(core::mem::offset_of!(Model, dense_rows) == 1);
    assert!(core::mem::offset_of!(Model, reserved) == 2);
    assert!(core::mem::offset_of!(Model, active_tool) == 4);
    assert!(core::mem::offset_of!(Model, progress_permille) == 8);
    assert!(core::mem::offset_of!(Model, chapter_title) == 12);
    assert!(core::mem::offset_of!(Model, page_info) == 16);
    assert!(core::mem::offset_of!(Model, panel_title) == 20);
    assert!(core::mem::size_of::<Contact>() == 8);
    assert!(core::mem::align_of::<Contact>() == 2);
    assert!(core::mem::offset_of!(Contact, active) == 0);
    assert!(core::mem::offset_of!(Contact, last_drag_x) == 2);
    assert!(core::mem::offset_of!(Contact, contact_held) == 4);
    assert!(core::mem::offset_of!(Contact, reserved) == 5);
};

/// # Safety
/// A non-null state is writable, live and aligned.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_menu_init(
    state: *mut reader_chrome::menu::State,
    orientation: u8,
) {
    if let Some(state) = unsafe { state.as_mut() } {
        *state = reader_chrome::menu::State::new(orientation);
    }
}
/// # Safety
/// A non-null state is readable, live and aligned.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_menu_activate(
    state: *const reader_chrome::menu::State,
    action: i32,
) -> reader_chrome::menu::Result {
    unsafe { state.as_ref() }.map_or_else(
        || reader_chrome::menu::State::default().invalid(),
        |s| s.activate(action),
    )
}
/// # Safety
/// A non-null state is writable, live and aligned.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_menu_choose(
    state: *mut reader_chrome::menu::State,
    option: u8,
    selection: i32,
) -> reader_chrome::menu::Result {
    unsafe { state.as_mut() }.map_or_else(
        || reader_chrome::menu::State::default().invalid(),
        |s| s.choose(option, selection),
    )
}
/// # Safety
/// A non-null state is readable, live and aligned.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_reader_menu_cancel(
    state: *const reader_chrome::menu::State,
) -> reader_chrome::menu::Result {
    unsafe { state.as_ref() }.map_or_else(
        || reader_chrome::menu::State::default().invalid(),
        |s| s.cancel(),
    )
}

const _: () = {
    assert!(core::mem::size_of::<crate::reader_chrome::hits::Control>() == 16);
    assert!(core::mem::align_of::<crate::reader_chrome::hits::Control>() == 4);
    assert!(core::mem::offset_of!(crate::reader_chrome::hits::Control, rect) == 0);
    assert!(core::mem::offset_of!(crate::reader_chrome::hits::Control, value) == 8);
    assert!(core::mem::offset_of!(crate::reader_chrome::hits::Control, action) == 12);
    assert!(core::mem::offset_of!(crate::reader_chrome::hits::Control, reserved) == 13);
};

const _: () = {
    assert!(core::mem::size_of::<Layout>() == 184);
    assert!(core::mem::align_of::<Layout>() == 4);
    assert!(core::mem::offset_of!(Layout, first_row) == 0);
    assert!(core::mem::offset_of!(Layout, first_index) == 8);
    assert!(core::mem::offset_of!(Layout, gap) == 12);
    assert!(core::mem::offset_of!(Layout, row_count) == 14);
    assert!(core::mem::offset_of!(Layout, row_action) == 15);
    assert!(core::mem::offset_of!(Layout, heights) == 16);
    assert!(core::mem::offset_of!(Layout, controls) == 64);
    assert!(core::mem::offset_of!(Layout, generation) == 176);
    assert!(core::mem::offset_of!(Layout, control_count) == 180);
    assert!(core::mem::offset_of!(Layout, uniform) == 181);
    assert!(core::mem::offset_of!(Layout, reserved) == 182);
};

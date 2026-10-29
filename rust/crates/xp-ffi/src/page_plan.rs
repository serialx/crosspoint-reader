//! Caller-owned word placements and primitive order, valid for one frozen page.
use super::*;
use core::mem::{align_of, size_of};

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
enum CommandKind {
    Text,
    Rect,
    Scaled,
    Image,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Command {
    bytes: *const u8,
    len: usize,
    values: [i32; 5],
    element: u16,
    kind: CommandKind,
    style: u8,
    start: i16,
    end: i16,
}
impl Command {
    fn new(kind: CommandKind, values: [i32; 5]) -> Self {
        Self {
            bytes: ptr::null(),
            len: 0,
            values,
            element: 0,
            kind,
            style: 0,
            start: i16::MAX,
            end: 0,
        }
    }
    pub fn text(t: Text<'_>) -> Self {
        Self {
            bytes: t.bytes.as_ptr(),
            len: t.bytes.len(),
            values: [t.font, t.x, t.y, i32::from(t.tracking), t.direction],
            element: 0,
            kind: CommandKind::Text,
            style: t.style,
            start: i16::MAX,
            end: 0,
        }
    }
    pub fn rect(x: i32, y: i32, w: i32, h: i32, shade: bool) -> Self {
        Self::new(CommandKind::Rect, [x, y, w, h, i32::from(shade)])
    }
    pub fn scaled(font: i32, text: &[u8], style: u8, scale: i32, x: i32, y: i32) -> Self {
        Self {
            bytes: text.as_ptr(),
            len: text.len(),
            values: [font, x, y, scale, 0],
            element: 0,
            kind: CommandKind::Scaled,
            style,
            start: i16::MAX,
            end: 0,
        }
    }
    pub fn image(index: u16, x: i32, y: i32, placeholder: bool) -> Self {
        Self::new(
            CommandKind::Image,
            [i32::from(index), x, y, i32::from(placeholder), 0],
        )
    }
    unsafe fn draw(self, renderer: &mut Renderer<'_>, images_only: bool) {
        if images_only && self.kind != CommandKind::Image {
            return;
        }
        let v = self.values;
        match self.kind {
            CommandKind::Text => renderer.text(Text {
                font: v[0],
                bytes: slice::from_raw_parts(self.bytes, self.len),
                x: v[1],
                y: v[2],
                tracking: v[3] as i8,
                direction: v[4],
                style: self.style,
            }),
            CommandKind::Rect => renderer.rect(v[0], v[1], v[2], v[3], v[4] != 0),
            CommandKind::Scaled => renderer.scaled(
                v[0],
                slice::from_raw_parts(self.bytes, self.len),
                self.style,
                v[3],
                v[1],
                v[2],
            ),
            CommandKind::Image => renderer.image(v[0] as u16, v[1], v[2], v[3] != 0),
        }
    }
}
#[repr(C)]
pub struct Plan {
    storage: *mut Command,
    capacity_bytes: usize,
    count: usize,
    valid: u8,
}
impl Plan {
    pub(super) fn add(&mut self, target: &xp_core::glyph::GlyphTarget, rect: xp_core::glyph::Clip) {
        if self.count == 0 || self.count > self.capacity_bytes / size_of::<Command>() {
            return;
        }
        let (start, end) = xp_core::strip::row_bounds(target, rect);
        let command = unsafe { &mut *self.storage.add(self.count - 1) };
        command.start = command.start.min(start.clamp(0, target.height) as i16);
        command.end = command.end.max(end.clamp(0, target.height) as i16);
    }
    pub(super) fn unbounded(&mut self, height: i32) {
        if self.count == 0 || self.count > self.capacity_bytes / size_of::<Command>() {
            return;
        }
        let command = unsafe { &mut *self.storage.add(self.count - 1) };
        command.start = 0;
        command.end = height as i16;
    }
}
impl Renderer<'_> {
    pub(super) fn record_command(&mut self, mut command: Command) {
        let Some(plan) = self.plan.as_mut() else {
            return;
        };
        if let Some(strip) = self.strip.as_ref() {
            command.element = strip.current as u16;
            let target = unsafe { &(*self.paint).target };
            let (start, end) = match command.kind {
                CommandKind::Rect => {
                    let v = command.values;
                    xp_core::strip::row_bounds(
                        target,
                        xp_core::glyph::Clip {
                            left: v[0],
                            top: v[1],
                            right: v[0] + v[2],
                            bottom: v[1] + v[3],
                        },
                    )
                }
                CommandKind::Scaled => (0, target.height),
                CommandKind::Image => {
                    let b = strip.bounds[strip.current];
                    (b.start, b.end)
                }
                CommandKind::Text => (target.height, 0),
            };
            command.start = start.clamp(0, target.height) as i16;
            command.end = end.clamp(0, target.height) as i16;
        }
        if plan.count < plan.capacity_bytes / size_of::<Command>() {
            unsafe {
                plan.storage.add(plan.count).write(command);
            }
        }
        plan.count = plan.count.saturating_add(1);
    }
}
/// # Safety
/// All page render loans apply. Plan storage is aligned to a pointer and exclusively
/// writable for capacity_bytes. Prepared commands borrow the unchanged page arena;
/// fonts, origin and orientation must stay fixed until the plan is discarded.
#[allow(clippy::too_many_arguments)]
#[no_mangle]
pub unsafe extern "C" fn xp_page_plan_render(
    arena: *const u8,
    len: usize,
    provider: *const Provider,
    services: *const Services,
    paint: *mut GlyphPaint,
    font: i32,
    x: i32,
    y: i32,
    mode: u8,
    bounds: *mut strip_state::RowBounds,
    count: usize,
    plan: *mut Plan,
    prepare: u8,
) -> u32 {
    if prepare != 0 && !plan.is_null() {
        (*plan).valid = 0;
    }
    if mode > 2
        || arena.is_null()
        || provider.is_null()
        || services.is_null()
        || paint.is_null()
        || bounds.is_null()
        || plan.is_null()
    {
        return 1;
    }
    let plan = &mut *plan;
    if plan.capacity_bytes > isize::MAX as usize
        || plan.storage.is_null()
        || !(plan.storage as usize).is_multiple_of(align_of::<Command>())
        || !(*paint).target.valid()
        || (*paint).target.height > i32::from(i16::MAX)
    {
        return 1;
    }
    let Ok(page) = page::Page::open(slice::from_raw_parts(arena, len)) else {
        return 1;
    };
    let n = usize::from(page.element_count());
    if count < n {
        return 1;
    }
    let bounds = slice::from_raw_parts_mut(bounds, n);
    let mut stats = strip_state::Stats::default();
    if prepare != 0 {
        plan.valid = 0;
        plan.count = 0;
        bounds.fill(strip_state::RowBounds {
            start: (*paint).target.height,
            end: 0,
        });
    } else if plan.valid == 0 || plan.count > plan.capacity_bytes / size_of::<Command>() {
        return 1;
    }
    let state = strip_state::State {
        bounds,
        stats: &mut stats,
        prepare: prepare != 0,
        current: 0,
    };
    let mut renderer = Renderer {
        engine: Engine::new(*provider),
        provider: &*provider,
        services: &*services,
        paint,
        slot: ptr::null(),
        font: 0,
        style: 0,
        scan: [0; 192],
        scan_len: 0,
        scan_font: 0,
        scan_style: 0,
        observer: None,
        strip: Some(state),
        plan: None,
    };
    if prepare != 0 {
        renderer.plan = Some(plan);
        let mode = match mode {
            0 => Mode::All,
            1 => Mode::Images,
            2 => Mode::Placeholders,
            _ => return 1,
        };
        let result = page::render::page(page, &mut renderer, font, x, y, mode);
        let plan = renderer.plan.as_mut().unwrap();
        if result.is_err() || plan.count > plan.capacity_bytes / size_of::<Command>() {
            return 2;
        }
        plan.valid = 1;
    } else {
        for i in 0..plan.count {
            let command = *plan.storage.add(i);
            let state = renderer.strip.as_mut().unwrap();
            let Some(bound) = state.bounds.get(usize::from(command.element)) else {
                return 1;
            };
            let target = &(*paint).target;
            if bound.start >= target.origin_y + target.rows
                || bound.end <= target.origin_y
                || i32::from(command.start) >= target.origin_y + target.rows
                || i32::from(command.end) <= target.origin_y
            {
                continue;
            }
            state.current = usize::from(command.element);
            command.draw(&mut renderer, mode == 1);
        }
    }
    0
}

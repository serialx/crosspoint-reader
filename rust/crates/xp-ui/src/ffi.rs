//! Checked C wire descriptors. Allocation and framebuffer ownership stay outside.
use crate::{
    draw::{self, Sink},
    scene::{
        Command, Interaction, Kind, Rect, Refresh, Scene, MAX_COMMAND_BYTES, MAX_HITS,
        MAX_TEXT_BYTES,
    },
};
use core::{
    ffi::{c_char, c_void},
    slice,
};
use xp_core::{
    glyph::{Clip, GlyphTarget},
    strip::Strip,
};

#[repr(C)]
pub struct SceneView {
    pub commands: *const Command,
    pub text: *const u8,
    pub interactions: *const Interaction,
    pub generation: u32,
    pub command_count: u16,
    pub text_len: u16,
    pub interaction_count: u16,
    pub refresh: u8,
    pub reserved: u8,
}
#[repr(C)]
pub struct Metrics {
    pub context: *mut c_void,
    pub width: Option<unsafe extern "C" fn(*mut c_void, i32, *const u8, u16, u8) -> i32>,
    pub line_height: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
}
#[repr(C)]
pub struct Paint {
    pub context: *mut c_void,
    pub clear: Option<unsafe extern "C" fn(*mut c_void)>,
    pub text: Option<unsafe extern "C" fn(*mut c_void, *const Command, Rect, *const c_char)>,
    pub resource_valid: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u8>,
    pub resource: Option<unsafe extern "C" fn(*mut c_void, *const Command, Rect)>,
}
unsafe fn loan<'a, T>(ptr: *const T, count: usize, max: usize) -> Option<&'a [T]> {
    if count > max || (count != 0 && ptr.is_null()) {
        return None;
    }
    Some(if count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(ptr, count) }
    })
}
unsafe fn scene(view: &SceneView) -> Option<Scene<'_>> {
    Some(Scene {
        commands: unsafe {
            loan(
                view.commands,
                usize::from(view.command_count),
                MAX_COMMAND_BYTES / 32,
            )?
        },
        text: unsafe { loan(view.text, usize::from(view.text_len), MAX_TEXT_BYTES)? },
        interactions: unsafe {
            loan(
                view.interactions,
                usize::from(view.interaction_count),
                MAX_HITS,
            )?
        },
        generation: view.generation,
        refresh: match view.refresh {
            0 => Refresh::None,
            1 => Refresh::Fast,
            2 => Refresh::Half,
            3 => Refresh::Full,
            _ => return None,
        },
    })
}
struct Raster<'a> {
    target: &'a GlyphTarget,
    services: &'a Paint,
}
impl Sink for Raster<'_> {
    fn resource_valid(&mut self, handle: u32, generation: u32) -> bool {
        self.services.resource.is_some()
            && self.services.resource_valid.is_some_and(|valid| unsafe {
                valid(self.services.context, handle, generation) != 0
            })
    }
    fn paint(&mut self, command: &Command, clip: Rect, text: &[u8]) {
        match Kind::from_byte(command.kind) {
            Some(Kind::Clear) if self.services.clear.is_some() => {
                if let Some(clear) = self.services.clear {
                    unsafe { clear(self.services.context) };
                }
            }
            Some(Kind::Text) => {
                if let Some(paint) = self.services.text {
                    unsafe { paint(self.services.context, command, clip, text.as_ptr().cast()) };
                }
            }
            Some(Kind::Bitmap | Kind::Page | Kind::TextHandle | Kind::HintText) => {
                if let Some(paint) = self.services.resource {
                    unsafe { paint(self.services.context, command, clip) };
                }
            }
            _ => {
                let t = self.target;
                let target = GlyphTarget {
                    buffer: t.buffer,
                    buffer_len: t.buffer_len,
                    width: t.width,
                    height: t.height,
                    stride: t.stride,
                    origin_y: t.origin_y,
                    rows: t.rows,
                    clip: Clip {
                        left: t.clip.left.max(i32::from(clip.x)),
                        top: t.clip.top.max(i32::from(clip.y)),
                        right: t.clip.right.min(i32::from(clip.x) + i32::from(clip.w)),
                        bottom: t.clip.bottom.min(i32::from(clip.y) + i32::from(clip.h)),
                    },
                    orientation: t.orientation,
                    plane: t.plane,
                    absolute: t.absolute,
                    strip: t.strip,
                    scanning: t.scanning,
                };
                // The sole mutable loan ends before the next service callback.
                let bytes = unsafe { slice::from_raw_parts_mut(t.buffer, t.buffer_len as usize) };
                if let Some(mut strip) = Strip::new(target, bytes) {
                    draw::geometry(&mut strip, command);
                }
            }
        }
    }
}
/// # Safety
/// Descriptors and their buffers are aligned, live, and disjoint. Target storage
/// is exclusively writable. Services obey Paint's synchronous loan contract.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_render(
    view: *const SceneView,
    target: *const GlyphTarget,
    paint: *const Paint,
) -> u8 {
    let (Some(view), Some(target), Some(services)) = (
        unsafe { view.as_ref() },
        unsafe { target.as_ref() },
        unsafe { paint.as_ref() },
    ) else {
        return 0;
    };
    if !target.valid()
        || target.buffer.is_null()
        || target.width > i32::from(i16::MAX)
        || target.height > i32::from(i16::MAX)
    {
        return 0;
    }
    let Some(scene) = (unsafe { scene(view) }) else {
        return 0;
    };
    if services.text.is_none() && scene.commands.iter().any(|c| c.kind == Kind::Text as u8) {
        return 0;
    }
    let (w, h) = if target.orientation & 1 == 0 {
        (target.height, target.width)
    } else {
        (target.width, target.height)
    };
    u8::from(
        draw::render(
            &scene,
            Rect {
                x: 0,
                y: 0,
                w: w as i16,
                h: h as i16,
            },
            &mut Raster { target, services },
        )
        .is_ok(),
    )
}
/// # Safety
/// view lends live immutable arrays; out is writable, aligned, and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_hit(
    view: *const SceneView,
    x: i16,
    y: i16,
    inputs: u16,
    out: *mut Interaction,
) -> u8 {
    let (Some(view), Some(out)) = (unsafe { view.as_ref() }, unsafe { out.as_mut() }) else {
        return 0;
    };
    let Some(scene) = (unsafe { scene(view) }) else {
        return 0;
    };
    if let Some(hit) = scene.hit(x, y, inputs) {
        *out = hit;
        1
    } else {
        0
    }
}
/// # Safety
/// Text lends text_len+1 bytes, including its terminator. Output has capacity
/// aligned writable commands and does not alias text/metrics. Callbacks cannot
/// retain the text or mutate the output.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_message_view(
    metrics: *const Metrics,
    screen: Rect,
    font: i32,
    style: u8,
    text: *const u8,
    text_len: u16,
    commands: *mut Command,
    capacity: u16,
) -> u8 {
    let Some(metrics) = (unsafe { metrics.as_ref() }) else {
        return 0;
    };
    let (Some(width), Some(height)) = (metrics.width, metrics.line_height) else {
        return 0;
    };
    if capacity < 2
        || commands.is_null()
        || text.is_null()
        || usize::from(text_len) >= MAX_TEXT_BYTES
        || screen.w <= 0
        || screen.h <= 0
    {
        return 0;
    }
    let text_bytes = unsafe { slice::from_raw_parts(text, usize::from(text_len) + 1) };
    if text_bytes.last() != Some(&0) || text_bytes[..usize::from(text_len)].contains(&0) {
        return 0;
    }
    let width = unsafe { width(metrics.context, font, text, text_len, style) };
    let height = unsafe { height(metrics.context, font) };
    let x = (i64::from(screen.w) - i64::from(width)) / 2;
    let y = (i64::from(screen.h) - i64::from(height)) / 2;
    let (Ok(x), Ok(y)) = (i16::try_from(x), i16::try_from(y)) else {
        return 0;
    };
    let output = unsafe { slice::from_raw_parts_mut(commands, 2) };
    output[0] = Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    };
    output[1] = Command {
        kind: Kind::Text as u8,
        rect: Rect { x, y, w: 0, h: 0 },
        font,
        style,
        text_len,
        ..Command::default()
    };
    2
}

#[repr(C)]
pub struct Resources {
    pub context: *mut c_void,
    pub text: Option<unsafe extern "C" fn(*mut c_void, u32, *mut u16) -> *const u8>,
    pub page_range: Option<unsafe extern "C" fn(*mut c_void, u16, *mut u32, *mut u32)>,
    pub count: u16,
    pub present: u8,
    pub reserved: u8,
    pub generation: u32,
}
#[repr(C)]
pub struct Buffer {
    pub commands: *mut Command,
    pub hits: *mut Interaction,
    pub command_capacity: u16,
    pub hit_capacity: u16,
}
impl crate::model::Metrics for &Metrics {
    fn width(&mut self, font: i32, text: &[u8], style: u8) -> i32 {
        self.width.map_or(0, |f| unsafe {
            f(self.context, font, text.as_ptr(), text.len() as u16, style)
        })
    }
    fn line_height(&mut self, font: i32) -> i32 {
        self.line_height
            .map_or(0, |f| unsafe { f(self.context, font) })
    }
}
impl crate::xtc::Texts for Resources {
    fn text(&self, handle: u32) -> &[u8] {
        let mut length = 0;
        let Some(f) = self.text else {
            return &[];
        };
        let ptr = unsafe { f(self.context, handle, &mut length) };
        unsafe { loan(ptr, usize::from(length), MAX_TEXT_BYTES) }.unwrap_or(&[])
    }
}
/// # Safety
/// Resources and callbacks lend live immutable book metadata for the call.
/// state is aligned, writable and disjoint from those resources.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_list_init(
    state: *mut crate::model::ListState,
    resources: *const Resources,
    page: u32,
) -> u8 {
    let (Some(state), Some(resources)) = (unsafe { state.as_mut() }, unsafe { resources.as_ref() })
    else {
        return 0;
    };
    *state = crate::model::ListState::new(0);
    if let Some(range) = resources.page_range {
        for index in 0..resources.count {
            let (mut start, mut end) = (0, 0);
            unsafe { range(resources.context, index, &mut start, &mut end) };
            if page >= start && page <= end {
                state.selected = i32::from(index);
                break;
            }
        }
    }
    1
}
/// # Safety
/// state is writable and input is readable, aligned and disjoint for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_list_update(
    state: *mut crate::model::ListState,
    input: *const crate::model::Input,
    count: u16,
    present: u8,
) -> crate::model::Effect {
    match (unsafe { state.as_mut() }, unsafe { input.as_ref() }) {
        (Some(state), Some(input)) => state.update(*input, i32::from(count), present != 0),
        _ => crate::model::Effect::default(),
    }
}
/// # Safety
/// All descriptors are aligned and live. Buffer arrays are exclusive and
/// disjoint from state/output/resources. Callbacks are synchronous and cannot
/// reenter UI. On error scratch arrays may change; state/output do not change.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_xtc_view(
    state: *const crate::model::ListState,
    layout: *const crate::xtc::Layout,
    metrics: *const Metrics,
    resources: *const Resources,
    buffer: *const Buffer,
    next_state: *mut crate::model::ListState,
    output: *mut SceneView,
) -> u8 {
    let (
        Some(state),
        Some(layout),
        Some(mut metrics),
        Some(resources),
        Some(buffer),
        Some(next_state),
        Some(output),
    ) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { metrics.as_ref() },
        unsafe { resources.as_ref() },
        unsafe { buffer.as_ref() },
        unsafe { next_state.as_mut() },
        unsafe { output.as_mut() },
    )
    else {
        return 0;
    };
    if buffer.commands.is_null()
        || buffer.hits.is_null()
        || usize::from(buffer.command_capacity) > MAX_COMMAND_BYTES / 32
        || usize::from(buffer.hit_capacity) > MAX_HITS
        || metrics.width.is_none()
        || metrics.line_height.is_none()
        || resources.text.is_none()
    {
        return 0;
    }
    let commands =
        unsafe { slice::from_raw_parts_mut(buffer.commands, usize::from(buffer.command_capacity)) };
    let hits = unsafe { slice::from_raw_parts_mut(buffer.hits, usize::from(buffer.hit_capacity)) };
    let Ok(mut arena) = crate::scene::Arena::new(commands, &mut [], hits) else {
        return 0;
    };
    let book = crate::xtc::Book {
        count: resources.count,
        present: resources.present != 0,
        generation: resources.generation,
    };
    let Ok(next) = crate::xtc::view(&mut arena, *state, *layout, book, &mut metrics, resources)
    else {
        return 0;
    };
    let Ok(scene) = arena.finish(Refresh::Fast, resources.generation) else {
        return 0;
    };
    *output = SceneView {
        commands: buffer.commands,
        text: core::ptr::null(),
        interactions: buffer.hits,
        generation: resources.generation,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: scene.interactions.len() as u16,
        refresh: scene.refresh as u8,
        reserved: 0,
    };
    *next_state = next;
    1
}

/// # Safety
/// Same loans as xp_ui_render, held across all layers. Up to four immutable
/// scene descriptors share the target and resource services for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_render_layers(
    views: *const SceneView,
    count: u8,
    target: *const GlyphTarget,
    paint: *const Paint,
) -> u8 {
    let (Some(target), Some(services)) = (unsafe { target.as_ref() }, unsafe { paint.as_ref() })
    else {
        return 0;
    };
    if !target.valid()
        || target.buffer.is_null()
        || target.width > i32::from(i16::MAX)
        || target.height > i32::from(i16::MAX)
    {
        return 0;
    }
    let Some(views) = (unsafe { loan(views, usize::from(count), 4) }) else {
        return 0;
    };
    let (w, h) = if target.orientation & 1 == 0 {
        (target.height, target.width)
    } else {
        (target.width, target.height)
    };
    let screen = Rect {
        x: 0,
        y: 0,
        w: w as i16,
        h: h as i16,
    };
    for view in views {
        let Some(scene) = (unsafe { scene(view) }) else {
            return 0;
        };
        if services.text.is_none() && scene.commands.iter().any(|c| c.kind == Kind::Text as u8) {
            return 0;
        }
        if draw::validate(&scene, screen, &mut Raster { target, services }).is_err() {
            return 0;
        }
    }
    for view in views {
        if unsafe { xp_ui_render(view, target, services) } == 0 {
            return 0;
        }
    }
    1
}

/// # Safety
/// damage is an aligned writable descriptor, exclusively lent for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_damage_add(damage: *mut crate::scene::Damage, native_rect: Rect) {
    if let Some(damage) = unsafe { damage.as_mut() } {
        damage.add(native_rect);
    }
}

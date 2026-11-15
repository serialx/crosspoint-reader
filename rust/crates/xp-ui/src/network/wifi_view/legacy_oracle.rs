// Frozen parent FFI builder, retained only as an independent layout oracle.
use crate::{
    ffi::{Buffer, SceneView},
    model::ListState,
    network::{wifi, wifi_view},
    scene::{Arena, Error},
};
#[allow(clippy::too_many_arguments)]
pub(super) unsafe fn wifi_view(
    state: *const wifi::State,
    layout: *const wifi_view::Layout,
    services: *const wifi_view::Services,
    buffer: *const Buffer,
    next: *mut ListState,
    out: *mut SceneView,
    hits: *mut wifi_view::Hits,
) -> u8 {
    let (Some(s), Some(l), Some(c), Some(b)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { services.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if next.is_null()
        || out.is_null()
        || hits.is_null()
        || b.commands.is_null()
        || b.command_capacity > 256
        || b.hit_capacity != 0
        || c.rows.row.is_none()
        || c.rows.width.is_none()
        || c.rows.height.is_none()
        || c.rows.line.is_none()
        || c.text.is_none()
    {
        return 0;
    }
    let mut working = *s;
    let mut resources = *c;
    if s.phase == wifi::Phase::List as u8 {
        working.list.active = -1;
    }
    // Wi-Fi's original SDK caller paints once and retains follow feedback for
    // the next requested frame. Re-emit that same layout only for a held touch.
    for pass in 0..2 {
        let commands =
            unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
        let mut routing = wifi_view::Hits {
            generation: s.generation,
            count: s.count,
            phase: s.phase,
            ..wifi_view::Hits::default()
        };
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut routing) else {
            return 0;
        };
        let (mut updated, refresh) = match wifi_view::view(&mut arena, &working, l, &mut resources)
        {
            Ok(result) => result,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        let Ok(scene) = arena.finish(refresh, 1) else {
            return 0;
        };
        let count = scene.commands.len() as u16;
        if pass == 0
            && s.phase == wifi::Phase::List as u8
            && s.list.active >= 0
            && routing.rows.count != 0
        {
            working.list.active = s.list.active + routing.rows.first_index;
            continue;
        }
        updated.active = s.list.active;
        unsafe {
            next.write(updated);
            hits.write(routing);
            out.write(SceneView {
                commands: b.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: 1,
                command_count: count,
                text_len: 0,
                interaction_count: 0,
                refresh: refresh as u8,
                reserved: 0,
            });
        }
        return 1;
    }
    0
}

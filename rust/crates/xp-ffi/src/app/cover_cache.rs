use super::{
    abi,
    resources::{Bitmap, Buffer, Text},
};
use xp_ui::{
    home::cache,
    scene::{Command, Kind, Rect},
};

pub struct CoverCache {
    state: cache::State,
    buffer: Option<Buffer>,
    services: &'static abi::Resources,
}

impl CoverCache {
    pub fn new(services: &'static abi::Resources, screen: Rect) -> Self {
        let region = unsafe { (services.region_size)(screen) };
        let capacity = u32::try_from(region).ok().map_or(0, |region| {
            cache::State::capacity(region, screen.w.into(), screen.h.into())
        });
        // Grid snapshots retain their PSRAM-only domain; there is no internal fallback.
        let buffer = Buffer::allocate(services, abi::MEMORY_PSRAM_ONLY, capacity as usize);
        let state = cache::State {
            capacity: if buffer.is_some() { capacity } else { 0 },
            ..cache::State::default()
        };
        Self {
            state,
            buffer,
            services,
        }
    }

    pub fn prepare(&mut self, orientation: i32) {
        self.state.prepare(orientation);
    }

    pub fn invalidate(&mut self) {
        self.state.invalidate();
    }

    pub fn paint(&mut self, index: usize, rect: Rect, path: &Text) -> bool {
        if index >= cache::SLOTS {
            return false;
        }
        let span = self.state.lookup(index, rect);
        if self
            .buffer
            .as_ref()
            .is_some_and(|buffer| buffer.write(rect, span.offset as usize, span.bytes as usize))
        {
            return true;
        }
        self.state.invalidate_slot(index);
        let mut drawn = false;
        if !path.is_empty() {
            if let Some(mut bitmap) = Bitmap::open(self.services, path) {
                let info = bitmap.info();
                if info.valid != 0 && info.width > 0 && info.height > 0 {
                    drawn = bitmap.draw(rect, 3);
                    if drawn {
                        let art_left = i32::from(rect.x)
                            .max(i32::from(rect.x) + (i32::from(rect.w) - info.width) / 2 + 3);
                        let Ok(art_left) = i16::try_from(art_left) else {
                            return false;
                        };
                        let Some(crease) = art_left.checked_add(3) else {
                            return false;
                        };
                        let commands = [
                            Command {
                                rect,
                                kind: Kind::Stroke as u8,
                                width: 1,
                                ..Command::default()
                            },
                            Command {
                                rect: Rect {
                                    x: art_left,
                                    w: 3,
                                    ..rect
                                },
                                ..Command::default()
                            },
                            Command {
                                rect: Rect {
                                    x: crease,
                                    w: 2,
                                    ..rect
                                },
                                paint: 2,
                                ..Command::default()
                            },
                        ];
                        if !paint_geometry(self.services, &commands) {
                            return false;
                        }
                    }
                }
            }
        }
        if !drawn {
            unsafe { (self.services.placeholder_draw)(rect) };
            if !paint_geometry(
                self.services,
                &[Command {
                    rect,
                    kind: Kind::Stroke as u8,
                    width: 1,
                    ..Command::default()
                }],
            ) {
                return false;
            }
        }
        if let Some(buffer) = &mut self.buffer {
            let needed = unsafe { (self.services.region_size)(rect) };
            if let Ok(needed) = u32::try_from(needed) {
                let span = self.state.reserve(index, rect, needed);
                if span.bytes != 0 {
                    let copied = buffer.read(rect, span.offset as usize, span.bytes as usize);
                    self.state.copied(index, copied);
                }
            }
        }
        true
    }
}

fn paint_geometry(services: &abi::Resources, commands: &[Command]) -> bool {
    let scene = xp_ui::ffi::SceneView {
        commands: commands.as_ptr(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: commands.len() as u16,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    };
    unsafe { (services.paint_scene)(&scene, core::ptr::null()) != 0 }
}

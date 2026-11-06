use super::{
    selection_owned::{terminated_bytes, Footnote},
    selection_scene,
};
use crate::app::{abi, epub::abi::XpEpubLeaves, resources::Buffer};
use core::ffi::c_void;
use xp_core::{glyph::GlyphTarget, i18n::ids::*};
use xp_ui::{
    ffi::Paint,
    model::{Effect, EffectKind, Input, BACK},
    reader_chrome,
    scene::{Arena, Command, Error, Kind, Rect, Refresh},
};

struct Number<'a> {
    services: &'a abi::Resources,
    text: abi::Text,
}
impl Number<'_> {
    unsafe extern "C" fn valid(_: *mut c_void, handle: u32, generation: u32) -> u8 {
        u8::from(handle == 1 && generation == 1)
    }
    unsafe extern "C" fn draw(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &*context.cast::<Self>() };
        unsafe { (this.services.text_draw)(command, clip, this.text) };
    }
}
impl Footnote {
    /// Entry is serialized against render and exit by the caller.
    pub(super) unsafe fn enter(&self, services: &'static abi::Resources) {
        // Optional pixel backing only; selection text always borrows the Page.
        unsafe {
            (*self.frame.get()).snapshot = Buffer::allocate(services, abi::MEMORY_DEFAULT, 4096)
        };
    }

    pub(super) fn input(&self, input: Input) -> super::input::Update {
        self.model.with(|model| {
            let admission = model.gate.input(&input);
            super::input::Update {
                effect: if admission.consume {
                    Effect {
                        kind: if admission.retry && input.released & BACK != 0 {
                            EffectKind::Cancel as u8
                        } else {
                            EffectKind::None as u8
                        },
                        ..Default::default()
                    }
                } else {
                    model.state.update(input, self.count)
                },
                retry: admission.retry,
                haptic: false,
                long_haptic: false,
            }
        })
    }

    /// Caller holds the render lock. All Page and number loans end before return.
    pub(super) unsafe fn render(
        &self,
        services: &abi::Resources,
        leaves: &XpEpubLeaves,
        screen: Rect,
        font: i32,
    ) -> Result<(), Error> {
        unsafe {
            (*self.frame.get()).rendering_selected = self.model.with(|model| model.state.selected)
        };
        let result = unsafe { self.draw(services, leaves, screen, font) };
        self.model.with(|model| {
            model.ready = result.is_ok();
            if result.is_err() {
                model.gate.reject();
            } else {
                model.gate.publish(true);
            }
        });
        result
    }

    unsafe fn hints(&self, services: &abi::Resources, leaves: &XpEpubLeaves) {
        let labels = [
            STR_BACK,
            STR_OPEN,
            STR_DIR_LEFT,
            STR_DIR_RIGHT,
            STR_DIR_UP,
            STR_DIR_DOWN,
        ]
        .map(|id| unsafe { (services.translate)(id) });
        unsafe { (leaves.child_selection_physical.directional_hints)(labels.as_ptr()) };
    }

    unsafe fn draw(
        &self,
        services: &abi::Resources,
        leaves: &XpEpubLeaves,
        screen: Rect,
        font: i32,
    ) -> Result<(), Error> {
        let frame = unsafe { &mut *self.frame.get() };
        if frame.snapshot_index >= 0
            && self.count != 0
            && frame.rendering_selected != frame.snapshot_index
        {
            if let Some(snapshot) = &frame.snapshot {
                snapshot.write(frame.snapshot_rect, 0, 4096);
            }
            // A failed differential snapshot follows native full-page repaint.
            let differential = unsafe { self.highlight(services, leaves, screen, font) };
            if differential.is_err() {
                self.model.with(|model| model.gate.reject());
            }
            if differential.unwrap_or(false) {
                unsafe {
                    self.hints(services, leaves);
                    (services.refresh)(Refresh::Fast as u8)
                };
                return Ok(());
            }
        }
        // End the frame borrow before highlight reuses its commands.
        unsafe {
            (*self.frame.get()).commands[0] = Command {
                kind: Kind::Clear as u8,
                ..Default::default()
            }
        };
        selection_scene::paint(
            services,
            unsafe { &(&(*self.frame.get()).commands)[..1] },
            &selection_scene::empty(),
        )?;
        self.page
            .borrow()
            .paint(leaves, font, self.margins[0], self.margins[1]);
        if self.count != 0 {
            unsafe { self.highlight(services, leaves, screen, font) }?;
        }
        unsafe {
            self.hints(services, leaves);
            (services.refresh)(Refresh::Fast as u8)
        };
        Ok(())
    }

    unsafe fn highlight(
        &self,
        services: &abi::Resources,
        leaves: &XpEpubLeaves,
        screen: Rect,
        font: i32,
    ) -> Result<bool, Error> {
        let frame = unsafe { &mut *self.frame.get() };
        let [link_index, footnote_index] = self.links[frame.rendering_selected as usize];
        let view = self.page.view();
        let link = view.link(link_index).map_err(|_| Error::Invalid)?;
        let (number, _) = view.footnote(footnote_index).map_err(|_| Error::Invalid)?;
        let mut arena = Arena::new(&mut frame.commands, &mut [], &mut [])?;
        let rect = reader_chrome::footnote_view(
            &mut arena,
            screen,
            Rect {
                x: link.x,
                y: link.y,
                w: link.width,
                h: link.height,
            },
            self.margins[0] as i16,
            self.margins[1] as i16,
            font,
            u32::from(number[0] != 0),
        );
        let Ok(rect) = rect else {
            unsafe {
                (leaves.log)(
                    1,
                    abi::Text::from_bytes(b"RUI"),
                    abi::Text::from_bytes(b"Cannot build footnote highlight"),
                )
            };
            frame.snapshot_index = -1;
            return Err(Error::Invalid);
        };
        let scene = arena.finish(Refresh::None, 1)?;
        let saved = rect.w > 0
            && rect.h > 0
            && frame
                .snapshot
                .as_mut()
                .is_some_and(|snapshot| snapshot.read(rect, 0, 4096));
        frame.snapshot_rect = rect;
        frame.snapshot_index = if saved { frame.rendering_selected } else { -1 };
        let mut number = Number {
            services,
            text: abi::Text::from_terminated(&number[..=terminated_bytes(number).len()]),
        };
        let resources = Paint {
            context: (&mut number as *mut Number<'_>).cast(),
            clear: None,
            text: None,
            resource_valid: Some(Number::valid),
            resource: Some(Number::draw),
        };
        if selection_scene::paint(services, scene.commands, &resources).is_err() {
            unsafe {
                (leaves.log)(
                    1,
                    abi::Text::from_bytes(b"RUI"),
                    abi::Text::from_bytes(b"Invalid footnote highlight"),
                )
            };
            frame.snapshot_index = -1;
            return Err(Error::Invalid);
        }
        Ok(saved)
    }
}

impl Footnote {
    #[inline(never)]
    pub(super) unsafe fn activity_input(
        &self,
        parent: &crate::app::epub::EpubSession,
        input: &Input,
    ) -> bool {
        let update = self.input(*input);
        if update.retry || update.effect.invalidate != 0 {
            parent.request_update();
        }
        let result = match update.effect.kind {
            kind if kind == EffectKind::Cancel as u8 => None,
            kind if kind == EffectKind::Activate as u8 => {
                let _access = parent.lock_resources();
                if self.model.with(|model| model.gate.failed()) {
                    parent.request_update();
                    return false;
                }
                if self.count == 0 {
                    return false;
                }
                let Some(href) = self.selection(update.effect.value as usize) else {
                    parent.request_update();
                    return false;
                };
                Some(href)
            }
            _ => return false,
        };
        unsafe {
            *self.result.get() = Some(super::child_result::ReaderChildResult::Footnote(result))
        };
        true
    }
}

impl Footnote {
    // Keep screen-specific sampling and scratch out of the shared child frame.
    #[inline(never)]
    pub(super) unsafe fn activity_render(&self, parent: &crate::app::epub::EpubSession) -> bool {
        let geometry = unsafe { (parent.runtime.display_geometry)() };
        let screen = xp_ui::scene::Rect {
            x: 0,
            y: 0,
            w: geometry.width as i16,
            h: geometry.height as i16,
        };
        unsafe {
            self.render(
                parent.resources,
                parent.leaves,
                screen,
                parent.settings(true).font,
            )
        }
        .is_ok()
    }
}

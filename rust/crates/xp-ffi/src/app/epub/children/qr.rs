use super::{
    owned::Qr,
    physical::{Fonts, Physical},
};
use crate::app::abi;
use core::ffi::c_void;
use xp_core::{
    glyph::GlyphTarget,
    i18n::ids::{STR_BACK, STR_DISPLAY_QR},
};
use xp_ui::{
    ffi::{Paint, SceneView},
    scene::{Arena, Command, Error, Rect, Refresh},
    utility::{self, Texts},
};

struct Drawing<'a> {
    services: &'a abi::Resources,
    physical: &'a Physical,
    payload: abi::Text,
}
impl Drawing<'_> {
    unsafe extern "C" fn valid(_: *mut c_void, handle: u32, generation: u32) -> u8 {
        u8::from(
            generation == 1 && matches!(handle, utility::HEADER | utility::FOOTER | utility::QR),
        )
    }
    unsafe extern "C" fn paint(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &*context.cast::<Self>() };
        let command = unsafe { &*command };
        match command.resource {
            utility::HEADER => unsafe {
                (this.physical.header)(command.rect, (this.services.translate)(STR_DISPLAY_QR), 0)
            },
            utility::FOOTER => {
                let empty = abi::Text::from_terminated(b"\0");
                let labels = [
                    unsafe { (this.services.translate)(STR_BACK) },
                    empty,
                    empty,
                    empty,
                ];
                unsafe { (this.physical.utility_footer)(clip, labels.as_ptr()) };
            }
            utility::QR => unsafe { (this.physical.qr)(command.rect, this.payload) },
            _ => {}
        }
    }
}
// QR has physical resources only; requesting text metrics would be a contract error.
impl Texts for Drawing<'_> {
    fn present(&self, _: u32) -> Result<bool, Error> {
        Err(Error::Invalid)
    }
    fn height(&self, _: u32, _: i32, _: i16, _: u8, _: u8) -> Result<i16, Error> {
        Err(Error::Invalid)
    }
}
impl Qr {
    pub(super) fn input(&self, input: xp_ui::model::Input) -> super::input::Update {
        use xp_ui::model::{Effect, EffectKind, BACK};
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
                    model
                        .state
                        .update(&input, None, &Default::default())
                        .navigation
                },
                retry: admission.retry,
                haptic: false,
                long_haptic: false,
            }
        })
    }

    /// Caller holds the render lifetime lock and parent's resource guard.
    pub(super) unsafe fn render(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        sample: impl FnOnce(&mut utility::Layout),
    ) -> Result<u16, Error> {
        let frame = unsafe { &mut *self.frame.get() };
        frame.layout = utility::Layout::default();
        sample(&mut frame.layout);
        let result = unsafe { self.draw(services, physical) };
        self.model.with(|model| {
            if result.is_err() {
                model.gate.reject();
            } else {
                model.gate.publish(true);
            }
        });
        result
    }

    #[inline(never)]
    unsafe fn draw(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
    ) -> Result<u16, Error> {
        let snapshot = self.model.with(|model| model.state);
        let frame = unsafe { &mut *self.frame.get() };
        if !frame.commands.recover(services) {
            return Err(Error::Capacity);
        }
        let mut drawing = Drawing {
            services,
            physical,
            payload: self.payload.wire(),
        };
        let mut arena = Arena::new(frame.commands.as_mut_slice(), &mut [], &mut [])?;
        let (_, refresh) = utility::view(
            &mut arena,
            &snapshot,
            &frame.layout,
            &mut Fonts(services),
            &drawing,
            Default::default(),
        )?;
        let scene = arena.finish(refresh, 1)?;
        let view = SceneView {
            commands: scene.commands.as_ptr(),
            text: core::ptr::null(),
            interactions: core::ptr::null(),
            generation: 1,
            command_count: scene.commands.len() as u16,
            text_len: 0,
            interaction_count: 0,
            refresh: Refresh::None as u8,
            reserved: 0,
        };
        let paint = Paint {
            context: (&mut drawing as *mut Drawing<'_>).cast(),
            clear: None,
            text: None,
            resource_valid: Some(Drawing::valid),
            resource: Some(Drawing::paint),
        };
        if unsafe { crate::app::scene::paint(services, &view, &paint) } == 0 {
            return Err(Error::Invalid);
        }
        self.model.with(|model| model.state.publish(&snapshot));
        unsafe { (services.refresh)(refresh as u8) };
        Ok(view.command_count)
    }
}

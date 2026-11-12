use super::{
    owned::{Fixed, Percent, PercentLabels},
    physical::Fonts,
};
use crate::app::abi;
use core::{ffi::c_void, fmt::Write};
use xp_core::{glyph::GlyphTarget, i18n::ids::*};
use xp_ui::{
    ffi::{Paint, SceneView},
    reader::percent::Controls,
    scene::{Command, Error, Rect, Refresh, TruncatingArena},
    utility::{self, Texts},
};

impl<const N: usize> Fixed<N> {
    pub(super) fn new() -> Self {
        Self {
            bytes: [0; N],
            len: 0,
        }
    }
    fn append(&mut self, bytes: &[u8]) {
        let count = bytes.len().min(N - 1 - self.len);
        self.bytes[self.len..self.len + count].copy_from_slice(&bytes[..count]);
        self.len += count;
        self.bytes[self.len] = 0;
    }
    pub(super) fn wire(&self) -> abi::Text {
        abi::Text::from_terminated(&self.bytes[..=self.len])
    }
}
impl<const N: usize> core::fmt::Write for Fixed<N> {
    fn write_str(&mut self, text: &str) -> core::fmt::Result {
        self.append(text.as_bytes());
        Ok(())
    }
}

// Same bounded snprintf storage as the dialog, reused in the owner allocation.
impl PercentLabels {
    fn update(&mut self, services: &abi::Resources, value: i32) {
        self.readout.len = 0;
        self.front.len = 0;
        self.side.len = 0;
        let _ = write!(self.readout, "{value}%");
        for (buffer, key, suffix) in [
            (&mut self.front, STR_STEP_HINT_FRONT, &b" 1%"[..]),
            (&mut self.side, STR_STEP_HINT_SIDE, &b" 10%"[..]),
        ] {
            let text = unsafe { (services.translate)(key) };
            buffer.append(unsafe { text.bytes() }.unwrap_or(&[]));
            buffer.append(suffix);
        }
    }
}
struct Labels<'a> {
    services: &'a abi::Resources,
    storage: &'a PercentLabels,
}
impl Labels<'_> {
    fn text(&self, handle: u32) -> Option<abi::Text> {
        Some(match handle {
            utility::TITLE => unsafe { (self.services.translate)(STR_GO_TO_PERCENT) },
            utility::CONFIRM => unsafe { (self.services.translate)(STR_CONFIRM) },
            utility::READOUT => self.storage.readout.wire(),
            utility::MINIMUM => abi::Text::from_terminated(b"0%\0"),
            utility::MAXIMUM => abi::Text::from_terminated(b"100%\0"),
            utility::HINT1 => self.storage.front.wire(),
            utility::HINT2 => self.storage.side.wire(),
            utility::MINUS => abi::Text::from_terminated(b"-\0"),
            utility::PLUS => abi::Text::from_terminated(b"+\0"),
            _ => return None,
        })
    }
    unsafe extern "C" fn valid(context: *mut c_void, handle: u32, generation: u32) -> u8 {
        let this = unsafe { &*context.cast::<Self>() };
        u8::from(generation == 1 && (handle == utility::FOOTER || this.text(handle).is_some()))
    }
    unsafe extern "C" fn paint(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &*context.cast::<Self>() };
        let command = unsafe { &*command };
        if command.resource == utility::FOOTER {
            let labels = [
                unsafe { (this.services.translate)(STR_BACK) },
                unsafe { (this.services.translate)(STR_SELECT) },
                abi::Text::from_terminated(b"-\0"),
                abi::Text::from_terminated(b"+\0"),
            ];
            // Native Percent paints hints after its popup without a rotated clip.
            unsafe { (this.services.footer_draw)(labels.as_ptr()) };
        } else if let Some(text) = this.text(command.resource) {
            unsafe { (this.services.text_draw)(command, clip, text) };
        }
    }
}
impl Texts for Labels<'_> {
    fn present(&self, handle: u32) -> Result<bool, Error> {
        self.text(handle)
            .map(|text| text.len != 0)
            .ok_or(Error::Invalid)
    }
    fn height(
        &self,
        handle: u32,
        font: i32,
        width: i16,
        style: u8,
        lines: u8,
    ) -> Result<i16, Error> {
        let text = self.text(handle).ok_or(Error::Invalid)?;
        let height = unsafe {
            (self.services.text_box_height)(
                text,
                abi::TextLayout {
                    font,
                    width: i32::from(width),
                    style,
                    lines,
                },
            )
        };
        i16::try_from(height)
            .ok()
            .filter(|height| *height >= 0)
            .ok_or(Error::Invalid)
    }
}

impl Percent {
    /// The caller holds the render lifetime lock; callbacks borrow only for this call.
    pub(super) unsafe fn render(
        &self,
        services: &abi::Resources,
        log: unsafe extern "C" fn(u8, abi::Text, abi::Text),
        sample: impl FnOnce(&mut utility::Layout),
    ) -> Result<u16, Error> {
        let frame = unsafe { &mut *self.frame.get() };
        frame.layout = utility::Layout::default();
        sample(&mut frame.layout);
        let result = unsafe { self.paint(services, log) };
        if result.is_err() {
            self.model.with(|model| model.controls = None);
        }
        result
    }

    #[inline(never)]
    unsafe fn paint(
        &self,
        services: &abi::Resources,
        log: unsafe extern "C" fn(u8, abi::Text, abi::Text),
    ) -> Result<u16, Error> {
        let (count, refresh) = unsafe { self.build_scene(services, log) }?;
        let frame = unsafe { &*self.frame.get() };
        let mut labels = Labels {
            services,
            storage: &frame.labels,
        };
        let view = SceneView {
            commands: frame.commands.as_ptr(),
            text: core::ptr::null(),
            interactions: core::ptr::null(),
            generation: 1,
            command_count: count,
            text_len: 0,
            interaction_count: 0,
            refresh: Refresh::None as u8,
            reserved: 0,
        };
        let paint = Paint {
            context: (&mut labels as *mut Labels<'_>).cast(),
            clear: None,
            text: None,
            resource_valid: Some(Labels::valid),
            resource: Some(Labels::paint),
        };
        if unsafe { crate::app::scene::paint(services, &view, &paint) } == 0 {
            return Err(Error::Invalid);
        }
        self.publish(frame.controls);
        unsafe { (services.refresh)(refresh as u8) };
        Ok(count)
    }

    #[inline(never)]
    unsafe fn build_scene(
        &self,
        services: &abi::Resources,
        log: unsafe extern "C" fn(u8, abi::Text, abi::Text),
    ) -> Result<(u16, Refresh), Error> {
        let snapshot = self.begin_render();
        let frame = unsafe { &mut *self.frame.get() };
        frame.labels.update(services, snapshot.value());
        let labels = Labels {
            services,
            storage: &frame.labels,
        };
        let mut state = snapshot.view_state(&frame.controls);
        // Normally contact order is unchanged. Rebuild only if the new generation
        // maps the SDK's active index to a different action/value.
        for _ in 0..2 {
            frame.controls = Controls::default();
            let mut arena =
                TruncatingArena::with_hit_sink(&mut frame.commands, &mut frame.controls)?;
            let (_, refresh) = utility::interval_view(
                &mut arena,
                &state,
                &frame.layout,
                &mut Fonts(services),
                &labels,
                Default::default(),
            )?;
            let scene = arena.finish_reporting_overflow(refresh, 1, |count| unsafe {
                report_overflow(log, count)
            })?;
            let count = scene.commands.len() as u16;
            let next = snapshot.view_state(&frame.controls);
            if (state.active_action, state.active_value) != (next.active_action, next.active_value)
            {
                state = next;
                continue;
            }
            return Ok((count, refresh));
        }
        Err(Error::Invalid)
    }
}

#[inline(never)]
unsafe fn report_overflow(log: unsafe extern "C" fn(u8, abi::Text, abi::Text), count: u16) {
    let mut message = Fixed::<64>::new();
    let _ = write!(message, "Percent scene dropped {count} drawing commands");
    unsafe { log(1, abi::Text::from_terminated(b"RUI\0"), message.wire()) };
}

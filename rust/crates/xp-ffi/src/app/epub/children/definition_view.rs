use super::{definition_owned::DefinitionSession, selection_scene};
use crate::app::{
    abi,
    epub::{
        abi::{XpEpubLeaves, XpEpubSettings},
        render_resources::PrewarmStorage,
    },
};
use core::{cell::Cell, ffi::c_void, mem::MaybeUninit, ptr};
use xp_core::{glyph::GlyphTarget, i18n::ids::*};
use xp_ui::{
    dictionary::{definition, popup, view},
    ffi::Paint,
    model::Metrics,
    scene::{Arena, Command, Error, Rect, Refresh},
};

struct Drawing<'a> {
    owner: &'a DefinitionSession,
    services: &'a abi::Resources,
    leaves: &'a XpEpubLeaves,
    message: abi::Text,
    prewarm: *mut PrewarmStorage,
    started: Cell<bool>,
}
impl Drawing<'_> {
    fn paint(&mut self, commands: &[Command]) -> Result<(), Error> {
        let paint = Paint {
            context: (self as *mut Self).cast(),
            clear: None,
            text: None,
            resource_valid: Some(Self::valid),
            resource: Some(Self::draw),
        };
        let result = selection_scene::paint(self.services, commands, &paint);
        if result.is_err() {
            unsafe {
                (self.leaves.log)(
                    1,
                    abi::Text::from_bytes(b"RUI"),
                    abi::Text::from_bytes(b"Invalid scene in DictionaryDefinition"),
                )
            };
        }
        result
    }
    unsafe extern "C" fn valid(_: *mut c_void, handle: u32, generation: u32) -> u8 {
        u8::from(
            generation == 1
                && matches!(
                    handle,
                    view::FOOTER | view::HEADER | view::BODY | view::MESSAGE
                ),
        )
    }
    unsafe extern "C" fn draw(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &*context.cast::<Self>() };
        let command = unsafe { &*command };
        let state = unsafe { (*this.owner.frame.get()).state };
        match command.resource {
            view::HEADER => unsafe {
                (this.owner.physical.draw_header)(
                    this.owner.payload.get(),
                    command.rect,
                    state.page,
                    state.pages,
                )
            },
            view::BODY => {
                this.started
                    .set(unsafe { this.leaves.font_begin(this.prewarm) });
                let body = || unsafe {
                    (this.owner.physical.draw_body)(
                        this.owner.payload.get(),
                        {
                            let mut settings = MaybeUninit::<XpEpubSettings>::uninit();
                            (this.leaves.settings_snapshot)(1, settings.as_mut_ptr());
                            settings.assume_init().font
                        },
                        i32::from(command.rect.x),
                        i32::from(command.rect.y),
                        state.page,
                    )
                };
                body();
                if this.started.get() {
                    unsafe { this.leaves.font_finish(this.prewarm) };
                }
                body();
            }
            view::FOOTER => {
                let empty = abi::Text::from_bytes(b"");
                let translate = |id| unsafe { (this.services.translate)(id) };
                let labels = [
                    translate(STR_BACK),
                    if state.lookup != 0 {
                        translate(STR_LOOKUP)
                    } else {
                        empty
                    },
                    if state.page > 0 {
                        abi::Text::from_bytes(b"<")
                    } else {
                        empty
                    },
                    if state.page + 1 < state.pages {
                        abi::Text::from_bytes(b">")
                    } else {
                        empty
                    },
                ];
                unsafe { (this.services.footer_draw)(labels.as_ptr()) };
            }
            view::MESSAGE => unsafe { (this.services.text_draw)(command, clip, this.message) },
            _ => {}
        }
    }
}
impl Metrics for Drawing<'_> {
    fn width(&mut self, font: i32, _: &[u8], style: u8) -> i32 {
        unsafe { (self.services.text_width)(self.message, font, style) }
    }
    fn line_height(&mut self, font: i32) -> i32 {
        unsafe { (self.services.line_height)(font) }
    }
}
impl Drop for Drawing<'_> {
    fn drop(&mut self) {
        if self.started.get() {
            unsafe { self.leaves.font_end(self.prewarm) };
        }
    }
}
impl DefinitionSession {
    /// Caller holds RenderLock; prewarm survives both scenes and their refreshes.
    #[inline(never)]
    pub(in crate::app) unsafe fn render(
        &self,
        services: &abi::Resources,
        leaves: &XpEpubLeaves,
    ) -> Result<(), Error> {
        unsafe { self.draw(services, leaves) }
    }
    fn publish_scene(&self) {
        self.model.with(|model| {
            model.ready = true;
            model.gate.publish(true);
        });
    }
    fn reject_scene(&self) {
        self.model.with(|model| {
            model.ready = false;
            model.gate.reject();
            model.discard_pending = true;
            model.state.last_repeat = 0;
        });
    }

    unsafe fn draw(&self, services: &abi::Resources, leaves: &XpEpubLeaves) -> Result<(), Error> {
        let frame = self.frame.get();
        unsafe {
            (*frame).state = self.model.with(|model| model.state);
            (self.physical.layout)(ptr::addr_of_mut!((*frame).layout), -1, -1);
        }
        let message = if unsafe { (*frame).state.message_visible } != 0 {
            unsafe { (services.translate)((*frame).state.message) }
        } else {
            abi::Text::from_bytes(b"")
        };
        let mut drawing = Drawing {
            owner: self,
            services,
            leaves,
            message,
            prewarm: unsafe { ptr::addr_of_mut!((*frame).prewarm).cast() },
            started: Cell::new(false),
        };
        let result = (|| {
            {
                let mut arena = Arena::new(unsafe { &mut (*frame).commands }, &mut [], &mut [])?;
                definition::view(unsafe { &(*frame).layout }, &mut arena)?;
                let scene = arena.finish(Refresh::Fast, 1)?;
                drawing.paint(scene.commands)?;
                if unsafe { (*frame).state.message_visible } == 0 {
                    self.publish_scene();
                    unsafe { (services.refresh)(Refresh::Fast as u8) };
                }
            }
            if unsafe { (*frame).state.message_visible } != 0 {
                let mut layout = popup::Layout::default();
                unsafe { (leaves.child_selection_physical.dictionary_popup_layout)(&mut layout) };
                let mut arena = Arena::new(unsafe { &mut (*frame).commands }, &mut [], &mut [])?;
                popup::view(
                    &layout,
                    unsafe { message.bytes() }.ok_or(Error::Invalid)?,
                    &mut drawing,
                    &mut arena,
                )?;
                let scene = arena.finish(Refresh::Fast, 1)?;
                drawing.paint(scene.commands)?;
                self.publish_scene();
                unsafe { (services.refresh)(Refresh::Fast as u8) };
            }
            Ok(())
        })();
        if result.is_err() {
            self.reject_scene();
        }
        result
    }
}

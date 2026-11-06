use super::{selection_owned::LookupData, selection_scene};
use crate::app::{
    abi,
    epub::{abi::XpEpubLeaves, render_resources::PrewarmStorage},
};
use core::{cell::Cell, ffi::c_void};
use xp_core::{glyph::GlyphTarget, i18n::ids::*};
use xp_ui::{
    dictionary::{popup, view},
    ffi::Paint,
    model::Metrics,
    scene::{Arena, Command, Error, Kind, Rect, Refresh},
};

struct Drawing<'a> {
    owner: &'a LookupData,
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
                    abi::Text::from_bytes(b"Invalid scene in DictionaryWordSelect"),
                )
            };
        }
        result
    }
    unsafe extern "C" fn valid(_: *mut c_void, handle: u32, generation: u32) -> u8 {
        u8::from(generation == 1 && matches!(handle, view::PAGE | view::MESSAGE))
    }
    unsafe extern "C" fn draw(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &*context.cast::<Self>() };
        if unsafe { (*command).resource } == view::MESSAGE {
            unsafe { (this.services.text_draw)(command, clip, this.message) };
            return;
        }
        let started = unsafe { (this.leaves.fonts_prewarm_begin)(this.prewarm) } != 0;
        this.started.set(started);
        let paint = || {
            if let Some(page) = unsafe { &*this.owner.page.get() } {
                page.borrow().paint(
                    this.leaves,
                    this.owner.font,
                    this.owner.margins[0],
                    this.owner.margins[1],
                );
            }
        };
        paint();
        if started {
            unsafe { (this.leaves.fonts_prewarm_finish_scan)(this.prewarm) };
        }
        paint();
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
            unsafe { (self.leaves.fonts_prewarm_drop)(self.prewarm) };
        }
    }
}
impl LookupData {
    /// Render lock excludes Page release and keeps prewarm through the refresh.
    pub(super) unsafe fn render(
        &self,
        services: &abi::Resources,
        leaves: &XpEpubLeaves,
        screen: Rect,
    ) -> Result<(), Error> {
        let result = unsafe { self.draw(services, leaves, screen) };
        self.model.with(|model| {
            model.failed = result.is_err();
            model.ready = result.is_ok();
        });
        result
    }

    unsafe fn draw(
        &self,
        services: &abi::Resources,
        leaves: &XpEpubLeaves,
        screen: Rect,
    ) -> Result<(), Error> {
        let state = self.model.with(|model| model.state);
        let messages = [
            STR_DICT_LOOKING_UP,
            STR_DICT_INDEXING,
            STR_DICT_NOT_FOUND,
            STR_DICT_LOW_MEMORY,
            STR_DICT_READ_FAILED,
            STR_DICT_DECOMPRESS_ERROR,
            STR_DICT_ERROR,
        ];
        let message = unsafe { (services.translate)(messages[usize::from(state.message)]) };
        let frame = self.frame.get();
        let mut drawing = Drawing {
            owner: self,
            services,
            leaves,
            message,
            prewarm: unsafe { core::ptr::addr_of_mut!((*frame).prewarm).cast() },
            started: Cell::new(false),
        };
        unsafe {
            (*frame).commands[0] = Command {
                kind: Kind::Clear as u8,
                ..Default::default()
            };
            (*frame).commands[1] = Command {
                kind: Kind::Page as u8,
                resource: view::PAGE,
                generation: 1,
                rect: screen,
                ..Default::default()
            };
        }
        drawing.paint(unsafe { &(&(*frame).commands)[..2] })?;
        if state.popup != 0 {
            unsafe {
                (leaves.child_selection_physical.dictionary_popup_layout)(core::ptr::addr_of_mut!(
                    (*frame).popup
                ))
            };
            let mut arena = Arena::new(unsafe { &mut (*frame).commands }, &mut [], &mut [])?;
            popup::view(
                unsafe { &(*frame).popup },
                unsafe { message.bytes() }.ok_or(Error::Invalid)?,
                &mut drawing,
                &mut arena,
            )?;
            let scene = arena.finish(Refresh::Fast, 1)?;
            drawing.paint(scene.commands)?;
        }
        unsafe { (services.refresh)(Refresh::Fast as u8) };
        Ok(())
    }
}

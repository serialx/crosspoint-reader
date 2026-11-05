//! Native table loans with a two-family cache. C++ owns and invalidates all tables.
use super::{FontFace, Glyph, MeasureMode};
use crate::{
    bidi,
    font::{Font, GlyphResult, SdView},
    utf8,
};
use core::{cell::UnsafeCell, ffi::c_void, ptr};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Family {
    pub styles: [SdView; 4],
    pub fallback: i32,
    pub has_fallback: u8,
    pub sd: u8,
    pub advance_table: u8,
    pub resolved: [u8; 4],
}
impl Family {
    const EMPTY: Self = Self {
        styles: [SdView {
            data: ptr::null(),
            advances: ptr::null(),
            advance_count: 0,
            full_intervals: ptr::null(),
            bmp_intervals: ptr::null(),
            coverage_count: 0,
        }; 4],
        fallback: 0,
        has_fallback: 0,
        sd: 0,
        advance_table: 0,
        resolved: [0; 4],
    };
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum DiagnosticEvent {
    MissingFont = 1,
    BidiLimit = 2,
}
pub type Diagnostic = unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Provider {
    pub context: *mut c_void,
    pub generation: *const core::sync::atomic::AtomicU32,
    pub family: unsafe extern "C" fn(*mut c_void, i32, *mut Family),
    pub glyph: unsafe extern "C" fn(*mut c_void, i32, u32, u8, *mut Glyph) -> u8,
    pub coverage: unsafe extern "C" fn(*mut c_void, i32, u32, u8) -> u8,
    pub kern: unsafe extern "C" fn(*mut c_void, i32, u32, u32, u8) -> i8,
    pub bidi_acquire: unsafe extern "C" fn() -> *mut bidi::Workspace,
    pub bidi_release: unsafe extern "C" fn(),
    pub prewarm: unsafe extern "C" fn(*mut c_void, i32, *const u8, usize, u8),
    pub diagnostic: Option<Diagnostic>,
}
impl Provider {
    pub fn report(&self, event: DiagnosticEvent, value: u32) {
        if let Some(diagnostic) = self.diagnostic {
            unsafe {
                diagnostic(self.context, event as u32, value, 0, 0, 0);
            }
        }
    }
}
struct Cache {
    families: [core::mem::MaybeUninit<Family>; 2],
    ids: [i32; 2],
    valid: [bool; 2],
    generation: u32,
    next: u8,
}
pub struct Engine {
    provider: Provider,
    cache: UnsafeCell<Cache>,
}
impl Engine {
    /// # Safety
    /// The provider, generation and font owners remain live and serialized. Every
    /// mutation increments generation before a later lookup; callbacks cannot reenter.
    pub unsafe fn new(provider: Provider) -> Self {
        Self {
            provider,
            cache: UnsafeCell::new(Cache {
                families: [core::mem::MaybeUninit::uninit(); 2],
                ids: [0; 2],
                valid: [false; 2],
                generation: 0,
                next: 0,
            }),
        }
    }
    #[inline(always)]
    fn index(&self, id: i32) -> usize {
        unsafe {
            let cache = &mut *self.cache.get();
            let generation =
                (*self.provider.generation).load(core::sync::atomic::Ordering::Relaxed);
            if cache.generation != generation {
                cache.valid = [false; 2];
                cache.generation = generation;
            }
            if let Some(i) = (0..2).find(|&i| cache.valid[i] && cache.ids[i] == id) {
                return i;
            }
            let i = usize::from(cache.next);
            cache.next ^= 1;
            cache.families[i].write(Family::EMPTY);
            (self.provider.family)(self.provider.context, id, cache.families[i].as_mut_ptr());
            cache.ids[i] = id;
            cache.valid[i] = true;
            i
        }
    }
    fn family_value<T>(&self, id: i32, f: impl FnOnce(&Family) -> T) -> T {
        let i = self.index(id);
        unsafe { f((*self.cache.get()).families[i].assume_init_ref()) }
    }
    fn loan<T>(&self, id: i32, style: u8, f: impl FnOnce(Option<&Font<'_>>) -> T) -> T {
        self.family_value(id, |family| {
            let view = &family.styles[usize::from(style & 3)];
            let font = if view.data.is_null() {
                None
            } else {
                Some(unsafe {
                    if family.sd != 0 {
                        Font::from_sd(view)
                    } else {
                        Font::new(&*view.data)
                    }
                })
            };
            f(font.as_ref())
        })
    }
    pub fn exists(&self, id: i32) -> bool {
        self.family_value(id, |f| !f.styles[0].data.is_null())
    }
    pub fn is_sd(&self, id: i32) -> bool {
        self.family_value(id, |f| f.sd != 0)
    }
    pub fn face(&self, id: i32) -> Face<'_> {
        Face { engine: self, id }
    }
    pub fn resolve(&self, id: i32, text: &[u8], style: u8) -> i32 {
        if text.is_empty() || text[0] == 0 {
            return id;
        }
        super::resolve_stream(self, id, Codepoints(text), style)
    }
    fn visual<T>(&self, text: &[u8], direction: i32, f: impl FnOnce(&[u8]) -> T) -> T {
        with_visual(&self.provider, text, direction, f)
    }
    pub fn advance(
        &self,
        id: i32,
        text: &[u8],
        style: u8,
        tracking: i8,
        direction: i32,
        mode: MeasureMode,
    ) -> i32 {
        let id = self.resolve(id, text, style);
        if !self.exists(id) {
            self.provider
                .report(DiagnosticEvent::MissingFont, id as u32);
            return 0;
        }
        self.visual(text, direction, |visual| {
            let direct = self.with_flash(id, style, |face| {
                super::advance_stream(
                    face,
                    Codepoints(visual),
                    style,
                    tracking,
                    mode,
                    bidi::is_transparent_mark,
                )
            });
            direct.unwrap_or_else(|| {
                super::advance_stream(
                    &mut self.face(id),
                    Codepoints(visual),
                    style,
                    tracking,
                    mode,
                    bidi::is_transparent_mark,
                )
            })
        })
    }
    fn with_flash<T>(
        &self,
        id: i32,
        style: u8,
        f: impl FnOnce(&mut FlashFace<'_>) -> T,
    ) -> Option<T> {
        self.family_value(id, |family| {
            let data = family.styles[usize::from(style & 3)].data;
            if family.sd != 0 || data.is_null() {
                return None;
            }
            let data = unsafe { &*data };
            if data.glyph_miss_handler.is_some()
                || data.coverage_handler.is_some()
                || data.kern_handler.is_some()
            {
                return None;
            }
            // No owner callbacks can run during this immutable flash-table walk.
            let font = unsafe { Font::new(data) };
            Some(f(&mut FlashFace(&font)))
        })
    }
    pub fn gap(&self, id: i32, left: u32, right: u32, style: u8) -> i32 {
        self.with_flash(id, style, |face| {
            super::space_advance(face, left, right, style)
        })
        .unwrap_or_else(|| super::space_advance(&mut self.face(id), left, right, style))
    }
    pub fn width(&self, id: i32, text: &[u8], style: u8, direction: i32) -> i32 {
        let resolved = self.resolve(id, text, style);
        if !self.exists(resolved) {
            if text.first().is_some_and(|&b| b != 0) {
                self.provider
                    .report(DiagnosticEvent::MissingFont, resolved as u32);
            }
            return 0;
        }
        self.visual(text, direction, |visual| {
            if resolved != id {
                unsafe {
                    (self.provider.prewarm)(
                        self.provider.context,
                        resolved,
                        visual.as_ptr(),
                        visual.len(),
                        style,
                    );
                }
            }
            super::dimensions_stream(&mut self.face(resolved), Codepoints(visual), style).0
        })
    }
    pub fn word_order(&self, text: &[u8], offsets: &[u16], rtl: bool, out: &mut [u16]) -> bool {
        unsafe {
            let work = (self.provider.bidi_acquire)();
            if work.is_null() {
                return false;
            }
            let result = bidi::visual_word_order(text, offsets, rtl, &mut *work, out)
                == bidi::Status::Applied;
            (self.provider.bidi_release)();
            result
        }
    }
}
#[derive(Clone)]
pub struct Codepoints<'a>(pub &'a [u8]);
impl Iterator for Codepoints<'_> {
    type Item = u32;
    #[inline]
    fn next(&mut self) -> Option<u32> {
        let &lead = self.0.first()?;
        if lead < 0x80 {
            if lead == 0 {
                return None;
            }
            self.0 = &self.0[1..];
            return Some(u32::from(lead));
        }
        let mut n = 0;
        let cp = utf8::next(self.0, &mut n);
        self.0 = &self.0[n..];
        cp
    }
}
pub struct Face<'a> {
    engine: &'a Engine,
    id: i32,
}
fn glyph(g: crate::font::raw::Glyph) -> Glyph {
    Glyph {
        advance: g.advance_x,
        width: g.width,
        height: g.height,
        left: g.left,
        top: g.top,
    }
}
impl FontFace for Face<'_> {
    fn glyph(&mut self, cp: u32, style: u8) -> Option<Glyph> {
        let result = self.engine.loan(self.id, style, |f| {
            f.map_or(GlyphResult::Missing, |f| f.glyph(cp))
        });
        match result {
            GlyphResult::Resident(g) | GlyphResult::Synthetic(g) => Some(glyph(g)),
            GlyphResult::Missing => None,
            GlyphResult::NotResident => {
                let mut g = Glyph::default();
                let p = &self.engine.provider;
                (unsafe { (p.glyph)(p.context, self.id, cp, style, &mut g) } != 0).then_some(g)
            }
        }
    }
    fn has_codepoint(&self, cp: u32, style: u8) -> bool {
        let result = self.engine.loan(self.id, style, |f| {
            f.map_or(Some(false), |f| f.has_codepoint(cp))
        });
        result.unwrap_or_else(|| {
            let p = &self.engine.provider;
            unsafe { (p.coverage)(p.context, self.id, cp, style) != 0 }
        })
    }
    fn kerning(&mut self, left: u32, right: u32, style: u8) -> i8 {
        let result = self.engine.loan(self.id, style, |f| {
            f.map_or(Some(0), |f| f.kerning(left, right))
        });
        result.unwrap_or_else(|| {
            let p = &self.engine.provider;
            unsafe { (p.kern)(p.context, self.id, left, right, style) }
        })
    }
    fn ligature(&self, left: u32, right: u32, style: u8) -> u32 {
        self.engine
            .loan(self.id, style, |f| f.map_or(0, |f| f.ligature(left, right)))
    }
    fn has_advance_table(&self) -> bool {
        self.engine.family_value(self.id, |f| f.advance_table != 0)
    }
    fn resolve_style(&self, style: u8) -> u8 {
        self.engine
            .family_value(self.id, |f| f.resolved[usize::from(style & 3)])
    }
    fn advance(&self, cp: u32, style: u8) -> u16 {
        self.engine.loan(self.id, style, |f| {
            f.and_then(|f| f.advance(cp)).unwrap_or(0)
        })
    }
    fn ascender(&self, style: u8) -> i32 {
        self.engine
            .loan(self.id, style, |f| f.map_or(0, |f| f.data().ascender))
    }
    fn line_height(&self) -> u8 {
        self.engine
            .loan(self.id, 0, |f| f.map_or(0, |f| f.data().advance_y))
    }
}

impl super::FontRegistry for Engine {
    fn fallback_id(&self, id: i32) -> Option<i32> {
        self.family_value(id, |f| (f.has_fallback != 0).then_some(f.fallback))
    }
    fn contains(&self, id: i32) -> bool {
        self.exists(id)
    }
    fn has_codepoint(&self, id: i32, cp: u32, style: u8) -> bool {
        self.face(id).has_codepoint(cp, style)
    }
}

// Used only inside a synchronous walk after excluding all mutating handlers.
struct FlashFace<'a>(&'a Font<'a>);
impl FontFace for FlashFace<'_> {
    fn glyph(&mut self, cp: u32, _: u8) -> Option<Glyph> {
        match self.0.glyph(cp) {
            GlyphResult::Resident(g) | GlyphResult::Synthetic(g) => Some(glyph(g)),
            _ => None,
        }
    }
    fn has_codepoint(&self, cp: u32, _: u8) -> bool {
        self.0.has_codepoint(cp).unwrap_or(false)
    }
    fn kerning(&mut self, left: u32, right: u32, _: u8) -> i8 {
        self.0.kerning(left, right).unwrap_or(0)
    }
    fn ligature(&self, left: u32, right: u32, _: u8) -> u32 {
        self.0.ligature(left, right)
    }
    fn has_advance_table(&self) -> bool {
        false
    }
    fn resolve_style(&self, style: u8) -> u8 {
        style & 3
    }
    fn advance(&self, _: u32, _: u8) -> u16 {
        0
    }
    fn ascender(&self, _: u8) -> i32 {
        self.0.data().ascender
    }
    fn line_height(&self) -> u8 {
        self.0.data().advance_y
    }
}

pub fn with_visual<T>(
    provider: &Provider,
    text: &[u8],
    direction: i32,
    f: impl FnOnce(&[u8]) -> T,
) -> T {
    if direction != 1
        && !text
            .iter()
            .take_while(|&&b| b != 0)
            .any(|&b| (0xd6..=0xdb).contains(&b))
    {
        return f(text);
    }
    unsafe {
        let work = (provider.bidi_acquire)();
        if work.is_null() {
            return f(text);
        }
        let visual = match bidi::visual_diagnostic(text, direction, &mut *work) {
            Ok(visual) if !visual.is_empty() => visual,
            Err(bidi::VisualError::TooLong) => {
                provider.report(DiagnosticEvent::BidiLimit, bidi::MAX_LINE as u32);
                text
            }
            _ => text,
        };
        let result = f(visual);
        (provider.bidi_release)();
        result
    }
}

/// A selected EpdFont's stable data slot, reread after every owner callback.
/// No table reference survives a bitmap emission, glyph miss, or kerning call.
pub struct DrawingFace<'a> {
    slot: *const *const crate::font::raw::FontData,
    provider: &'a Provider,
    id: i32,
    glyph_cp: u32,
    glyph_ptr: *const crate::font::raw::Glyph,
}
impl<'a> DrawingFace<'a> {
    /// # Safety
    /// The slot and provider stay live and serialized for the draw. Callbacks
    /// may replace its tables, but cannot unregister or replace the EpdFont owner.
    pub unsafe fn new(
        slot: *const *const crate::font::raw::FontData,
        provider: &'a Provider,
        id: i32,
    ) -> Self {
        Self {
            slot,
            provider,
            id,
            glyph_cp: 0,
            glyph_ptr: core::ptr::null(),
        }
    }
    #[inline]
    fn loan<T>(&self, query: impl FnOnce(&Font<'_>) -> T) -> T {
        query(&unsafe { Font::new(&**self.slot) })
    }
}
impl FontFace for DrawingFace<'_> {
    fn glyph_pointer(&self, cp: u32) -> *const crate::font::raw::Glyph {
        if cp == self.glyph_cp {
            self.glyph_ptr
        } else {
            core::ptr::null()
        }
    }
    #[inline(always)]
    fn glyph(&mut self, cp: u32, style: u8) -> Option<Glyph> {
        let (result, ptr) = self.loan(|font| font.glyph_with_pointer(cp));
        self.glyph_cp = cp;
        self.glyph_ptr = ptr;
        match result {
            GlyphResult::Resident(g) | GlyphResult::Synthetic(g) => Some(glyph(g)),
            GlyphResult::Missing => None,
            GlyphResult::NotResident => {
                let mut g = Glyph::default();
                let p = self.provider;
                (unsafe { (p.glyph)(p.context, self.id, cp, style, &mut g) } != 0).then_some(g)
            }
        }
    }
    fn has_codepoint(&self, cp: u32, style: u8) -> bool {
        self.loan(|font| font.has_codepoint(cp)).unwrap_or_else(|| {
            let p = self.provider;
            unsafe { (p.coverage)(p.context, self.id, cp, style) != 0 }
        })
    }
    #[inline(always)]
    fn kerning(&mut self, left: u32, right: u32, style: u8) -> i8 {
        self.loan(|font| font.kerning(left, right))
            .unwrap_or_else(|| {
                let p = self.provider;
                unsafe { (p.kern)(p.context, self.id, left, right, style) }
            })
    }
    #[inline(always)]
    fn ligature(&self, left: u32, right: u32, _: u8) -> u32 {
        self.loan(|font| font.ligature(left, right))
    }
    fn has_advance_table(&self) -> bool {
        false
    }
    fn resolve_style(&self, style: u8) -> u8 {
        style & 3
    }
    fn advance(&self, _: u32, _: u8) -> u16 {
        0
    }
    fn ascender(&self, _: u8) -> i32 {
        self.loan(|font| font.data().ascender)
    }
    fn line_height(&self) -> u8 {
        self.loan(|font| font.data().advance_y)
    }
}

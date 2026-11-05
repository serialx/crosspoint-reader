//! Page ABI. Page arenas are caller-owned and byte-aligned; query spans borrow
//! them. Builders borrow a separate output buffer until finish or reset.
use core::{mem, ptr, slice};
use xp_core::{
    page::{
        self,
        builder::{Builder, Content, PackedLine, Rubies},
        Error,
    },
    para::{paragraph::Span as ParaSpan, paragraph_extract::Output},
    section::{Diagnostic, Kind, Record},
};

#[unsafe(no_mangle)]
pub extern "C" fn xp_page_line_capacity(
    lines: usize,
    words: usize,
    text_bytes: usize,
    focus: u8,
    fonts: u8,
    ruby_bytes: usize,
) -> usize {
    page::builder::line_capacity(lines, words, text_bytes, focus != 0, fonts != 0, ruby_bytes)
}

#[unsafe(no_mangle)]
pub extern "C" fn xp_page_plain_line_arena_capacity(encoded_bytes: usize, lines: usize) -> usize {
    page::plain_line_arena_capacity(encoded_bytes, lines)
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Span {
    data: *const u8,
    len: usize,
}
impl From<&[u8]> for Span {
    fn from(s: &[u8]) -> Self {
        Self {
            data: s.as_ptr(),
            len: s.len(),
        }
    }
}
#[repr(C)]
pub struct Element {
    tag: u32,
    record: Record,
    first: Span,
    second: Span,
    width: i16,
    height: i16,
}
#[repr(C)]
pub struct Line {
    block: Record,
    fonts: [i32; 3],
    count: u16,
    paragraph_start: u16,
    has_fonts: u8,
    has_ruby: u8,
    has_focus: u8,
    packed: Span,
    font_data: Span,
    ruby_offsets: Span,
    style_data: Span,
}
#[repr(C)]
pub struct Word {
    text: Span,
    ruby: Span,
    source_start: u32,
    source_end: u32,
    font: i32,
    x: i16,
    focus_suffix: u16,
    style: u8,
    focus_boundary: u8,
    starts_paragraph: u8,
    discretionary_hyphen: u8,
    small_caps: u8,
}
#[repr(C)]
pub struct Link {
    href: Span,
    x: i16,
    y: i16,
    width: i16,
    height: i16,
}
#[repr(C)]
pub struct BuilderStorage {
    words: [usize; 8],
}
const _: () = assert!(mem::size_of::<Builder<'static>>() <= mem::size_of::<BuilderStorage>());
const _: () = assert!(mem::align_of::<Builder<'static>>() <= mem::align_of::<BuilderStorage>());

fn aligned<T>(p: *const T) -> bool {
    !p.is_null() && (p as usize).is_multiple_of(mem::align_of::<T>())
}
unsafe fn input<'a, T>(p: *const T, n: usize) -> page::Result<&'a [T]> {
    if n > isize::MAX as usize / mem::size_of::<T>() || (n != 0 && !aligned(p)) {
        return Err(Error::Invalid);
    }
    Ok(if n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, n)
    })
}
unsafe fn output<'a>(p: *mut u8, n: usize) -> page::Result<&'a mut [u8]> {
    if n > isize::MAX as usize || (n != 0 && p.is_null()) {
        return Err(Error::Invalid);
    }
    Ok(if n == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(p, n)
    })
}
fn status(result: page::Result<()>) -> u32 {
    match result {
        Ok(()) => 0,
        Err(Error::NeedInput) => 1,
        Err(Error::NeedOutput(_)) => 2,
        Err(Error::Invalid) => 3,
    }
}
unsafe fn page<'a>(p: *const u8, n: usize) -> page::Result<page::Page<'a>> {
    page::Page::open(input(p, n)?)
}
unsafe fn store<T>(p: *mut T, value: T) -> page::Result<()> {
    if !aligned(p) {
        return Err(Error::Invalid);
    }
    ptr::write(p, value);
    Ok(())
}

/// # Safety
/// Input borrows len bytes; info is aligned, writable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_page_arena_size(
    data: *const u8,
    len: usize,
    info: *mut page::Info,
) -> u32 {
    xp_page_arena_size_diagnostic(data, len, info, ptr::null_mut())
}
/// # Safety
/// Same buffer and callback contract as xp_page_arena_size; diagnostic is null or
/// aligned writable storage, disjoint from all other input and output buffers.
#[no_mangle]
pub unsafe extern "C" fn xp_page_arena_size_diagnostic(
    data: *const u8,
    len: usize,
    info: *mut page::Info,
    diagnostic: *mut Diagnostic,
) -> u32 {
    let mut detail = Diagnostic::default();
    let result = (|| {
        store(
            info,
            page::inspect_detailed(input(data, len)?, &mut detail)?,
        )
    })();
    if !diagnostic.is_null() {
        if let Err(error) = store(diagnostic, detail) {
            return status(Err(error));
        }
    }
    status(result)
}
/// # Safety
/// The callback writes every requested byte or returns zero; info is writable.
#[no_mangle]
pub unsafe extern "C" fn xp_page_probe(
    context: *mut core::ffi::c_void,
    read: unsafe extern "C" fn(*mut core::ffi::c_void, usize, *mut u8, usize) -> u8,
    len: usize,
    info: *mut page::Info,
) -> u32 {
    xp_page_probe_diagnostic(context, read, len, info, ptr::null_mut())
}
/// # Safety
/// Same buffer and callback contract as xp_page_probe; diagnostic is null or
/// aligned writable storage, disjoint from all other input and output buffers.
#[no_mangle]
pub unsafe extern "C" fn xp_page_probe_diagnostic(
    context: *mut core::ffi::c_void,
    read: unsafe extern "C" fn(*mut core::ffi::c_void, usize, *mut u8, usize) -> u8,
    len: usize,
    info: *mut page::Info,
    diagnostic: *mut Diagnostic,
) -> u32 {
    let mut detail = Diagnostic::default();
    let result = (|| {
        if !aligned(info) {
            return Err(Error::Invalid);
        }
        store(
            info,
            page::probe::probe_detailed(
                len,
                |at, bytes| read(context, at, bytes.as_mut_ptr(), bytes.len()) != 0,
                &mut detail,
            )?,
        )
    })();
    if !diagnostic.is_null() {
        if let Err(error) = store(diagnostic, detail) {
            return status(Err(error));
        }
    }
    status(result)
}
/// # Safety
/// Arena is writable and contains the input at offset/length; info is disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_page_decode_in_place(
    arena: *mut u8,
    capacity: usize,
    offset: usize,
    len: usize,
    visible: u32,
    info: *mut page::Info,
) -> u32 {
    xp_page_decode_in_place_diagnostic(arena, capacity, offset, len, visible, info, ptr::null_mut())
}
/// # Safety
/// Same buffer and callback contract as xp_page_decode_in_place; diagnostic is null or
/// aligned writable storage, disjoint from all other input and output buffers.
#[no_mangle]
pub unsafe extern "C" fn xp_page_decode_in_place_diagnostic(
    arena: *mut u8,
    capacity: usize,
    offset: usize,
    len: usize,
    visible: u32,
    info: *mut page::Info,
    diagnostic: *mut Diagnostic,
) -> u32 {
    let mut detail = Diagnostic::default();
    let result = (|| {
        if !aligned(info) {
            return Err(Error::Invalid);
        }
        store(
            info,
            page::decode_in_place_detailed(
                output(arena, capacity)?,
                offset,
                len,
                visible,
                &mut detail,
            )?,
        )
    })();
    if !diagnostic.is_null() {
        if let Err(error) = store(diagnostic, detail) {
            return status(Err(error));
        }
    }
    status(result)
}
/// # Safety
/// Input, writable arena and info are live, disjoint allocations; null buffers
/// are allowed only at zero length. info is aligned and writable.
#[no_mangle]
pub unsafe extern "C" fn xp_page_decode(
    data: *const u8,
    len: usize,
    visible: u32,
    arena: *mut u8,
    capacity: usize,
    info: *mut page::Info,
) -> u32 {
    xp_page_decode_diagnostic(data, len, visible, arena, capacity, info, ptr::null_mut())
}
/// # Safety
/// Same buffer and callback contract as xp_page_decode; diagnostic is null or
/// aligned writable storage, disjoint from all other input and output buffers.
#[no_mangle]
pub unsafe extern "C" fn xp_page_decode_diagnostic(
    data: *const u8,
    len: usize,
    visible: u32,
    arena: *mut u8,
    capacity: usize,
    info: *mut page::Info,
    diagnostic: *mut Diagnostic,
) -> u32 {
    let mut detail = Diagnostic::default();
    let result = (|| {
        if !aligned(info) {
            return Err(Error::Invalid);
        }
        let data = input(data, len)?;
        match page::decode_detailed(data, visible, output(arena, capacity)?, &mut detail) {
            Ok(value) => store(info, value),
            Err(error @ Error::NeedOutput(_)) => {
                // Preserve the capacity-query contract without scanning twice on success.
                store(info, page::inspect(data)?)?;
                Err(error)
            }
            Err(error) => Err(error),
        }
    })();
    if !diagnostic.is_null() {
        if let Err(error) = store(diagnostic, detail) {
            return status(Err(error));
        }
    }
    status(result)
}
/// # Safety
/// Arena is a successful decode, unchanged; output and required are writable,
/// disjoint from it and each other. All scalar outputs are aligned.
#[no_mangle]
pub unsafe extern "C" fn xp_page_encode(
    arena: *const u8,
    len: usize,
    out: *mut u8,
    capacity: usize,
    required: *mut usize,
) -> u32 {
    status((|| {
        let page = page(arena, len)?;
        store(required, page.encoded_size()?)?;
        page.encode(output(out, capacity)?)?;
        Ok(())
    })())
}
/// # Safety
/// Arena is immutable; all output objects are aligned, writable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_page_info(
    arena: *const u8,
    len: usize,
    info: *mut page::Info,
    visible: *mut u32,
) -> u32 {
    status((|| {
        let p = page(arena, len)?;
        store(info, p.info())?;
        store(visible, p.visible_offset())
    })())
}
#[repr(C)]
#[derive(Default)]
pub struct Position {
    x: i16,
    y: i16,
    tag: u16,
}
/// # Safety
/// Arena is a successful immutable decode. Invalid input returns tag zero.
#[no_mangle]
pub unsafe extern "C" fn xp_page_position(arena: *const u8, len: usize, index: u16) -> Position {
    page(arena, len)
        .and_then(|p| p.position(index))
        .map(|(x, y, tag)| Position { x, y, tag })
        .unwrap_or_default()
}
/// # Safety
/// Arena is immutable; out is aligned, writable and disjoint. Returned spans
/// borrow the arena until it is mutated or released.
#[no_mangle]
pub unsafe extern "C" fn xp_page_element(
    arena: *const u8,
    len: usize,
    index: u16,
    out: *mut Element,
) -> u32 {
    status((|| {
        let e = page(arena, len)?.element(index)?;
        let mut value = Element {
            tag: u32::from(e.tag()?),
            record: e.record()?,
            first: (&[][..]).into(),
            second: (&[][..]).into(),
            width: 0,
            height: 0,
        };
        match value.tag {
            2 => {
                let (a, b, w, h) = e.image()?;
                value.first = a.into();
                value.second = b.into();
                value.width = w;
                value.height = h;
            }
            4 => {
                value.first = e.drop_cap_text()?.into();
            }
            _ => {}
        }
        store(out, value)
    })())
}
/// # Safety
/// Same arena/output contract as xp_page_element.
#[no_mangle]
pub unsafe extern "C" fn xp_page_line(
    arena: *const u8,
    len: usize,
    index: u16,
    out: *mut Line,
) -> u32 {
    status((|| {
        let l = page(arena, len)?.element(index)?.line()?;
        let fonts = l.fonts()?;
        store(
            out,
            Line {
                block: l.block_style()?,
                fonts: fonts.unwrap_or([0; 3]),
                count: l.count,
                paragraph_start: l.paragraph_start,
                has_fonts: fonts.is_some() as u8,
                has_ruby: l.has_ruby() as u8,
                has_focus: l.focus as u8,
                packed: l.packed_data()?.into(),
                font_data: l.font_data()?.into(),
                ruby_offsets: l.ruby_offsets()?.into(),
                style_data: l.style_data()?.into(),
            },
        )
    })())
}
/// # Safety
/// Same arena/output contract as xp_page_element. Word text has a trailing NUL
/// at data[len]. Ruby on has_ruby lines also has a trailing NUL; otherwise it is empty.
#[no_mangle]
pub unsafe extern "C" fn xp_page_word(
    arena: *const u8,
    len: usize,
    element: u16,
    index: u16,
    font: i32,
    out: *mut Word,
) -> u32 {
    status((|| {
        let l = page(arena, len)?.element(element)?.line()?;
        let w = l.word(index, font)?;
        store(
            out,
            Word {
                text: w.text.into(),
                ruby: l.ruby(index)?.into(),
                source_start: w.source_start,
                source_end: w.source_end,
                font: w.font,
                x: w.x,
                focus_suffix: w.focus_suffix,
                style: w.style,
                focus_boundary: w.focus_boundary,
                starts_paragraph: w.starts_paragraph as u8,
                discretionary_hyphen: w.discretionary_hyphen as u8,
                small_caps: w.small_caps as u8,
            },
        )
    })())
}
/// # Safety
/// Same arena/output contract as xp_page_element; both spans are disjoint outputs.
#[no_mangle]
pub unsafe extern "C" fn xp_page_footnote(
    arena: *const u8,
    len: usize,
    index: u16,
    number: *mut Span,
    href: *mut Span,
) -> u32 {
    status((|| {
        let (n, h) = page(arena, len)?.footnote(index)?;
        store(number, n.into())?;
        store(href, h.into())
    })())
}
/// # Safety
/// Same arena/output contract as xp_page_element.
#[no_mangle]
pub unsafe extern "C" fn xp_page_link(
    arena: *const u8,
    len: usize,
    index: u16,
    out: *mut Link,
) -> u32 {
    status((|| {
        let l = page(arena, len)?.link(index)?;
        store(
            out,
            Link {
                href: l.href.into(),
                x: l.x,
                y: l.y,
                width: l.width,
                height: l.height,
            },
        )
    })())
}

unsafe fn builder<'a>(p: *mut BuilderStorage) -> page::Result<&'a mut Builder<'a>> {
    if !aligned(p) {
        return Err(Error::Invalid);
    }
    Ok(&mut *p.cast())
}
/// # Safety
/// State is aligned writable storage. Buffer is exclusively borrowed until
/// finish/reset; neither state nor buffer overlaps any later append input.
#[no_mangle]
pub unsafe extern "C" fn xp_page_builder_init(
    state: *mut BuilderStorage,
    buffer: *mut u8,
    capacity: usize,
) -> u32 {
    status((|| {
        if !aligned(state) {
            return Err(Error::Invalid);
        }
        ptr::write(
            state.cast::<Builder<'_>>(),
            Builder::new(output(buffer, capacity)?)?,
        );
        Ok(())
    })())
}
struct RubySpans<'a>(&'a [ParaSpan]);
impl Rubies for RubySpans<'_> {
    fn len(&self) -> usize {
        self.0.len()
    }
    fn get(&self, i: usize) -> &[u8] {
        unsafe {
            if self.0[i].len == 0 {
                &[]
            } else {
                slice::from_raw_parts(self.0[i].data, self.0[i].len as usize)
            }
        }
    }
}
/// # Safety
/// Initialized exclusive state. line and its complete XpParaLine graph are
/// readable for the call, disjoint from state/output, with aligned arrays.
/// Layout-only links remain borrowed from line->links; the assembly owner must
/// turn them into page links before releasing the packed line.
#[no_mangle]
pub unsafe extern "C" fn xp_page_builder_line(
    state: *mut BuilderStorage,
    x: i16,
    y: i16,
    line: *const Output,
) -> u32 {
    status((|| {
        if !aligned(line) {
            return Err(Error::Invalid);
        }
        let l = &*line;
        let n = l.count as usize;
        if n > 10000 || !aligned(l.block) {
            return Err(Error::Invalid);
        }
        let b = &*l.block;
        let mut block = Record {
            kind: Kind::BlockStyle as u32,
            ..Record::default()
        };
        block.values[..16].copy_from_slice(&[
            u32::from(b.alignment),
            u32::from(b.text_align_defined),
            b.margin_top as u32,
            b.margin_bottom as u32,
            b.margin_left as u32,
            b.margin_right as u32,
            b.padding_top as u32,
            b.padding_bottom as u32,
            b.padding_left as u32,
            b.padding_right as u32,
            b.text_indent as u32,
            u32::from(b.text_indent_defined),
            u32::from(b.rtl),
            u32::from(b.direction_defined),
            b.character_spacing as u32,
            b.font_id as u32,
        ]);
        let ruby = RubySpans(input(l.ruby, if l.ruby.is_null() { 0 } else { n })?);
        for span in ruby.0 {
            input(span.data, span.len as usize)?;
        }
        let fonts = if l.font_slots.is_null() {
            None
        } else {
            let f = input(l.fonts, 3)?;
            Some(([f[0], f[1], f[2]], input(l.font_slots, n)?))
        };
        let packed = PackedLine {
            text: input(l.text.data, l.text.len as usize)?,
            offsets: input(l.offsets, n)?,
            positions: input(l.positions, n)?,
            styles: input(l.styles, n)?,
            boundaries: input(l.boundaries, if l.boundaries.is_null() { 0 } else { n })?,
            suffixes: input(l.suffixes, if l.boundaries.is_null() { 0 } else { n })?,
            sources: input(l.sources, n * 2)?,
            ruby: &ruby,
            paragraph_start: l.paragraph_start,
            block,
            fonts,
        };
        builder(state)?.append(x, y, Content::Line(&packed))
    })())
}
/// # Safety
/// Initialized exclusive state. record and spans are readable, disjoint inputs.
/// record.kind is IMAGE_SIZE, RULE, DROP_CAP or BORDER; positions are x/y.
#[no_mangle]
pub unsafe extern "C" fn xp_page_builder_element(
    state: *mut BuilderStorage,
    x: i16,
    y: i16,
    record: *const Record,
    first: Span,
    second: Span,
) -> u32 {
    status((|| {
        if !aligned(record) {
            return Err(Error::Invalid);
        }
        let r = &*record;
        let v = &r.values;
        let first = input(first.data, first.len)?;
        let second = input(second.data, second.len)?;
        let content = match Kind::from_raw(r.kind) {
            Some(Kind::ImageSize) => Content::Image {
                path: first,
                source: second,
                width: v[0] as i16,
                height: v[1] as i16,
            },
            Some(Kind::Rule) => Content::Rule {
                width: v[2] as u16,
                thickness: v[3] as u8,
            },
            Some(Kind::DropCap) => Content::DropCap {
                font: v[2] as i32,
                scale: v[3] as u16,
                style: v[4] as u8,
                text: first,
            },
            Some(Kind::Border) => Content::Border {
                width: v[2] as u16,
                height: v[3] as u16,
                sides: [
                    v[4] as u8,
                    v[5] as u8,
                    v[6] as u8,
                    v[7] as u8,
                    v[8] as u8,
                    v[9] as u8,
                    v[10] as u8,
                    v[11] as u8,
                ],
                shaded: v[12] != 0,
            },
            _ => return Err(Error::Invalid),
        };
        builder(state)?.append(x, y, content)
    })())
}
/// # Safety
/// Initialized exclusive state. number/href point to 32/256 readable bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_page_builder_footnote(
    state: *mut BuilderStorage,
    number: *const u8,
    href: *const u8,
) -> u32 {
    status((|| {
        builder(state)?.footnote(
            input(number, 32)?.try_into().map_err(|_| Error::Invalid)?,
            input(href, 256)?.try_into().map_err(|_| Error::Invalid)?,
        )
    })())
}
/// # Safety
/// Initialized exclusive state. link and its href span are readable/disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_page_builder_link(
    state: *mut BuilderStorage,
    link: *const Link,
) -> u32 {
    status((|| {
        if !aligned(link) {
            return Err(Error::Invalid);
        }
        let l = &*link;
        builder(state)?.link(page::Link {
            href: input(l.href.data, l.href.len)?,
            x: l.x,
            y: l.y,
            width: l.width,
            height: l.height,
        })
    })())
}
/// # Safety
/// Initialized exclusive state; written is aligned, writable and disjoint.
/// On success buffer[..written] is the encoded page. Reset before more appends.
#[no_mangle]
pub unsafe extern "C" fn xp_page_builder_finish(
    state: *mut BuilderStorage,
    written: *mut usize,
) -> u32 {
    status((|| {
        if !aligned(written) {
            return Err(Error::Invalid);
        }
        store(written, builder(state)?.finish()?.len())
    })())
}

/// # Safety
/// Same arena contract as xp_page_element; bounds points to four writable i16
/// values, found to one writable byte. All buffers are disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_page_image_bounds(
    arena: *const u8,
    len: usize,
    bounds: *mut i16,
    found: *mut u8,
) -> u32 {
    status((|| {
        if !aligned(bounds) || found.is_null() {
            return Err(Error::Invalid);
        }
        if let Some(value) = page(arena, len)?.image_bounds()? {
            ptr::copy_nonoverlapping(value.as_ptr(), bounds, 4);
            store(found, 1)
        } else {
            store(found, 0)
        }
    })())
}

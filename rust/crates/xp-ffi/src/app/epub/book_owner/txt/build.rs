use super::super::{writer::Writer, BookCache, Metadata};
use super::*;
use xp_core::epub::{diagnostic::Diagnostics, opf};

pub(in super::super) fn build(
    path: &Text,
    cache_path: &Text,
    cache: &mut BookCache,
    metadata: &mut Metadata,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    diagnostics: &Diagnostics,
) -> bool {
    struct Context<'a> {
        path: &'a Text,
        cache_path: &'a Text,
        cache: &'a mut BookCache,
        metadata: &'a mut Metadata,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
        diagnostics: &'a Diagnostics,
        writer: Option<Writer<'a>>,
        title: Text,
        work: Option<Metadata>,
        companion: Text,
        failed: bool,
    }
    unsafe extern "C" fn control(raw: *mut c_void, operation: u32) -> bool {
        use txt::CacheAction as A;
        let ctx = unsafe { &mut *raw.cast::<Context<'_>>() };
        match operation {
            x if x == A::Exists as u32 => unsafe {
                (ctx.resources.path_exists)(ctx.cache_path.wire()) != 0
            },
            x if x == A::Mkdir as u32 => {
                unsafe { (ctx.leaves.path_mkdir)(ctx.cache_path.wire()) };
                true
            }
            x if x == A::Invalidate as u32 => {
                ctx.failed |= invalidate(ctx.cache_path, ctx.leaves).is_err();
                true
            }
            x if x == A::Begin as u32 => {
                if ctx.failed {
                    return false;
                }
                ctx.cache.spine_count = 0;
                ctx.cache.toc_count = 0;
                ctx.writer = Some(Writer::new(
                    ctx.path,
                    ctx.cache_path,
                    ctx.resources,
                    ctx.leaves,
                ));
                true
            }
            x if x == A::BeginOpf as u32 => {
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                writer.begin_opf().is_ok()
            }
            x if x == A::Spine as u32 => {
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                // The native void createSpineEntry logs failure and continues this transaction.
                if writer
                    .cache
                    .create_spine(&writer.streams.io(), b"content.html", ctx.diagnostics)
                    .is_err()
                {
                    log::message(
                        ctx.leaves,
                        1,
                        b"BMC",
                        format_args!("Could not write spine entry"),
                    );
                }
                true
            }
            x if x == A::EndOpf as u32 => {
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                writer.end_opf().is_ok()
            }
            x if x == A::BeginToc as u32 => {
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                writer.begin_toc(ctx.diagnostics).is_ok()
            }
            x if x == A::Toc as u32 => {
                if ctx
                    .title
                    .assign(residual::txt_title(ctx.path.bytes()))
                    .is_err()
                {
                    ctx.failed = true;
                    return false;
                }
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                // Native buildTxtCache ignores the createTocEntry result.
                let _ = writer.cache.create_toc(
                    &writer.streams.io(),
                    ctx.title.bytes(),
                    b"content.html",
                    b"",
                    0,
                    ctx.diagnostics,
                );
                true
            }
            x if x == A::EndToc as u32 => {
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                writer.end_toc().is_ok()
            }
            x if x == A::End as u32 => {
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                writer.end()
            }
            x if x == A::Book as u32 => {
                if ctx.failed {
                    return false;
                }
                ctx.work = Some(Metadata::default());
                let work = ctx.work.as_mut().unwrap();
                if work.assign(opf::TITLE, ctx.title.bytes()).is_err()
                    || work.assign(opf::LANGUAGE, b"en").is_err()
                {
                    return false;
                }
                let Ok(cover) = companion(ctx.path, ctx.resources) else {
                    return false;
                };
                ctx.companion = cover;
                if !ctx.companion.is_empty()
                    && work.assign(opf::COVER, ctx.companion.bytes()).is_err()
                {
                    return false;
                }
                let Some(writer) = &mut ctx.writer else {
                    return false;
                };
                ctx.cache.spine_count = writer.cache.spine_count;
                ctx.cache.toc_count = writer.cache.toc_count;
                writer.build(work, ctx.diagnostics).is_ok()
            }
            x if x == A::Cleanup as u32 => {
                ctx.failed |= cleanup(ctx.cache_path, ctx.resources, ctx.leaves).is_err();
                true
            }
            x if x == A::Reload as u32 => {
                ctx.writer = None;
                *ctx.cache = BookCache::new(ctx.leaves);
                *ctx.metadata = Metadata::default();
                let mut path = Text::default();
                if path
                    .assign_parts(&[ctx.cache_path.bytes(), b"/book.bin"])
                    .is_err()
                {
                    return false;
                }
                ctx.cache.load(ctx.metadata, &path).is_ok()
            }
            _ => false,
        }
    }
    log::with_bytes(
        leaves,
        0,
        b"TXT",
        &[b"Building metadata cache for TXT: ", path.bytes()],
        format_args!(""),
    );
    let mut context = Context {
        path,
        cache_path,
        cache,
        metadata,
        resources,
        leaves,
        diagnostics,
        writer: None,
        title: Text::default(),
        work: None,
        companion: Text::default(),
        failed: false,
    };
    let result = unsafe { txt::build_cache((&mut context as *mut Context<'_>).cast(), control) };
    use txt::CacheAction as A;
    let error = match result {
        x if x == A::Begin as u32 => "Could not begin writing cache",
        x if x == A::BeginOpf as u32 => "Could not begin writing content.opf pass",
        x if x == A::EndOpf as u32 => "Could not end writing content.opf pass",
        x if x == A::BeginToc as u32 => "Could not begin writing toc pass",
        x if x == A::EndToc as u32 => "Could not end writing toc pass",
        x if x == A::End as u32 => "Could not end writing cache",
        x if x == A::Book as u32 => "Could not build book.bin for TXT",
        x if x == A::Reload as u32 => "Failed to reload cache after build",
        u32::MAX => return true,
        _ => return false,
    };
    log::message(leaves, 1, b"TXT", format_args!("{}", error));
    false
}

pub(in super::super) fn cleanup(
    cache_path: &Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
) -> Result<(), ()> {
    for suffix in [b"/spine.bin.tmp".as_slice(), b"/toc.bin.tmp"] {
        let mut path = Text::default();
        path.assign_parts(&[cache_path.bytes(), suffix])
            .map_err(|_| ())?;
        if unsafe { (resources.path_exists)(path.wire()) } != 0 {
            unsafe { (leaves.path_remove)(path.wire()) };
        }
    }
    Ok(())
}

fn companion(path: &Text, resources: &'static abi::Resources) -> Result<Text, ()> {
    struct Context<'a> {
        folder: &'a [u8],
        title: &'a [u8],
        resources: &'static abi::Resources,
        found: Text,
        failed: bool,
    }
    unsafe extern "C" fn candidate(
        raw: *mut c_void,
        name: *const u8,
        extension: *const u8,
    ) -> bool {
        let ctx = unsafe { &mut *raw.cast::<Context<'_>>() };
        let name = if name.is_null() {
            ctx.title
        } else {
            unsafe { core::ffi::CStr::from_ptr(name.cast()).to_bytes() }
        };
        let extension = unsafe { core::ffi::CStr::from_ptr(extension.cast()).to_bytes() };
        if ctx
            .found
            .assign_parts(&[ctx.folder, b"/", name, extension])
            .is_err()
        {
            ctx.failed = true;
            return true;
        }
        if unsafe { (ctx.resources.path_exists)(ctx.found.wire()) } != 0 {
            return true;
        }
        ctx.found.clear();
        false
    }
    let folder = path
        .bytes()
        .iter()
        .rposition(|&byte| byte == b'/')
        .filter(|&index| index != 0)
        .map_or(b"/".as_slice(), |index| &path.bytes()[..index]);
    let mut context = Context {
        folder,
        title: residual::txt_title(path.bytes()),
        resources,
        found: Text::default(),
        failed: false,
    };
    unsafe { txt::find_cover((&mut context as *mut Context<'_>).cast(), candidate) };
    if context.failed {
        Err(())
    } else if context.found.is_empty() {
        Ok(Text::default())
    } else {
        Ok(context.found)
    }
}

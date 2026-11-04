//! Session-owned build transitions. Only the build field is borrowed during callbacks.
extern crate alloc;
use super::super::{
    abi::XpEpubRenderSpec,
    book_owner::{log, queries, FileSlot},
    Text,
};
use super::{
    book::{self, CssStatus},
    driver::{BuildOwner, Step},
    file::CacheFile,
    image::Physical,
    owner::{Progress, ReaderSection},
    services::{Fonts, Init, Paths, Popup},
};
use crate::{app::abi, chapter::Options, section_cache};
use core::{ffi::c_void, ptr::NonNull};
use xp_core::{
    chapter::assembly,
    section::{PARTIAL_VERSION, VERSION},
};

pub(super) fn header(spec: &XpEpubRenderSpec, pages: u16) -> [u32; 20] {
    [
        0,
        spec.font as u32,
        spec.compression.to_bits(),
        u32::from(spec.paragraph_spacing != 0),
        spec.paragraph_indent.into(),
        spec.alignment.into(),
        spec.width.into(),
        spec.height.into(),
        u32::from(spec.hyphenation != 0),
        u32::from(spec.embedded_style != 0),
        spec.images.into(),
        u32::from(spec.focus_reading != 0),
        spec.character_spacing as u8 as u32,
        spec.word_spacing_percent.into(),
        pages.into(),
        0,
        0,
        0,
        0,
        0,
    ]
}

fn options(spec: &XpEpubRenderSpec) -> Options {
    Options {
        alignment: spec.alignment,
        embedded: u8::from(spec.embedded_style != 0),
        images: spec.images,
        css_enabled: u8::from(spec.embedded_style != 0),
        assembly: assembly::Options {
            font: spec.font,
            compression: spec.compression,
            width: spec.width,
            height: spec.height,
            extra_spacing: spec.paragraph_spacing != 0,
            alignment: spec.alignment,
            indent_spaces: spec.paragraph_indent,
            character_spacing: spec.character_spacing,
            word_spacing: spec.word_spacing_percent,
            hyphenation: spec.hyphenation != 0,
            focus: spec.focus_reading != 0,
        },
    }
}

fn cleanup(
    paths: &Paths,
    output: &mut FileSlot,
    leaves: &super::super::abi::XpEpubLeaves,
    html: bool,
) {
    output.close();
    unsafe { (leaves.path_remove)(paths.temporary_cache.wire()) };
    if html {
        unsafe { (leaves.path_remove)(paths.temporary_html.wire()) };
    }
}

impl ReaderSection {
    pub(in crate::app::epub) unsafe fn load_cache(
        raw: NonNull<Self>,
        spec: &XpEpubRenderSpec,
    ) -> bool {
        unsafe { Self::load(raw, &header(spec, 0)) }
    }

    pub(in crate::app::epub) unsafe fn html_cached(raw: NonNull<Self>) -> bool {
        let owner = raw.as_ptr();
        let mut path = Text::default();
        let mut digits = [0; 20];
        let spine = unsafe { (*owner).spine };
        if path
            .assign_parts(&[
                unsafe { queries::cache_bytes((*owner).book.as_ptr()) },
                b"/html/",
                if spine < 0 { b"-" } else { b"" },
                queries::decimal(u64::from(spine.unsigned_abs()), &mut digits),
                b".html",
            ])
            .is_err()
        {
            return false;
        }
        unsafe { ((*owner).resources.path_exists)(path.wire()) != 0 }
    }

    pub(in crate::app::epub) unsafe fn create(
        raw: NonNull<Self>,
        spec: &XpEpubRenderSpec,
        popup: Option<Popup>,
        runtime: &'static abi::Runtime,
    ) -> bool {
        unsafe {
            Self::start(raw, spec, popup, runtime)
                && Self::advance(raw, 0, false)
                && Self::complete(raw)
        }
    }

    pub(in crate::app::epub) unsafe fn start(
        raw: NonNull<Self>,
        spec: &XpEpubRenderSpec,
        popup: Option<Popup>,
        runtime: &'static abi::Runtime,
    ) -> bool {
        let owner = raw.as_ptr();
        let (resources, leaves, book, spine) = unsafe {
            (
                (*owner).resources,
                (*owner).leaves,
                (*owner).book,
                (*owner).spine,
            )
        };
        if unsafe { (*owner).build.is_some() } {
            log::message(
                leaves,
                1,
                b"SCT",
                format_args!("startBuild called while a build is already active"),
            );
            return false;
        }
        unsafe {
            (leaves.fonts_release_caches)();
            (*owner).complete = false;
            (*owner).readable.pages = if (*owner).readable.partial != 0 {
                (*owner).readable.partial_pages
            } else {
                0
            };
        }
        // Remove the stale binary before allocating the remaining preparation paths.
        let mut temporary = Text::default();
        if temporary
            .assign_parts(&[unsafe { (*owner).path.bytes() }, b".part"])
            .is_err()
        {
            return false;
        }
        if unsafe { (resources.path_exists)(temporary.wire()) } != 0 {
            unsafe { (leaves.path_remove)(temporary.wire()) };
        }
        drop(temporary);
        let Ok((mut paths, href)) = (unsafe { Paths::prepare(book, spine) }) else {
            return false;
        };
        let mut directory = Text::default();
        let cache = unsafe { queries::cache_bytes(book.as_ptr()) };
        if directory.assign_parts(&[cache, b"/sections"]).is_err() {
            return false;
        }
        unsafe { (leaves.path_mkdir)(directory.wire()) };
        let reused = unsafe { (resources.path_exists)(paths.html.wire()) != 0 };
        let mut cached = reused;
        if reused {
            log::with_bytes(
                leaves,
                0,
                b"SCT",
                &[b"Reusing cached HTML ", paths.html.bytes()],
                format_args!(""),
            );
        } else {
            if directory.assign_parts(&[cache, b"/html"]).is_err() {
                return false;
            }
            unsafe { (leaves.path_mkdir)(directory.wire()) };
            let mut streamed = false;
            let mut size = 0;
            for attempt in 0..3 {
                if attempt > 0 {
                    log::message(
                        leaves,
                        0,
                        b"SCT",
                        format_args!("Retrying stream (attempt {})...", attempt + 1),
                    );
                    unsafe { (runtime.delay_ms)(50) };
                }
                if unsafe { (resources.path_exists)(paths.temporary_html.wire()) } != 0 {
                    unsafe { (leaves.path_remove)(paths.temporary_html.wire()) };
                }
                let mut file = FileSlot::new();
                if !file.open(leaves, &paths.temporary_html, true, c"SCT") {
                    continue;
                }
                unsafe extern "C" fn write(
                    raw: *mut c_void,
                    bytes: *const u8,
                    length: usize,
                ) -> u8 {
                    (unsafe { (&mut *raw.cast::<FileSlot>()).write(bytes, length) } == length)
                        .into()
                }
                streamed = unsafe {
                    queries::stream(
                        book.as_ptr(),
                        href.wire(),
                        8192,
                        0,
                        write,
                        (&mut file as *mut FileSlot).cast(),
                    ) != 0
                };
                size = file.size();
                file.close();
                if !streamed && unsafe { (resources.path_exists)(paths.temporary_html.wire()) } != 0
                {
                    unsafe { (leaves.path_remove)(paths.temporary_html.wire()) };
                    log::message(
                        leaves,
                        0,
                        b"SCT",
                        format_args!("Removed incomplete temp file after failed attempt"),
                    );
                }
                if streamed {
                    break;
                }
            }
            if !streamed {
                log::message(
                    leaves,
                    1,
                    b"SCT",
                    format_args!("Failed to stream item contents to temp file after retries"),
                );
                return false;
            }
            log::with_bytes(
                leaves,
                0,
                b"SCT",
                &[b"Streamed temp HTML to ", paths.temporary_html.bytes()],
                format_args!(" ({} bytes)", size),
            );
            if unsafe { (leaves.path_rename)(paths.temporary_html.wire(), paths.html.wire()) } != 0
            {
                cached = true;
            } else {
                log::message(
                    leaves,
                    0,
                    b"SCT",
                    format_args!("Failed to promote HTML cache; parsing from temp"),
                );
            }
        }
        drop(directory);
        drop(href);
        let mut output = FileSlot::new();
        if !output.open(leaves, &paths.temporary_cache, true, c"SCT") {
            if !reused {
                unsafe { (leaves.path_remove)(paths.temporary_html.wire()) };
            }
            return false;
        }
        let pages = unsafe { (*owner).readable.pages };
        section_cache::header(
            &mut CacheFile::writing(
                &paths.final_cache,
                &paths.temporary_cache,
                &mut output,
                resources,
                leaves,
            ),
            &header(spec, pages),
        );
        if paths.select_html(cached).is_err() {
            cleanup(&paths, &mut output, leaves, !reused);
            return false;
        }
        let init = Init {
            book,
            resources,
            leaves,
            paths,
            output,
            pages,
            fonts: Fonts {
                metrics: unsafe { &*(leaves.build_metrics)() },
                allocator: unsafe { &*(leaves.build_allocator)() },
                variant: leaves.font_variant,
                line_height: leaves.font_line_height,
                ready: leaves.font_ready,
                codepoint: leaves.font_codepoint,
            },
            image: Physical {
                supported: leaves.image_supported,
                dimensions: leaves.image_dimensions,
                flush: leaves.file_flush,
                lend: leaves.framebuffer_lend,
                restore: leaves.framebuffer_restore,
                delay: runtime.delay_ms,
            },
            now: runtime.now_ms,
            popup,
            css: None,
            toc: alloc::vec::Vec::new(),
            language: 0,
        };
        let mut pending = Some(init);
        let mut build = match unsafe { BuildOwner::allocate(&mut pending) } {
            Some(build) => build,
            None => {
                let mut init = pending.take().unwrap();
                log::message(leaves, 1, b"SCT", format_args!("OOM: BuildContext"));
                cleanup(&init.paths, &mut init.output, leaves, !reused);
                return false;
            }
        };
        if spec.embedded_style != 0 {
            let cache = match Text::copy(cache) {
                Ok(cache) => cache,
                Err(_) => {
                    build
                        .services()
                        .output(|file| cleanup(&build.services().paths, file, leaves, !cached));
                    return false;
                }
            };
            match build.load_css(&cache) {
                CssStatus::LowMemory => {
                    log::message(
                        leaves,
                        1,
                        b"SCT",
                        format_args!("Insufficient heap to hydrate CSS; section build deferred"),
                    );
                    build.clear_css();
                    build
                        .services()
                        .output(|file| cleanup(&build.services().paths, file, leaves, !cached));
                    return false;
                }
                CssStatus::Invalid => log::message(
                    leaves,
                    1,
                    b"SCT",
                    format_args!("Failed to load CSS from cache"),
                ),
                CssStatus::Complete => {}
            }
        }
        let Ok(toc) = (unsafe { book::toc(book, spine) }) else {
            build.clear_css();
            build
                .services()
                .output(|file| cleanup(&build.services().paths, file, leaves, !reused));
            return false;
        };
        let Some(language) =
            Text::receive(|sink, raw| unsafe { queries::language(book.as_ptr(), sink, raw) })
        else {
            build.clear_css();
            build
                .services()
                .output(|file| cleanup(&build.services().paths, file, leaves, !reused));
            return false;
        };
        build.configure(toc, unsafe { (leaves.hyphen_language)(language.wire()) });
        drop(language);
        // Publish ownership before begin: begin failure follows abandon, including old-cache removal.
        unsafe {
            (*owner).progress = Progress::default();
            (*owner).build = Some(build);
        }
        let began = unsafe { (*owner).build.as_mut().unwrap().begin(&options(spec)) };
        if !began {
            log::message(leaves, 1, b"SCT", format_args!("Failed to begin parse"));
            unsafe { Self::abandon(raw) };
            return false;
        }
        unsafe {
            (*owner).progress.total = (*owner).build.as_ref().unwrap().services().input.total()
        };
        true
    }

    pub(in crate::app::epub) unsafe fn advance(
        raw: NonNull<Self>,
        max: i32,
        yield_input: bool,
    ) -> bool {
        let owner = raw.as_ptr();
        let leaves = unsafe { (*owner).leaves };
        let Some(start) = (unsafe { (*owner).build.as_ref() }).map(|b| b.services().built.get())
        else {
            log::message(
                leaves,
                1,
                b"SCT",
                format_args!("buildSomeMore with no active build"),
            );
            return false;
        };
        loop {
            match unsafe { (*owner).build.as_mut().unwrap().step() } {
                Step::Error => {
                    log::message(
                        leaves,
                        1,
                        b"SCT",
                        format_args!("Parse error during incremental build"),
                    );
                    unsafe { Self::abandon(raw) };
                    return false;
                }
                Step::Done => return unsafe { Self::finish(raw) },
                Step::More => {}
            }
            let built = unsafe { (*owner).build.as_ref().unwrap().services().built.get() };
            if (max > 0 && i32::from(built) - i32::from(start) >= max)
                || (yield_input && unsafe { (leaves.input_raw_contact)() } != 0)
            {
                unsafe {
                    (*owner).progress.consumed =
                        (*owner).build.as_ref().unwrap().services().input.consumed()
                };
                return true;
            }
        }
    }

    unsafe fn finish(raw: NonNull<Self>) -> bool {
        let owner = raw.as_ptr();
        if !unsafe { (*owner).build.as_mut().unwrap().finish() } {
            log::message(
                unsafe { (*owner).leaves },
                1,
                b"SCT",
                format_args!("Parse finalize failed; abandoning section build"),
            );
            unsafe { Self::abandon(raw) };
            return false;
        }
        let build = unsafe { (*owner).build.as_ref().unwrap() };
        let services = build.services();
        if services.paths.parse.bytes() != services.paths.html.bytes()
            && unsafe {
                (services.leaves.path_rename)(
                    services.paths.temporary_html.wire(),
                    services.paths.html.wire(),
                )
            } == 0
        {
            log::message(
                services.leaves,
                0,
                b"SCT",
                format_args!("Failed to promote HTML cache, removing temp"),
            );
            unsafe { (services.leaves.path_remove)(services.paths.temporary_html.wire()) };
        }
        let committed = services.commit(VERSION, 0, 0);
        let built = services.built.get();
        build.clear_css();
        let build = unsafe { (*owner).build.take() };
        drop(build);
        unsafe {
            (*owner).readable.partial = 0;
            (*owner).readable.partial_pages = 0;
            (*owner).readable.pages = if committed { built } else { 0 };
            if committed {
                (*owner).complete = true;
            }
        }
        committed
    }

    pub(in crate::app::epub) unsafe fn suspend(raw: NonNull<Self>) {
        let owner = raw.as_ptr();
        let Some(build) = (unsafe { (*owner).build.as_ref() }) else {
            return;
        };
        let services = build.services();
        let built = services.built.get();
        let readable = unsafe { (*owner).readable };
        let mut committed = false;
        if built > 0 && (readable.partial == 0 || built > readable.partial_pages) {
            let consumed = services.input.consumed();
            let total = unsafe { (*owner).progress.total };
            committed = services.commit(PARTIAL_VERSION, consumed, total);
            if committed {
                unsafe {
                    (*owner).readable.partial = 1;
                    (*owner).readable.partial_pages = built;
                    (*owner).readable.consumed = consumed;
                    (*owner).readable.total = total;
                }
                log::message(
                    services.leaves,
                    2,
                    b"SCT",
                    format_args!("Suspended build: {} pages persisted", built),
                );
            }
        }
        unsafe { Self::release_build(raw, !committed, false) };
        unsafe {
            (*owner).complete = false;
            (*owner).readable.pages = if (*owner).readable.partial != 0 {
                (*owner).readable.partial_pages
            } else {
                0
            };
        }
    }

    pub(in crate::app::epub) unsafe fn abandon(raw: NonNull<Self>) {
        let owner = raw.as_ptr();
        if unsafe { (*owner).build.is_none() } {
            return;
        }
        unsafe { Self::release_build(raw, true, true) };
        unsafe {
            (*owner).complete = false;
            (*owner).readable.partial = 0;
            (*owner).readable.partial_pages = 0;
            (*owner).readable.pages = 0;
        }
    }

    unsafe fn release_build(raw: NonNull<Self>, remove_binary: bool, remove_final: bool) {
        let owner = raw.as_ptr();
        let build = unsafe { (*owner).build.as_mut().unwrap() };
        build.abort();
        build.clear_css();
        let s = build.services();
        if remove_binary {
            s.output(|file| {
                if file.is_open() {
                    file.close();
                    unsafe { (s.leaves.path_remove)(s.paths.temporary_cache.wire()) };
                }
            });
        }
        if remove_final && unsafe { (s.resources.path_exists)(s.paths.final_cache.wire()) } != 0 {
            unsafe { (s.leaves.path_remove)(s.paths.final_cache.wire()) };
        }
        if s.paths.parse.bytes() != s.paths.html.bytes()
            && unsafe { (s.resources.path_exists)(s.paths.temporary_html.wire()) } != 0
        {
            unsafe { (s.leaves.path_remove)(s.paths.temporary_html.wire()) };
        }
        let build = unsafe { (*owner).build.take() };
        drop(build);
    }
}

#[cfg(feature = "app-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_section_spec(
    spec: &XpEpubRenderSpec,
    fields: *mut [u32; 20],
    flags: *mut [u8; 6],
) {
    let resolved = options(spec);
    unsafe {
        fields.write(header(spec, 3));
        flags.write([
            resolved.assembly.extra_spacing.into(),
            resolved.assembly.hyphenation.into(),
            resolved.assembly.focus.into(),
            resolved.embedded,
            resolved.css_enabled,
            resolved.images,
        ]);
    }
}

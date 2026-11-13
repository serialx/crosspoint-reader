//! Host-only composition of the real load context and query/mapping owners.
use super::*;
use core::{ffi::c_void, ptr};
use xp_core::sync::mapping::{self, Query, Span};

type Fault = unsafe extern "C" fn();
unsafe extern "C" fn now() -> u32 {
    0
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_discover_parse(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: app_abi::Text,
    cache: app_abi::Text,
    fault: Fault,
) -> u32 {
    let path = Text::copy(unsafe { path.bytes() }.unwrap()).unwrap();
    let cache_path = Text::copy(unsafe { cache.bytes() }.unwrap()).unwrap();
    let mut context = Context {
        path: &path,
        cache_path: &cache_path,
        resources,
        leaves,
        now,
        metadata: Metadata::default(),
        cache: BookCache::new(leaves),
        css: None,
        protection: Protection::Plain,
        paths: Paths::default(),
        writer: None,
        work: None,
        error: None,
    };
    let raw = (&mut context as *mut Context<'_>).cast();
    unsafe {
        fault();
        operation(raw, O::DiscoverCss as u32, 0);
        operation(raw, O::ReleaseMetadata as u32, 0);
        let result = operation(raw, O::ParseCss as u32, 0);
        result | (u32::from(context.error.is_some()) << 8)
    }
}

struct NoChapterRead;
impl super::super::super::progress_mapping::Chapter for NoChapterRead {
    fn read(&self, _: &mut dyn FnMut(&[u8])) -> bool {
        panic!("zero-page save needs no chapter read")
    }
}
struct SaveSource(*mut c_void);
impl super::super::super::progress_mapping::Source for SaveSource {
    type Chapter<'a> = NoChapterRead;
    fn spine_count(&self) -> i32 {
        unsafe { queries::spine_count(self.0) }
    }
    fn chapter(&self, _: i32) -> Option<NoChapterRead> {
        Some(NoChapterRead)
    }
    fn query(&mut self, _: u32, _: i32, _: usize, _: &[u8]) -> i64 {
        panic!("unexpected save query")
    }
    #[allow(clippy::useless_conversion)]
    fn progress(&self, spine: i32, fraction: f32) -> Option<f32> {
        unsafe { queries::progress(self.0, spine, fraction) }.into()
    }
    fn entity(_: &[u8]) -> i32 {
        panic!("unexpected entity")
    }
    fn log(&self, _: super::super::super::progress_mapping::Diagnostic<'_>) {}
    fn reverse(&mut self, _: i32, _: &mut dyn FnMut(&[u8])) -> bool {
        panic!("unexpected reverse")
    }
}

// The same seam compiles against the pre-fix numeric and checked-result APIs.
#[allow(clippy::useless_conversion)]
unsafe extern "C" fn query(raw: *mut c_void, kind: u32, spine: i32, arg: usize, _: Span) -> i64 {
    match kind {
        x if x == Query::BookSize as u32 => unsafe { queries::size(raw).into() },
        x if x == Query::SpineCount as u32 => unsafe { queries::spine_count(raw).into() },
        x if x == Query::SpineForSize as u32 => unsafe {
            queries::spine_for_size(raw, arg as u32).into()
        },
        x if x == Query::CumulativeSize as u32 => {
            let value: Option<u32> = unsafe { queries::cumulative_size(raw, spine) }.into();
            value.map_or(-1, i64::from)
        }
        x if x == Query::CachedPages as u32 => 1,
        _ => panic!("unexpected mapping query"),
    }
}
#[no_mangle]
#[allow(clippy::useless_conversion)]
pub unsafe extern "C" fn xp_epub_test_harden_progress(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    cache: app_abi::Text,
    fault: Fault,
    kind: u8,
) -> i32 {
    let cache_path = Text::copy(unsafe { cache.bytes() }.unwrap()).unwrap();
    let mut path = Text::default();
    path.assign_parts(&[cache_path.bytes(), b"/book.bin"])
        .unwrap();
    let mut cache = BookCache::new(leaves);
    let mut metadata = Metadata::default();
    cache.load(&mut metadata, &path).unwrap();
    let mut book = ReaderBook {
        #[cfg(feature = "app-test")]
        facade: None,
        resources,
        now,
        path,
        cache_path,
        metadata,
        cache,
        css: None,
        protection: Protection::Plain,
    };
    let raw = ptr::addr_of_mut!(book).cast();
    unsafe { fault() };
    if kind == 2 {
        let position = mapping::Position {
            spine: 64,
            page: 0,
            pages: 2,
            ..Default::default()
        };
        return super::super::super::progress_mapping::save(&mut SaveSource(raw), &position)
            .map_or(-1, |saved| (saved.percentage * 10000.0 + 0.5) as i32);
    }
    if kind == 0 {
        let value: Option<f32> = unsafe { queries::progress(raw, 64, 0.5) }.into();
        return value.map_or(-1, |value| (value * 10000.0 + 0.5) as i32);
    }
    unsafe extern "C" fn stream(
        _: *mut c_void,
        _: i32,
        _: *mut xp_core::sync::reverse::Reverse,
    ) -> u8 {
        panic!("percentage mapping must not read chapter content")
    }
    mapping::map(
        &mapping::Services {
            ctx: raw,
            query,
            stream,
            diagnose: None,
        },
        b"",
        0.71,
        0,
        1,
        0,
    )
    .spine
}

// Real shipping book/cache queries feed the native percentage boundary under a HAL fault.
#[cfg(not(feature = "app-test"))]
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_harden_boundary(
    resources: &'static app_abi::Resources,
    leaves: &'static XpEpubLeaves,
    runtime: &'static app_abi::Runtime,
    cache: app_abi::Text,
    fault: Fault,
    known: u8,
    out: *mut app_abi::Screenshot,
) -> i32 {
    let cache_path = Text::copy(unsafe { cache.bytes() }.unwrap()).unwrap();
    let mut path = Text::default();
    path.assign_parts(&[cache_path.bytes(), b"/book.bin"])
        .unwrap();
    let mut cache = BookCache::new(leaves);
    let mut metadata = Metadata::default();
    cache.load(&mut metadata, &path).unwrap();
    let mut book = ReaderBook {
        #[cfg(feature = "app-test")]
        facade: None,
        resources,
        now,
        path,
        cache_path,
        metadata,
        cache,
        css: None,
        protection: Protection::Plain,
    };
    let raw = ptr::addr_of_mut!(book).cast();

    book.metadata.title.extend_from_slice(b"Book");
    let owner = super::super::super::EpubSession::try_new(
        b"/book.epub",
        false,
        3,
        leaves,
        runtime,
        resources,
    )
    .unwrap();
    let access = owner.borrowed_resources();
    owner.set_book(&access, core::ptr::NonNull::new(raw));
    owner.model(|state| {
        state.current.spine = 32;
        state.current.page = 1;
        state.facts.section = Some(super::super::super::state::SectionFacts {
            pages: 2,
            estimated_pages: 2,
            building: false,
            partial: false,
        });
    });
    if known != 0 {
        assert_eq!(owner.screenshot_info().progress_percent, 33);
    }
    owner.model(|state| state.current.spine = 64);
    unsafe { fault() };
    assert_eq!(
        owner.book_percent_for(super::super::super::projection::ChapterPosition {
            page: 1,
            count: 2
        }),
        -1
    );
    let info = owner.screenshot_info();
    let exit = unsafe { super::super::super::base::xp_epub_test_exit_percent(&*owner) };
    unsafe { out.write(info) };
    super::super::super::bookmarks::test_failed_commit(
        &owner,
        core::ptr::NonNull::new(raw).unwrap(),
    );
    owner.set_book(&access, None);
    exit
}

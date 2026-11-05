use super::super::{items::Items, log, zip::Archive, Vec};
use super::{abi, run, Action, Box, Buffer, Css, FileSlot, Text};
use core::{
    ffi::c_void,
    mem::{size_of, MaybeUninit},
    slice,
};
use xp_core::{
    book::{css as policy, residual, residual_abi::*},
    epub::cache::sort_legacy_slice,
};

pub(in super::super) fn discover(
    items: &Items<'_>,
    base: &[u8],
    paths: &mut Vec<Text>,
) -> Result<(), ()> {
    unsafe extern "C" fn path(raw: *mut c_void, index: usize) -> residual::Span {
        let paths = unsafe { &*raw.cast::<Vec<Text>>() };
        residual::Span::new(paths[index].bytes())
    }
    let mut failed = false;
    let ok = Archive::new(items.path, items.resources, items.leaves).enumerate_until(|name, _| {
        if !unsafe {
            residual::discover_css(
                base,
                name,
                paths.len(),
                (paths as *mut Vec<Text>).cast(),
                path,
            )
        } {
            return true;
        }
        log::with_bytes(
            items.leaves,
            0,
            b"EBP",
            &[b"Discovered CSS file via ZIP enumeration: ", name],
            format_args!(""),
        );
        let value = Text::copy(name);
        if paths.try_reserve(1).is_err() || value.is_err() {
            failed = true;
            return false;
        }
        paths.push(value.unwrap());
        true
    });
    if !ok {
        log::message(
            items.leaves,
            1,
            b"EBP",
            format_args!("Failed to enumerate ZIP file paths for CSS discovery"),
        );
    }
    if failed {
        Err(())
    } else {
        Ok(())
    }
}

struct Temp {
    path: Text,
    file: FileSlot,
}
struct Context<'a, 'b> {
    items: &'a Items<'b>,
    paths: &'a [Text],
    cache_path: &'a Text,
    owner: &'a mut Option<Box<Css>>,
    entries: Option<Buffer>,
    temp: Option<Temp>,
}

pub(in super::super) fn parse_files(
    items: &Items<'_>,
    paths: &[Text],
    cache_path: &Text,
    owner: &mut Option<Box<Css>>,
    partial_cache: bool,
) -> u32 {
    let mut context = Context {
        items,
        paths,
        cache_path,
        owner,
        entries: None,
        temp: None,
    };
    let services = policy::ParseServices {
        ctx: (&mut context as *mut Context<'_, '_>).cast(),
        call,
        diagnostic: Some(diagnostic),
    };
    unsafe { policy::parse_files(&services, paths.len(), partial_cache) }
}

impl Context<'_, '_> {
    fn dedup(&mut self, count: usize) -> Option<*mut u8> {
        let bytes = count.checked_mul(size_of::<policy::CssEntry>())?;
        let buffer = Buffer::allocate(self.items.resources, abi::MEMORY_DEFAULT, bytes)?;
        // Native value-initializes this same array before hash/index assignment.
        unsafe { buffer.as_ptr().write_bytes(0, bytes) };
        let entries =
            unsafe { slice::from_raw_parts_mut(buffer.as_ptr().cast::<policy::CssEntry>(), count) };
        for (index, entry) in entries.iter_mut().enumerate() {
            entry.path_hash = xp_core::fnv1a64(self.paths[index].bytes());
            entry.css_index = index;
        }
        sort_legacy_slice(entries, |a, b| a.path_hash < b.path_hash).ok()?;
        unsafe extern "C" fn path(raw: *mut c_void, index: usize) -> policy::Span {
            let paths = unsafe { &*raw.cast::<&[Text]>() };
            let bytes = paths[index].bytes();
            policy::Span {
                data: bytes.as_ptr(),
                len: bytes.len(),
            }
        }
        let paths = self.paths;
        Archive::new(self.items.path, self.items.resources, self.items.leaves).enumerate(
            |name, entry| {
                unsafe {
                    policy::css_observe(
                        entries,
                        name,
                        entry.crc32,
                        entry.compressed_size,
                        (&paths as *const &[Text]).cast_mut().cast(),
                        path,
                    )
                };
            },
        );
        sort_legacy_slice(entries, |a, b| a.css_index < b.css_index).ok()?;
        let data = buffer.as_ptr();
        self.entries = Some(buffer);
        Some(data)
    }

    fn operation(&mut self, operation: u32, index: usize, argument: usize) -> usize {
        match operation {
            XP_BOOK_CSS_FILE_CLEAR => {
                *self.owner = None;
                1
            }
            XP_BOOK_CSS_FILE_DEDUP => self.dedup(argument).map_or(0, |p| p as usize),
            XP_BOOK_CSS_FILE_HEAP => {
                let mut heap = MaybeUninit::uninit();
                unsafe {
                    (self.items.leaves.heap_snapshot)(heap.as_mut_ptr());
                    heap.assume_init().free_bytes as usize
                }
            }
            XP_BOOK_CSS_FILE_SIZE => {
                self.items.size(self.paths[index].bytes()).unwrap_or(0) as usize
            }
            XP_BOOK_CSS_FILE_OPEN_WRITE => {
                let mut path = Text::default();
                if path
                    .assign_parts(&[self.cache_path.bytes(), b"/.tmp.css"])
                    .is_err()
                {
                    return 0;
                }
                let temp = self.temp.insert(Temp {
                    path,
                    file: FileSlot::new(),
                });
                usize::from(temp.file.open(self.items.leaves, &temp.path, true, c"EBP"))
            }
            XP_BOOK_CSS_FILE_STREAM => {
                unsafe extern "C" fn write(
                    raw: *mut c_void,
                    bytes: *const u8,
                    length: usize,
                ) -> u8 {
                    u8::from(
                        unsafe { (&mut *raw.cast::<FileSlot>()).write(bytes, length) } == length,
                    )
                }
                let file = &mut self.temp.as_mut().unwrap().file;
                usize::from(unsafe {
                    self.items.stream(
                        self.paths[index].bytes(),
                        argument,
                        false,
                        write,
                        (file as *mut FileSlot).cast(),
                    )
                })
            }
            XP_BOOK_CSS_FILE_CLOSE => {
                self.temp.as_mut().unwrap().file.close();
                1
            }
            XP_BOOK_CSS_FILE_REMOVE => {
                unsafe { (self.items.leaves.path_remove)(self.temp.as_ref().unwrap().path.wire()) };
                1
            }
            XP_BOOK_CSS_FILE_OPEN_READ => {
                let temp = self.temp.as_mut().unwrap();
                usize::from(temp.file.open(self.items.leaves, &temp.path, false, c"EBP"))
            }
            XP_BOOK_CSS_FILE_PARSE => run(
                self.owner,
                self.cache_path,
                self.items.resources,
                self.items.leaves,
                Action::Stream,
                false,
                Some(&mut self.temp.as_mut().unwrap().file),
            ) as usize,
            XP_BOOK_CSS_FILE_DROP_TEMP => {
                self.temp = None;
                1
            }
            XP_BOOK_CSS_FILE_EMPTY => usize::from(
                self.owner
                    .as_ref()
                    .is_none_or(|css| css.store.rule_count() == 0),
            ),
            XP_BOOK_CSS_FILE_SAVE => run(
                self.owner,
                self.cache_path,
                self.items.resources,
                self.items.leaves,
                Action::Save,
                argument != 0,
                None,
            ) as usize,
            _ => 0,
        }
    }
}

unsafe extern "C" fn call(raw: *mut c_void, op: u32, index: usize, argument: usize) -> usize {
    unsafe { (&mut *raw.cast::<Context<'_, '_>>()).operation(op, index, argument) }
}

unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32, index: usize, value: usize) {
    let ctx = unsafe { &*raw.cast::<Context<'_, '_>>() };
    let leaves = ctx.items.leaves;
    let path = || {
        ctx.paths[index]
            .bytes()
            .split(|&byte| byte == 0)
            .next()
            .unwrap_or_default()
    };
    macro_rules! emit {
        ($severity:expr, $($argument:tt)*) => { log::message(leaves, $severity, b"EBP", format_args!($($argument)*)) };
    }
    match event {
        XP_BOOK_CSS_LOG_NO_FILES => emit!(
            0,
            "No CSS files to parse, but CssParser created for inline styles"
        ),
        XP_BOOK_CSS_LOG_COUNT => emit!(0, "CSS files to parse: {}", value),
        XP_BOOK_CSS_LOG_DEDUP_OOM => emit!(
            1,
            "Insufficient heap for CSS deduplication; parsing every stylesheet"
        ),
        XP_BOOK_CSS_LOG_PARSING => log::with_bytes(
            leaves,
            0,
            b"EBP",
            &[b"Parsing CSS file: ", path()],
            format_args!(""),
        ),
        XP_BOOK_CSS_LOG_LOW_HEAP => log::with_tail(
            leaves,
            1,
            b"EBP",
            format_args!(
                "Insufficient heap for CSS parsing ({} bytes free, need 65536), skipping: ",
                value as u32
            ),
            &[path()],
        ),
        XP_BOOK_CSS_LOG_OVERSIZE => log::with_tail(
            leaves,
            1,
            b"EBP",
            format_args!(
                "CSS file too large ({} bytes > 131072 max), skipping: ",
                value
            ),
            &[path()],
        ),
        XP_BOOK_CSS_LOG_CREATE => emit!(1, "Could not create temp CSS file"),
        XP_BOOK_CSS_LOG_READ => log::with_bytes(
            leaves,
            1,
            b"EBP",
            &[b"Could not read CSS file: ", path()],
            format_args!(""),
        ),
        XP_BOOK_CSS_LOG_OPEN => emit!(1, "Could not open temp CSS file for reading"),
        XP_BOOK_CSS_LOG_FAILED => emit!(
            1,
            "CSS parse failed; preserving any previous cache for a later retry"
        ),
        XP_BOOK_CSS_LOG_EMPTY_PARTIAL => emit!(
            1,
            "CSS parsing stopped before any usable rules were loaded; cache will not be replaced"
        ),
        XP_BOOK_CSS_LOG_KEEP_PARTIAL => emit!(
            0,
            "CSS retry remained partial; preserving the previous partial cache"
        ),
        XP_BOOK_CSS_LOG_SAVE => emit!(1, "Failed to save CSS rules to cache"),
        XP_BOOK_CSS_LOG_LOADED => emit!(
            0,
            "Loaded {} CSS style rules from {} files ({} identical duplicates skipped, {})",
            ctx.owner.as_ref().map_or(0, |css| css.store.rule_count()),
            ctx.paths.len(),
            index,
            if value == XP_BOOK_CSS_PARSE_COMPLETE as usize {
                "complete"
            } else {
                "partial"
            }
        ),
        _ => (),
    }
}

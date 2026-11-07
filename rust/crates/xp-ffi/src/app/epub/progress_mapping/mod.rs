//! Reader-owned progress adapters over the shared mapping and XPath algorithms.
extern crate alloc;
use super::Text;
use core::ffi::c_void;
use xp_core::sync::{
    mapping::{self, Event, Position, Span},
    reverse::Reverse,
};
mod decimal;
pub(super) mod diagnostics;
mod xpath;
pub(super) use xpath::resolve;

pub(super) trait Chapter {
    fn read(&self, consume: &mut dyn FnMut(&[u8])) -> bool;
}
pub(super) enum Diagnostic<'a> {
    ParserAllocation,
    Xml {
        code: u32,
        final_input: bool,
    },
    Resolved {
        mode: u32,
        spine: i32,
        value: u32,
        ratio: f32,
        path: &'a [u8],
    },
    Map {
        position: &'a Position,
        event: &'a Event,
        text: &'a [u8],
    },
    Saved {
        position: &'a Position,
        percentage: f32,
        path: &'a [u8],
    },
}
pub(super) trait Source {
    type Chapter<'a>: Chapter
    where
        Self: 'a;
    fn spine_count(&self) -> i32;
    fn chapter(&self, spine: i32) -> Option<Self::Chapter<'_>>;
    fn query(&mut self, kind: u32, spine: i32, arg: usize, text: &[u8]) -> i64;
    fn progress(&self, spine: i32, fraction: f32) -> f32;
    fn entity(data: &[u8]) -> i32;
    fn log(&self, diagnostic: Diagnostic<'_>);
    // Resolve the href, then release the temporary section before streaming.
    fn reverse(&mut self, spine: i32, consume: &mut dyn FnMut(&[u8])) -> bool;
}
pub(super) struct Saved {
    pub(super) xpath: Text,
    pub(super) percentage: f32,
}
struct Adapter<'a, S> {
    source: &'a mut S,
    xpath: Text,
    failed: bool,
}
unsafe fn span<'a>(value: Span) -> &'a [u8] {
    if value.len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(value.data, value.len) }
    }
}
impl<S: Source> Adapter<'_, S> {
    fn services(&mut self) -> mapping::Services {
        mapping::Services {
            ctx: (self as *mut Self).cast(),
            query: Self::query,
            stream: Self::stream,
            diagnose: Some(Self::diagnose),
        }
    }
    unsafe extern "C" fn query(
        raw: *mut c_void,
        kind: u32,
        spine: i32,
        arg: usize,
        text: Span,
    ) -> i64 {
        unsafe { &mut *raw.cast::<Self>() }
            .source
            .query(kind, spine, arg, unsafe { span(text) })
    }
    unsafe extern "C" fn stream(raw: *mut c_void, spine: i32, reverse: *mut Reverse) -> u8 {
        let this = unsafe { &mut *raw.cast::<Self>() };
        unsafe extern "C" fn entity<S: Source>(data: *const u8, len: usize) -> i32 {
            S::entity(unsafe { core::slice::from_raw_parts(data, len) })
        }
        u8::from(this.source.reverse(spine, &mut |data| unsafe {
            (*reverse).feed(data, Some(entity::<S>))
        }))
    }
    unsafe extern "C" fn diagnose(
        raw: *mut c_void,
        position: *const Position,
        event: *const Event,
    ) {
        let this = unsafe { &*raw.cast::<Self>() };
        this.source.log(Diagnostic::Map {
            position: unsafe { &*position },
            event: unsafe { &*event },
            text: unsafe { span((*event).text) },
        });
    }
    unsafe extern "C" fn progress(raw: *mut c_void, spine: i32, fraction: f32) -> f32 {
        unsafe { &*raw.cast::<Self>() }
            .source
            .progress(spine, fraction)
    }
    unsafe extern "C" fn xpath(
        raw: *mut c_void,
        spine: i32,
        mode: u32,
        value: u32,
        ratio: f32,
    ) -> u8 {
        let this = unsafe { &mut *raw.cast::<Self>() };
        match resolve(this.source, spine, mode, value, ratio) {
            Ok(text) => {
                this.xpath = text;
                u8::from(!this.xpath.bytes().is_empty())
            }
            Err(()) => {
                this.failed = true;
                0
            }
        }
    }
    unsafe extern "C" fn append(raw: *mut c_void, bytes: Span) {
        let this = unsafe { &mut *raw.cast::<Self>() };
        if this.failed {
            return;
        }
        if this.xpath.append(unsafe { span(bytes) }).is_err() {
            this.failed = true;
        }
    }
}
#[inline(never)]
pub(super) fn save<S: Source>(source: &mut S, position: &Position) -> Option<Saved> {
    let mut adapter = Adapter {
        source,
        xpath: Text::default(),
        failed: false,
    };
    let services = mapping::SaveServices {
        services: adapter.services(),
        progress: Adapter::<S>::progress,
        xpath: Adapter::<S>::xpath,
        append: Adapter::<S>::append,
    };
    let percentage = mapping::save(&services, position);
    if adapter.failed {
        return None;
    }
    adapter.source.log(Diagnostic::Saved {
        position,
        percentage,
        path: adapter.xpath.bytes(),
    });
    Some(Saved {
        xpath: adapter.xpath,
        percentage,
    })
}
#[inline(never)]
pub(super) fn map(
    source: &mut impl Source,
    xpath: &[u8],
    percentage: f32,
    current: i32,
    pages: i32,
) -> Position {
    let mut adapter = Adapter {
        source,
        xpath: Text::default(),
        failed: false,
    };
    mapping::map(&adapter.services(), xpath, percentage, current, pages, 0)
}

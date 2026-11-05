use super::*;
use crate::render_resources::physical::{self, Kind, Op, Query};
use std::cell::{Cell, UnsafeCell};

struct Panel {
    pixels: UnsafeCell<[u8; 1600]>,
    target: Cell<*mut u8>,
    rows: Cell<u16>,
    mode: Cell<u8>,
    width: Cell<u16>,
    stride: Cell<u16>,
    caps: Cell<u32>,
    source: Cell<*mut crate::display_source::DisplaySource>,
    seen_width: Cell<u16>,
    reject: Cell<bool>,
}
impl Panel {
    fn new() -> Self {
        Self {
            pixels: UnsafeCell::new([0; 1600]),
            target: Cell::new(std::ptr::null_mut()),
            rows: Cell::new(0),
            mode: Cell::new(2),
            width: Cell::new(800),
            stride: Cell::new(100),
            caps: Cell::new(0),
            source: Cell::new(std::ptr::null_mut()),
            seen_width: Cell::new(0),
            reject: Cell::new(false),
        }
    }
}
impl PhysicalResources for Panel {
    unsafe fn prewarm_begin(&self, _: *mut PrewarmStorage) -> bool {
        true
    }
    unsafe fn prewarm_finish(&self, _: *mut PrewarmStorage) {}
    unsafe fn prewarm_drop(&self, _: *mut PrewarmStorage) {}
    fn detach_source(&self) {
        let source = self.source.get();
        assert!(!source.is_null());
        record("detach:enter");
        unsafe {
            ((*source).paint.unwrap())((*source).ctx, (*source).scratch, 0, 1);
        }
        record("detach:leave");
        self.source.set(std::ptr::null_mut());
    }
    fn clear_plan(&self) {}
    fn clear_window(&self) {}
    fn release_images(&self) {}
}
impl physical::Physical for Panel {
    fn query(&self, query: Query) -> u32 {
        match query {
            Query::WidthBytes => self.stride.get().into(),
            Query::OverlayCaps | Query::AbsoluteCaps => self.caps.get(),
            Query::Mode => self.mode.get().into(),
            Query::Width => self.width.get().into(),
            Query::Height => 16,
            Query::IsBw => u32::from(self.mode.get() == 0),
            Query::StripBoard => 1,
            Query::FreeHeap | Query::MaxBlock => 200000,
            _ => 0,
        }
    }
    fn operation(&self, op: &Op) -> u32 {
        record(&format!("physical:{}", op.kind));
        match op.kind {
            k if k == Kind::Mode as u8 => self.mode.set(op.mode),
            k if k == Kind::StripBegin as u8 => {
                assert!(self.target.get().is_null());
                self.target.set(op.buffer);
                self.rows.set(op.rows);
            }
            k if k == Kind::Clear as u8 => unsafe {
                self.target
                    .get()
                    .write_bytes(op.value as u8, usize::from(self.rows.get()) * 100);
            },
            k if k == Kind::CopyStatus as u8 => {
                assert_eq!(op.value as i32, -7);
                unsafe { self.target.get().add(2).write(0x33) };
            }
            k if k == Kind::StripEnd as u8 => self.target.set(std::ptr::null_mut()),
            k if [
                Kind::Display,
                Kind::DisplayAsync,
                Kind::GrayBase,
                Kind::Precondition,
                Kind::Wait,
                Kind::WritePlane,
                Kind::GrayDisplay,
                Kind::Cleanup,
                Kind::CopyLsb,
                Kind::CopyMsb,
                Kind::RestoreBw,
            ]
            .iter()
            .any(|kind| k == *kind as u8) => {}
            _ => panic!("unexpected physical operation {}", op.kind),
        }
        0
    }
    fn plan(&self, plan: *mut crate::page_render::Plan, _: *mut u8, _: u16, recording: bool) {
        if recording {
            unsafe {
                (*plan).valid = 1;
            }
        }
    }
    fn image_window(&self, _: *mut u8, _: u32) {}
    fn source(&self, source: *mut crate::display_source::DisplaySource) -> bool {
        self.seen_width.set(unsafe { (*source).width });
        if self.reject.get() {
            return false;
        }
        self.source.set(source);
        true
    }
    fn framebuffer(&self) -> (*mut u8, usize) {
        (self.pixels.get().cast(), 1600)
    }
}

struct Content {
    images: bool,
    aa: bool,
}
impl crate::render_resources::renderer::Context for Content {
    fn now(&self) -> u32 {
        0
    }
    fn has_images(&self) -> bool {
        self.images
    }
    fn images_need_decode(&self) -> bool {
        false
    }
    fn elements(&self) -> u16 {
        1
    }
    fn take_forced(&self) -> bool {
        false
    }
    fn counter(&self) -> u32 {
        3
    }
    fn set_counter(&self, _: u32) {}
    fn frequency(&self) -> u32 {
        3
    }
    fn text_aa(&self) -> bool {
        self.aa
    }
    fn is_a4(&self) -> bool {
        true
    }
    fn image_reset(&self) {}
    fn paint(&self, _: u8) {
        record("page:paint");
    }
    fn highlights(&self) {
        record("page:highlight");
    }
    fn status(&self) {}
    fn log_enabled(&self, _: u8) -> bool {
        true
    }
    fn log(&self, _: u8, text: &[u8]) {
        record(std::str::from_utf8(text).unwrap());
    }
}

#[test]
fn source_receives_exact_pixel_width_and_detach_reenters_live_context() {
    reset(0);
    let owner = pinned();
    let panel = Panel::new();
    // The strip allocator admits stride 100; width must not be inferred as 800.
    panel.width.set(758);
    crate::render_resources::renderer::run(
        owner.as_ref(),
        &[1],
        &panel,
        &Content {
            images: false,
            aa: false,
        },
        &RESOURCES,
        PageGeometry {
            status_top: -7,
            ..PageGeometry::EMPTY
        },
        true,
    );
    assert_eq!(panel.seen_width.get(), 758);
    assert!(panel.source.get().is_null());
    let trace = events();
    let start = trace.iter().position(|x| x == "detach:enter").unwrap();
    let finish = trace.iter().position(|x| x == "detach:leave").unwrap();
    assert!(trace[start..finish].iter().any(|x| x == "page:paint"));
    assert!(trace[start..finish].iter().any(|x| x == "page:highlight"));
    assert!(owner.as_ref().release_idle_strips());
    drop(owner);
    reset(0);
}

#[test]
fn command_composition_releases_every_partial_reservation_and_rejected_source() {
    for images in [false, true] {
        for fail in 0..=if images { 4 } else { 3 } {
            reset(fail);
            let owner = pinned();
            let panel = Panel::new();
            panel.reject.set(true);
            crate::render_resources::renderer::run(
                owner.as_ref(),
                &[1],
                &panel,
                &Content { images, aa: false },
                &RESOURCES,
                PageGeometry {
                    status_top: -7,
                    ..PageGeometry::EMPTY
                },
                true,
            );
            assert!(panel.source.get().is_null());
            assert!(!owner.as_ref().release_idle_strips());
            assert!(!events().iter().any(|x| x == "detach:enter"));
            drop(owner);
            reset(0);
        }
    }
}

#[test]
fn unsupported_stride_is_rejected_before_source_admission() {
    reset(0);
    let owner = pinned();
    let panel = Panel::new();
    panel.width.set(758);
    panel.stride.set(95);
    crate::render_resources::renderer::run(
        owner.as_ref(),
        &[1],
        &panel,
        &Content {
            images: false,
            aa: false,
        },
        &RESOURCES,
        PageGeometry {
            status_top: -7,
            ..PageGeometry::EMPTY
        },
        true,
    );
    assert_eq!(panel.seen_width.get(), 0);
    assert_eq!(TRACE.with(|trace| trace.borrow().calls), 0);
    assert!(events()
        .iter()
        .any(|event| event == "OOM: reader strips (28480 bytes); using framebuffer"));
}

#[test]
fn tiled_command_composition_covers_plane_and_scratch_allocation_failures() {
    for strips in [false, true] {
        let mut count = 0;
        for fail in 0..=6 {
            if fail > count && fail != 0 {
                break;
            }
            reset(fail);
            let owner = pinned();
            let panel = Panel::new();
            panel.caps.set(5); // Tiled async with separate base, unchanged planner.
            crate::render_resources::renderer::run(
                owner.as_ref(),
                &[1],
                &panel,
                &Content {
                    images: false,
                    aa: true,
                },
                &RESOURCES,
                PageGeometry {
                    status_top: -7,
                    ..PageGeometry::EMPTY
                },
                strips,
            );
            if fail == 0 {
                count = TRACE.with(|trace| trace.borrow().calls);
                assert!(count >= 2);
            }
            assert!(panel.source.get().is_null());
            drop(owner);
            reset(0);
        }
    }
}

#[test]
fn text_replay_retains_committed_bytes_and_rejects_gaps_and_partial_repeats() {
    reset(0);
    let owner = pinned();
    let panel = Panel::new();
    let frame = owner.as_ref().begin_frame(&[1], &panel);
    frame.configure(PageGeometry {
        status_top: -7,
        inverted: true,
        ..PageGeometry::EMPTY
    });
    let mut scratch = [0; 800];
    let result = unsafe {
        owner.replay(
            &panel,
            scratch.as_mut_ptr(),
            0,
            8,
            || {
                assert_eq!(panel.mode.get(), 0);
                panel.target.get().write(0x80);
            },
            || {
                assert_eq!(*panel.target.get(), 0x80);
                panel.target.get().add(1).write(0x0f);
            },
        )
    };
    assert_eq!(result, 0);
    assert_eq!(panel.mode.get(), 2);
    assert_eq!(&scratch[..4], &[0x7f, 0xf0, 0xcc, 0]);
    assert_eq!(
        unsafe { std::slice::from_raw_parts(panel.pixels.get().cast::<u8>(), 800) },
        &scratch
    );
    clear_events();
    scratch.fill(0xa5);
    assert_eq!(
        unsafe {
            owner.replay(
                &panel,
                scratch.as_mut_ptr(),
                0,
                8,
                || panic!("committed highlight"),
                || panic!("committed paint"),
            )
        },
        0
    );
    assert_eq!(&scratch[..4], &[0x7f, 0xf0, 0xcc, 0]);
    for (y, rows) in [(4, 8), (9, 1)] {
        assert_eq!(
            unsafe {
                owner.replay(
                    &panel,
                    scratch.as_mut_ptr(),
                    y,
                    rows,
                    || panic!("rejected highlight"),
                    || panic!("rejected paint"),
                )
            },
            1
        );
    }
    assert!(events().is_empty());
}

#[test]
fn image_replay_compares_retained_bytes_without_committing_mismatches() {
    reset(0);
    let owner = pinned();
    let panel = Panel::new();
    unsafe {
        panel.pixels.get().cast::<u8>().write_bytes(0xff, 1600);
    }
    let frame = owner.as_ref().begin_frame(&[1], &panel);
    frame.configure(PageGeometry {
        status_top: -7,
        images: true,
        ..PageGeometry::EMPTY
    });
    let mut scratch = [0; 100];
    // Status differs, so the panel rejects the source. Retained bytes stay intact.
    assert_eq!(
        unsafe { owner.replay(&panel, scratch.as_mut_ptr(), 7, 1, || {}, || {}) },
        1
    );
    assert_eq!(unsafe { *panel.pixels.get().cast::<u8>().add(702) }, 255);
    unsafe {
        panel.pixels.get().cast::<u8>().add(702).write(0x33);
    }
    assert_eq!(
        unsafe { owner.replay(&panel, scratch.as_mut_ptr(), 7, 1, || {}, || {}) },
        0
    );
    assert_eq!(panel.mode.get(), 2);
}

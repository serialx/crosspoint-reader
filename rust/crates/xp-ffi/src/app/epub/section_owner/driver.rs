//! Incremental parsing keeps Chapter destruction ahead of its service allocation.
use super::super::book_owner::log;
use super::{
    diagnostics,
    services::{BuildServices, Init},
};
use crate::chapter::{self, Chapter, Options};
use core::ptr::NonNull;
use xp_core::chapter::ReaderServices;

#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Step {
    More,
    Done,
    Error,
}

pub(super) struct BuildOwner {
    chapter: Option<NonNull<Chapter>>,
    services: NonNull<BuildServices>,
}

impl BuildOwner {
    // Book, physical tables and popup context outlive this owner. No callback may
    // reenter its lifecycle; callbacks can access only the separate service allocation.
    pub(super) unsafe fn new(init: Init) -> Option<Self> {
        Some(Self {
            chapter: None,
            services: BuildServices::allocate(init)?,
        })
    }

    pub(super) fn services(&self) -> &BuildServices {
        unsafe { self.services.as_ref() }
    }

    pub(super) fn load_css(&self, cache: &super::super::Text) -> super::book::CssStatus {
        assert!(self.chapter.is_none());
        unsafe { self.services().load_css(cache) }
    }

    pub(super) fn clear_css(&self) {
        assert!(self.chapter.is_none());
        unsafe { self.services().clear_css() };
    }

    pub(super) fn begin(&mut self, options: &Options) -> bool {
        if self.chapter.is_none() {
            let service: NonNull<dyn ReaderServices> = self.services;
            match unsafe { Chapter::create_reader(service, self.services().css(), options) } {
                Ok(chapter) => self.chapter = Some(chapter),
                Err(_) => {
                    log::message(
                        self.services().leaves,
                        1,
                        b"EHP",
                        format_args!("Couldn't allocate memory for parser"),
                    );
                    return false;
                }
            }
        }
        self.services().stopped.set(false);
        let chapter = self.chapter.unwrap();
        unsafe { Chapter::begin(chapter) };
        if unsafe { Chapter::oom(chapter) } {
            self.abort();
            return false;
        }
        if self.services().input.replace_buffer(1024).is_none() {
            log::message(
                self.services().leaves,
                1,
                b"EHP",
                format_args!("Couldn't allocate memory for buffer"),
            );
            self.abort();
            return false;
        }
        let services = self.services();
        if !services.input.open(services.leaves, &services.paths.parse) {
            self.abort();
            return false;
        }
        if services.input.total() >= 10 * 1024 {
            if let Some(popup) = services.popup {
                popup.show();
            }
        }
        services.started.set(unsafe { (services.now)() });
        true
    }

    fn oom(&self) -> bool {
        self.chapter
            .is_some_and(|chapter| unsafe { Chapter::oom(chapter) })
    }

    fn layout_oom(&self) {
        log::message(
            self.services().leaves,
            1,
            b"EHP",
            format_args!("Text layout dropped content (OOM); failing section build"),
        );
    }

    pub(super) fn step(&mut self) -> Step {
        if self.services().stopped.get() {
            return Step::Error;
        }
        if self.oom() {
            self.layout_oom();
            return Step::Error;
        }
        let Some(chapter) = self.chapter else {
            return Step::Error;
        };
        let input = &self.services().input;
        let count = input.read();
        if count == 0 && input.available() > 0 {
            log::message(
                self.services().leaves,
                1,
                b"EHP",
                format_args!("File read error"),
            );
            return Step::Error;
        }
        let done = input.available() == 0;
        // Copy the pointer only. XML accepts/copies input before an entity callback
        // can replace the service-owned buffer; no slice spans event dispatch.
        let bytes = input.bytes();
        let result =
            unsafe { chapter::feed_diagnostic(chapter.as_ptr(), bytes, count, done.into()) };
        if result as u32 >> 8 != 0 {
            diagnostics::parse_error(self.services().leaves, result);
        } else if result as u8 == 2 && self.oom() {
            self.layout_oom();
        }
        match result as u8 {
            0 => Step::More,
            1 => Step::Done,
            _ => Step::Error,
        }
    }

    pub(super) fn finish(&mut self) -> bool {
        if self.services().stopped.get() {
            return false;
        }
        if self.oom() {
            self.layout_oom();
            return false;
        }
        if self.chapter.is_some() {
            let services = self.services();
            log::message(
                services.leaves,
                0,
                b"EHP",
                format_args!(
                    "Time to parse and build pages: {} ms",
                    unsafe { (services.now)() }.wrapping_sub(services.started.get())
                ),
            );
        }
        self.services().input.release_buffer();
        self.services().input.close();
        let success = self
            .chapter
            .is_some_and(|chapter| unsafe { Chapter::finish(chapter) });
        if !self.services().stopped.get() && self.oom() {
            self.layout_oom();
        }
        self.destroy_chapter();
        success
    }

    fn destroy_chapter(&mut self) {
        if let Some(chapter) = self.chapter.take() {
            unsafe { Chapter::destroy(chapter) };
        }
    }
    pub(super) fn abort(&mut self) {
        self.destroy_chapter();
        self.services().input.release_buffer();
        self.services().input.close();
    }
}
impl Drop for BuildOwner {
    fn drop(&mut self) {
        self.abort();
        unsafe { BuildServices::destroy(self.services) };
    }
}

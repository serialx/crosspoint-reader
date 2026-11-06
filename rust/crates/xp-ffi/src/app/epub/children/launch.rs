use super::{
    menu::{Flags, Items},
    owned::Fixed,
    owned::{Chapters, Clippings, Menu, Percent, Qr, ReaderChild},
};
use crate::app::{
    epub::{abi, state::ChildIntent, EpubSession},
    resources::Text,
    stable_owner::StableOwner,
    Owner,
};
use core::{fmt::Write, ptr::NonNull};
use xp_core::i18n::ids;
use xp_ui::app::Transition;

impl EpubSession {
    fn push_owned_child(&self, child: Option<ReaderChild>, intent: ChildIntent) {
        let Some(child) = child else {
            self.request_update();
            return;
        };
        let app = crate::app::app().expect("application initialized");
        self.model(|state| state.child = intent);
        app.input.borrow_mut().reset();
        app.navigation.queue(Transition::Push(Owner::ReaderChild {
            child,
            parent: NonNull::from(self),
        }));
    }

    pub(in crate::app::epub) fn launch_owned_menu(&self, args: &abi::XpEpubMenuArgs) {
        let owner = (|| {
            let title = Text::copy(unsafe { args.title.bytes() }?).ok()?;
            let mut current = Fixed::<16>::new();
            let mut total = Fixed::<16>::new();
            let mut percent = Fixed::<16>::new();
            write!(current, "{}", args.display_page).ok()?;
            write!(total, "{}", args.pages).ok()?;
            write!(percent, "{}", args.percent).ok()?;
            let translated =
                |id| unsafe { (self.resources.translate)(id).bytes().unwrap_or_default() };
            let mut progress = Text::default();
            let chapter = args.pages > 0;
            progress
                .assign_parts(&[
                    if chapter {
                        translated(ids::STR_CHAPTER_PREFIX)
                    } else {
                        b""
                    },
                    if chapter {
                        unsafe { current.wire().bytes() }?
                    } else {
                        b""
                    },
                    if chapter { b"/" } else { b"" },
                    if chapter {
                        unsafe { total.wire().bytes() }?
                    } else {
                        b""
                    },
                    if chapter {
                        translated(ids::STR_PAGES_SEPARATOR)
                    } else {
                        b""
                    },
                    translated(ids::STR_BOOK_PREFIX),
                    unsafe { percent.wire().bytes() }?,
                    b"%",
                ])
                .ok()?;
            let mut input = core::mem::MaybeUninit::uninit();
            unsafe { (self.leaves.input_facts)(input.as_mut_ptr()) };
            let input = unsafe { input.assume_init() };
            let items = Items::new(Flags {
                footnotes: args.footnotes != 0,
                bookmarks: args.bookmarks != 0,
                clippings: args.clippings != 0,
                frontlight: unsafe { (self.runtime.frontlight_present)() } != 0
                    && input.slide_owns_light == 0,
                bluetooth: self.radio_status().compiled_in != 0,
                dictionary: self.settings(false).dictionary_available != 0,
            });
            Menu::try_new(self.runtime, items, title, progress, args.orientation)
        })();
        self.push_owned_child(
            owner.map(StableOwner::from).map(ReaderChild::Menu),
            ChildIntent::Menu,
        );
    }

    pub(in crate::app::epub) fn launch_owned_chapters(&self, selected: i32) {
        self.push_owned_child(
            Chapters::try_new(self.runtime, selected)
                .map(StableOwner::from)
                .map(ReaderChild::Chapters),
            ChildIntent::Chapter,
        );
    }
    // Called when the native clipping owner is retired at the switch.
    #[allow(dead_code)]
    pub(in crate::app::epub) fn launch_owned_clippings(&self) {
        self.push_owned_child(
            Clippings::try_new(self.runtime)
                .map(StableOwner::from)
                .map(ReaderChild::Clippings),
            ChildIntent::Clippings,
        );
    }

    pub(in crate::app::epub) fn launch_owned_percent(&self, value: i32) {
        self.push_owned_child(
            Percent::try_new(self.runtime, value)
                .map(StableOwner::from)
                .map(ReaderChild::Percent),
            ChildIntent::Percent,
        );
    }
    pub(in crate::app::epub) fn launch_owned_qr(&self, payload: Text) {
        self.push_owned_child(
            Qr::try_new(self.runtime, payload)
                .map(StableOwner::from)
                .map(ReaderChild::Qr),
            ChildIntent::Qr,
        );
    }
}

impl EpubSession {
    pub(super) fn push_definition(&self, viewer: super::definition::Definition) {
        let app = crate::app::app().expect("application initialized");
        app.input.borrow_mut().reset();
        app.navigation
            .queue(Transition::Push(Owner::Definition(viewer)));
    }

    // Lane B replaces its temporary factory with this entry at integration.
    #[allow(dead_code)]
    pub(in crate::app) unsafe fn launch_plain_definition(
        &self,
        title: crate::app::abi::Text,
        length: u32,
        fill: super::selection_physical::DefinitionFill,
        context: *mut core::ffi::c_void,
    ) -> bool {
        let Some(viewer) = (unsafe {
            super::definition::Definition::try_plain(
                &self.leaves.child_selection_physical,
                title,
                length,
                fill,
                context,
            )
        }) else {
            return false;
        };
        self.push_definition(viewer);
        true
    }
}

impl EpubSession {
    pub(in crate::app::epub) fn launch_owned_footnote(
        &self,
        page: super::super::page_owner::Page,
        margins: [i32; 2],
        reopen_menu: bool,
    ) -> bool {
        let Some(child) = super::selection_owned::Footnote::try_new(self.runtime, page, margins)
        else {
            return false;
        };
        self.push_owned_child(
            Some(ReaderChild::Footnote(child)),
            ChildIntent::Footnote { reopen_menu },
        );
        true
    }
    pub(in crate::app::epub) fn launch_owned_lookup(
        &self,
        page: super::super::page_owner::Page,
        margins: [i32; 2],
        initial: crate::app::abi::Text,
    ) -> bool {
        let Some(child) = super::selection_owned::Lookup::try_new(
            self.runtime,
            page,
            margins,
            self.settings(true).font,
            &self.leaves.child_selection_physical,
            initial,
        ) else {
            return false;
        };
        self.push_owned_child(Some(ReaderChild::Lookup(child)), ChildIntent::Dictionary);
        true
    }
}

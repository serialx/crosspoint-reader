use super::{
    menu::{Flags, Items},
    owned::Fixed,
    owned::{Chapters, Menu, Percent, Qr, ReaderChild},
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
    pub(in crate::app::epub) fn children_enabled(&self) -> bool {
        crate::app::app().is_some_and(|app| app.child_drawing.is_some())
    }

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

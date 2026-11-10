// The fixture's stand-in for the production `app::epub` module. The reader
// children mount here, at their production depth, so `super::` and
// `super::super::` in the production files resolve as they do there.

// This file lives outside the `epub` module directory, so name its path.
#[path = "../children.rs"]
pub(crate) mod children;

pub mod abi {
    pub enum XpEpubLeaves {}
    #[repr(C)]
    pub struct XpEpubHeap {
        pub free_bytes: u32,
        pub largest_block: u32,
    }
}
// This crate exercises the four O4a children. Real Page/selection owners run
// in reader_selection_ownership, which compiles the complete App instead.
pub mod page_owner {
    pub(crate) struct Page;
    impl Drop for Page {
        fn drop(&mut self) {}
    }
    impl Page {
        pub(crate) fn view(&self) -> xp_core::page::Page<'_> {
            panic!("selection requires the real App composition")
        }
    }
}
// The real `render_resources` pulls the page_render/display_source graph this
// crate does not compile. `selection_owned` stores only its two-word FontScan.
pub(crate) mod render_resources {
    #[repr(C)]
    pub(crate) struct FontScan {
        pub(crate) owner: *const core::ffi::c_void,
        pub(crate) active: bool,
    }
    impl FontScan {
        pub(crate) const EMPTY: Self = Self {
            owner: core::ptr::null(),
            active: false,
        };
    }
    const _: () = assert!(core::mem::size_of::<FontScan>() == 2 * core::mem::size_of::<usize>());
}

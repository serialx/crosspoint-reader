extern crate alloc;

pub(super) mod abi;
mod base;
mod book;
// The new owner stays dormant until the atomic reader switch.
#[allow(dead_code)]
mod book_owner;
mod book_render;
mod bookmarks;
mod build;
mod child;
mod child_result;
mod clipping;
mod guarded;
mod idle;
mod lifetime;
mod links;
mod loop_input;
mod loop_maintenance;
mod loop_owner;
mod menu;
mod menu_more;
mod navigation;
mod overlay;
mod overlay_actions;
mod overlay_input;
mod overlay_view;
mod ownership;
mod page;
mod position;
mod projection;
mod read_folder;
mod recovery;
mod rejected_input;
mod render;
mod render_scope;
mod selection_metadata;
mod session_book;
mod settings;
mod state;
mod storage;
mod sync;
#[cfg(feature = "app-test")]
mod test_support;
mod text;
mod view_values;

use super::{abi as app_abi, resources::Text};
use alloc::{alloc::alloc, boxed::Box};
use core::{alloc::Layout, cell::UnsafeCell, ffi::c_void, marker::PhantomData, ptr::NonNull};
use guarded::GuardedCell;
use state::{SectionFacts, State};

pub(super) struct EpubSession {
    path: UnsafeCell<Text>,
    #[cfg(feature = "app-test")]
    book_factory: session_book::Factory,
    #[cfg(feature = "app-test")]
    legacy_fixture: UnsafeCell<core::mem::MaybeUninit<session_book::Loaded>>,
    state: UnsafeCell<State>,
    guarded: GuardedCell,
    leaves: &'static abi::XpEpubLeaves,
    runtime: &'static app_abi::Runtime,
    resources: &'static app_abi::Resources,
    #[cfg(feature = "app-test")]
    estimate_observer: Option<test_support::EstimateObserver>,
    #[cfg(feature = "app-test")]
    building_observer: Option<test_support::BuildingObserver>,
    #[cfg(feature = "app-test")]
    deferred_override: Option<test_support::DeferredOverride>,
    #[cfg(feature = "app-test")]
    lifetime_overrides: Option<test_support::LifetimeOverrides>,
    #[cfg(feature = "app-test")]
    bookmark_overrides: Option<test_support::BookmarkOverrides>,
    #[cfg(feature = "app-test")]
    settings_overrides: Option<test_support::settings::Overrides>,
    #[cfg(feature = "app-test")]
    projection_hooks: Option<test_support::ProjectionHooks>,
    #[cfg(feature = "app-test")]
    navigation_hooks: Option<test_support::NavigationHooks>,
    #[cfg(feature = "app-test")]
    base_hooks: Option<test_support::BaseHooks>,
    #[cfg(feature = "app-test")]
    clipping_hooks: Option<test_support::ClippingHooks>,
    #[cfg(feature = "app-test")]
    view_value_hooks: Option<test_support::ViewValueHooks>,
    #[cfg(feature = "app-test")]
    render_hooks: Option<test_support::RenderHooks>,
    #[cfg(feature = "app-test")]
    overlay_view_hooks: Option<test_support::OverlayViewHooks>,
    #[cfg(feature = "app-test")]
    overlay_hooks: Option<test_support::OverlayHooks>,
    #[cfg(feature = "app-test")]
    book_render_hooks: Option<test_support::BookRenderHooks>,
    #[cfg(feature = "app-test")]
    child_hooks: Option<test_support::ChildHooks>,
    #[cfg(feature = "app-test")]
    menu_hooks: Option<test_support::MenuHooks>,
    #[cfg(feature = "app-test")]
    overlay_input_hooks: Option<test_support::OverlayInputHooks>,
    #[cfg(feature = "app-test")]
    loop_hooks: Option<test_support::LoopHooks>,
}

// This capability cannot move to another task. Native render borrows its lock;
// loop operations own a token and release it when this scope ends.
struct ResourceAccess<'a> {
    owner: &'a EpubSession,
    token: Option<NonNull<c_void>>,
    task: PhantomData<*mut ()>,
}

impl Drop for ResourceAccess<'_> {
    fn drop(&mut self) {
        if let Some(token) = self.token.take() {
            unsafe { (self.owner.runtime.render_unlock)(token.as_ptr()) };
        }
    }
}

impl EpubSession {
    pub(super) fn try_new(
        path: &[u8],
        fast_initial: bool,
        frequency: i32,
        leaves: &'static abi::XpEpubLeaves,
        runtime: &'static app_abi::Runtime,
        resources: &'static app_abi::Resources,
    ) -> Option<Box<Self>> {
        let path = Text::copy(path).ok()?;
        let owner = NonNull::new(unsafe { alloc(Layout::new::<Self>()) }.cast::<Self>())?;
        // Construction runs on the loop task, before publishing this allocation.
        unsafe {
            let ptr = owner.as_ptr();
            core::ptr::addr_of_mut!((*ptr).path).write(UnsafeCell::new(path));
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).book_factory).write(session_book::Factory::Reader);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).legacy_fixture)
                .write(UnsafeCell::new(core::mem::MaybeUninit::uninit()));
            core::ptr::addr_of_mut!((*ptr).state)
                .write(UnsafeCell::new(State::new(fast_initial, frequency)));
            core::ptr::addr_of_mut!((*ptr).guarded).write(GuardedCell::default());
            core::ptr::addr_of_mut!((*ptr).leaves).write(leaves);
            core::ptr::addr_of_mut!((*ptr).runtime).write(runtime);
            core::ptr::addr_of_mut!((*ptr).resources).write(resources);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).estimate_observer).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).building_observer).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).deferred_override).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).lifetime_overrides).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).bookmark_overrides).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).settings_overrides).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).projection_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).navigation_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).base_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).clipping_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).view_value_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).render_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).overlay_view_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).overlay_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).book_render_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).child_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).menu_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).overlay_input_hooks).write(None);
            #[cfg(feature = "app-test")]
            core::ptr::addr_of_mut!((*ptr).loop_hooks).write(None);
            Some(Box::from_raw(ptr))
        }
    }

    // Only bounded scalar work is allowed here. R cannot borrow from State.
    fn model<R>(&self, update: impl FnOnce(&mut State) -> R) -> R {
        struct Unlock(&'static app_abi::Runtime);
        impl Drop for Unlock {
            fn drop(&mut self) {
                unsafe { (self.0.model_unlock)() };
            }
        }
        unsafe { (self.runtime.model_lock)() };
        let _unlock = Unlock(self.runtime);
        update(unsafe { &mut *self.state.get() })
    }

    fn lock_resources(&self) -> ResourceAccess<'_> {
        let token = NonNull::new(unsafe { (self.runtime.render_lock)() })
            .expect("reader resource lock failed");
        ResourceAccess {
            owner: self,
            token: Some(token),
            task: PhantomData,
        }
    }

    fn try_resources(&self) -> Option<ResourceAccess<'_>> {
        let token = NonNull::new(unsafe { (self.runtime.render_try_lock)() })?;
        Some(ResourceAccess {
            owner: self,
            token: Some(token),
            task: PhantomData,
        })
    }

    // The app render/exit caller holds the native lock until this scope ends.
    unsafe fn borrowed_resources(&self) -> ResourceAccess<'_> {
        ResourceAccess {
            owner: self,
            token: None,
            task: PhantomData,
        }
    }

    fn entry_resources(&self) -> ResourceAccess<'_> {
        assert!(!self.model(|state| state.ready));
        // Render returns before resources until ready, so entry has sole access.
        ResourceAccess {
            owner: self,
            token: None,
            task: PhantomData,
        }
    }

    fn check_resources(&self, access: &ResourceAccess<'_>) {
        assert!(core::ptr::eq(self, access.owner));
    }

    // Entry alone owns the unpublished model; do not acquire a runtime lock here.
    unsafe fn initialize_refresh_counter(&self, frequency: i32) {
        (*self.state.get()).base.refresh_counter = frequency.max(2);
    }

    // Call after each Section mutation, before releasing the resource lock.
    // Loop readers only receive these values, never Section's build pointers.
    fn publish_section(&self, access: &ResourceAccess<'_>) {
        let facts = self.section(access).map(|section| unsafe {
            let section = section.as_ptr();
            SectionFacts {
                pages: (self.leaves.section_page_count)(section),
                estimated_pages: (self.leaves.section_estimated_pages)(section),
                building: (self.leaves.section_building)(section) != 0,
                #[cfg(feature = "app-test")]
                complete: (self.leaves.section_complete)(section) != 0,
                #[cfg(feature = "app-test")]
                partial: (self.leaves.section_partial)(section) != 0,
            }
        });
        self.model(|state| state.facts.section = facts);
    }

    fn drop_section(&self, access: &ResourceAccess<'_>) {
        let section = self.take_section(access);
        self.model(|state| state.facts.section = None);
        if let Some(section) = section {
            unsafe { (self.leaves.section_close)(section.as_ptr()) };
        }
    }
}

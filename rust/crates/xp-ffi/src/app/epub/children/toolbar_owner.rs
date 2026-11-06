#[cfg(feature = "app-test")]
use core::ffi::c_void;
extern crate alloc;

use super::{owned::Toolbar, toolbar_view::Content};
use crate::app::epub::{abi, state::Overlay, EpubSession, ResourceAccess};
use crate::app::stable_owner::StableOwner;
use core::{ffi::CStr, mem::MaybeUninit, ptr::NonNull};
use xp_ui::{
    model::{Input, TOUCH_RELEASED},
    reader_chrome::toolbar::Contact,
};

pub(in crate::app::epub) enum ToolbarSlot {
    #[cfg(feature = "app-test")]
    Legacy(NonNull<c_void>),
    Owned(StableOwner<Toolbar>),
}
#[derive(Clone, Copy)]
pub(in crate::app::epub) enum ToolbarTarget {
    #[cfg(feature = "app-test")]
    Legacy(NonNull<c_void>),
    Owned(NonNull<Toolbar>),
}
impl ToolbarSlot {
    pub(in crate::app::epub) fn open(parent: &EpubSession) -> Option<Self> {
        #[cfg(feature = "app-test")]
        if let Some(legacy) = parent.legacy_children.get() {
            return NonNull::new(unsafe { (legacy.toolbar_open)() }).map(Self::Legacy);
        }
        Toolbar::try_new(parent.runtime).map(|owner| Self::Owned(owner.into()))
    }

    pub(in crate::app::epub) fn target(&self) -> ToolbarTarget {
        match self {
            #[cfg(feature = "app-test")]
            Self::Legacy(p) => ToolbarTarget::Legacy(*p),
            Self::Owned(owner) => ToolbarTarget::Owned(owner.pointer()),
        }
    }
    pub(in crate::app::epub) fn close(self, _parent: &EpubSession) {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(p) = self {
            unsafe {
                (_parent
                    .legacy_children
                    .get()
                    .expect("legacy toolbar fixture")
                    .toolbar_close)(p.as_ptr())
            };
        }
    }
}
impl ToolbarTarget {
    pub(in crate::app::epub) fn begin(self, _parent: &EpubSession) {
        match self {
            #[cfg(feature = "app-test")]
            Self::Legacy(p) => unsafe {
                (_parent
                    .legacy_children
                    .get()
                    .expect("legacy toolbar fixture")
                    .toolbar_begin)(p.as_ptr())
            },
            Self::Owned(p) => unsafe { p.as_ref() }.model.with(|model| {
                model.ready = false;
                model.contact = Contact::default();
                xp_ui::reader::input::toolbar_reset(&mut model.navigation);
            }),
        }
    }
    pub(in crate::app::epub) fn reset(self, _parent: &EpubSession, selected: i32, top: i32) {
        match self {
            #[cfg(feature = "app-test")]
            Self::Legacy(p) => unsafe {
                (_parent
                    .legacy_children
                    .get()
                    .expect("legacy toolbar fixture")
                    .toolbar_reset_navigation)(p.as_ptr(), selected, top)
            },
            Self::Owned(p) => unsafe { p.as_ref() }.reset_navigation(selected, top),
        }
    }
    pub(in crate::app::epub) fn scroll(
        self,
        _parent: &EpubSession,
        delta: i32,
        count: i32,
    ) -> bool {
        match self {
            #[cfg(feature = "app-test")]
            Self::Legacy(p) => unsafe {
                (_parent
                    .legacy_children
                    .get()
                    .expect("legacy toolbar fixture")
                    .toolbar_scroll)(p.as_ptr(), delta, count)
                    != 0
            },
            Self::Owned(p) => unsafe { p.as_ref() }.scroll(delta, count),
        }
    }
    pub(in crate::app::epub) fn visible_rows(self, _parent: &EpubSession) -> i32 {
        match self {
            #[cfg(feature = "app-test")]
            Self::Legacy(p) => unsafe {
                (_parent
                    .legacy_children
                    .get()
                    .expect("legacy toolbar fixture")
                    .toolbar_visible_rows)(p.as_ptr())
            },
            Self::Owned(p) => unsafe { p.as_ref() }.visible_rows(),
        }
    }
    pub(in crate::app::epub) fn route(self, parent: &EpubSession) -> abi::XpEpubRouted {
        match self {
            #[cfg(feature = "app-test")]
            Self::Legacy(p) => {
                let mut out = MaybeUninit::uninit();
                unsafe {
                    (parent
                        .legacy_children
                        .get()
                        .expect("legacy toolbar fixture")
                        .toolbar_route)(p.as_ptr(), out.as_mut_ptr());
                    out.assume_init()
                }
            }
            Self::Owned(p) => unsafe { p.as_ref() }.route(parent.runtime, || {
                let mut input = MaybeUninit::<Input>::uninit();
                unsafe {
                    (parent.leaves.input_snapshot)(input.as_mut_ptr());
                    input.assume_init()
                }
            }),
        }
    }
}
impl Toolbar {
    pub(super) fn route(
        &self,
        runtime: &crate::app::abi::Runtime,
        sample: impl FnOnce() -> Input,
    ) -> abi::XpEpubRouted {
        let empty = abi::XpEpubRouted {
            value: 0,
            permille: -1,
            x: 0,
            y: 0,
            event: 0,
            routed: 0,
        };
        if !self.model.with(|model| model.ready) {
            return empty;
        }
        let input = sample();
        let mut permille = -1;
        let effect = self.model.with(|model| {
            if !model.ready {
                return Default::default();
            }
            model.contact.route(&model.hits, input, &mut permille)
        });
        if effect.kind != 0 && input.touch_flags & TOUCH_RELEASED != 0 {
            unsafe { (runtime.touch_feedback)(0) };
        }
        abi::XpEpubRouted {
            value: effect.value,
            permille,
            x: i32::from(input.touch_x),
            y: i32::from(input.touch_y),
            event: effect.kind,
            routed: effect.invalidate,
        }
    }
}
impl EpubSession {
    pub(in crate::app::epub) unsafe fn paint_toolbar_target(
        &self,
        access: &ResourceAccess<'_>,
        toolbar: NonNull<Toolbar>,
        mode: Overlay,
        model: &abi::XpEpubToolbarModel,
    ) -> bool {
        let text = |wire: crate::app::abi::Text| {
            if wire.data.is_null() {
                None
            } else {
                Some(unsafe { CStr::from_ptr(wire.data.cast()) })
            }
        };
        let content = Content {
            panel: model.panel != 0,
            dense: model.dense != 0,
            active_tool: model.active_tool,
            progress_permille: model.progress_permille,
            chapter: text(model.chapter),
            page_info: text(model.page_info),
            panel_title: text(model.panel_title),
            count: model.item_count,
            selected: model.selected,
        };
        let physical = crate::app::app()
            .and_then(|app| app.child_drawing)
            .expect("child drawing installed");
        unsafe { self.paint_owned_toolbar(access, toolbar.as_ref(), physical, mode, &content) }
            .is_ok()
    }
}

#[cfg(test)]
pub(in crate::app::epub::children) mod tests;

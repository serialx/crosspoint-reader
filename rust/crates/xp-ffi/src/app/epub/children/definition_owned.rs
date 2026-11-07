extern crate alloc;
use super::{
    definition_physical::*,
    definition_text::OwnedText,
    selection_physical::{DefinitionFill, LookupStorage},
};
use crate::app::{abi, stable_owner::StableOwner, sync::ModelCell};
use alloc::{alloc::alloc, boxed::Box};
use core::{
    alloc::Layout as Allocation,
    cell::UnsafeCell,
    ffi::c_void,
    mem::MaybeUninit,
    ptr::{self, NonNull},
};
use xp_ui::{
    dictionary::definition::{Layout, State},
    runtime::SceneGate,
    scene::Command,
};

#[repr(u8)]
#[derive(Clone, Copy)]
pub(super) enum PayloadSlot {
    Current = 0,
    Candidate = 1,
}

#[repr(C)]
#[derive(Default)]
pub(super) struct HistoryEntry {
    pub query: OwnedText,
    pub page: i32,
    pub selection_page: i32,
    pub selection: SelectionPosition,
    pub styled: bool,
}
#[repr(C)]
pub(super) struct Navigation {
    pub query: OwnedText,
    pub pending: OwnedText,
    pub history: [HistoryEntry; 16],
    pub selection_page: i32,
    pub selection: SelectionPosition,
}
#[repr(C)]
pub(super) struct Publication {
    pub state: State,
    pub gate: SceneGate,
    pub ready: bool,
    pub discard_pending: bool,
    pub styled: bool,
}
#[repr(C)]
pub(super) struct Frame {
    pub commands: [Command; 4],
    pub state: State,
    pub layout: Layout,
    pub prewarm: [usize; 2],
    pub prewarm_active: bool,
}
#[repr(C)]
pub(crate) struct DefinitionSession {
    pub(super) model: ModelCell<Publication>,
    pub(super) frame: UnsafeCell<Frame>,
    pub(super) navigation: UnsafeCell<Navigation>,
    pub(super) payload: UnsafeCell<PayloadStorage>,
    pub(super) lookup: *mut LookupStorage,
    pub(super) physical: &'static DefinitionPhysical,
    initialized: UnsafeCell<bool>,
    pub(super) html: UnsafeCell<bool>,
    pub(in crate::app) result: UnsafeCell<Option<bool>>,
}
pub(crate) struct DefinitionOwner(StableOwner<DefinitionSession>);
impl core::ops::Deref for DefinitionOwner {
    type Target = DefinitionSession;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
impl DefinitionOwner {
    fn allocate(
        runtime: &'static abi::Runtime,
        physical: &'static DefinitionPhysical,
        lookup: *mut LookupStorage,
    ) -> Option<Self> {
        // Initialize directly in the sole fallible allocation, before native interior loans exist.
        let out = NonNull::new(
            unsafe { alloc(Allocation::new::<DefinitionSession>()) }.cast::<DefinitionSession>(),
        )?;
        unsafe {
            let out = out.as_ptr();
            ptr::addr_of_mut!((*out).model).write(ModelCell::new(
                Publication {
                    state: State {
                        pages: 1,
                        lookup: u8::from(!lookup.is_null()),
                        ..State::default()
                    },
                    gate: SceneGate::default(),
                    ready: false,
                    discard_pending: false,
                    styled: false,
                },
                runtime,
            ));
            let frame = ptr::addr_of_mut!((*out).frame).cast::<Frame>();
            for i in 0..4 {
                ptr::addr_of_mut!((*frame).commands)
                    .cast::<Command>()
                    .add(i)
                    .write(Command::default());
            }
            ptr::addr_of_mut!((*frame).state).write(State::default());
            ptr::addr_of_mut!((*frame).layout).write(Layout::default());
            ptr::addr_of_mut!((*frame).prewarm).write([0; 2]);
            ptr::addr_of_mut!((*frame).prewarm_active).write(false);
            let nav = ptr::addr_of_mut!((*out).navigation).cast::<Navigation>();
            ptr::addr_of_mut!((*nav).query).write(OwnedText::default());
            ptr::addr_of_mut!((*nav).pending).write(OwnedText::default());
            for i in 0..16 {
                ptr::addr_of_mut!((*nav).history)
                    .cast::<HistoryEntry>()
                    .add(i)
                    .write(HistoryEntry::default());
            }
            ptr::addr_of_mut!((*nav).selection_page).write(0);
            ptr::addr_of_mut!((*nav).selection).write(SelectionPosition::default());
            ptr::addr_of_mut!((*out).payload).write_bytes(0, 1);
            ptr::addr_of_mut!((*out).lookup).write(lookup);
            ptr::addr_of_mut!((*out).physical).write(physical);
            ptr::addr_of_mut!((*out).initialized).write(UnsafeCell::new(false));
            ptr::addr_of_mut!((*out).html).write(UnsafeCell::new(false));
            ptr::addr_of_mut!((*out).result).write(UnsafeCell::new(None));
            Some(Self(StableOwner::from(Box::from_raw(out))))
        }
    }
    pub(crate) fn pointer(&self) -> NonNull<DefinitionSession> {
        self.0.pointer()
    }
    pub(crate) unsafe fn try_plain(
        runtime: &'static abi::Runtime,
        physical: &'static DefinitionPhysical,
        title: abi::Text,
        length: u32,
        fill: DefinitionFill,
        context: *mut c_void,
    ) -> Option<Self> {
        let owner = Self::allocate(runtime, physical, ptr::null_mut())?;
        if unsafe { (physical.payload_plain)(owner.payload.get(), title, length, fill, context) }
            == 0
        {
            return None;
        }
        unsafe {
            *owner.initialized.get() = true;
        }
        Some(owner)
    }
    pub(crate) unsafe fn try_native(
        runtime: &'static abi::Runtime,
        physical: &'static DefinitionPhysical,
        transfer: *mut NativeTransfer,
    ) -> Option<Self> {
        let owner = Self::allocate(runtime, physical, ptr::null_mut())?;
        let mut html = 0;
        if unsafe { (physical.payload_native)(owner.payload.get(), transfer, &mut html) } == 0 {
            return None;
        }
        unsafe {
            *owner.initialized.get() = true;
            *owner.html.get() = html != 0;
        }
        Some(owner)
    }
    pub(crate) unsafe fn try_lookup(
        runtime: &'static abi::Runtime,
        physical: &'static DefinitionPhysical,
        lookup: *mut LookupStorage,
    ) -> Option<Self> {
        let owner = Self::allocate(runtime, physical, lookup)?;
        unsafe extern "C" fn query(context: *mut c_void, value: abi::Text) -> u8 {
            let query = unsafe { &mut *context.cast::<OwnedText>() };
            let bytes = if value.len == 0 {
                &[]
            } else {
                unsafe { core::slice::from_raw_parts(value.data, value.len) }
            };
            u8::from(query.assign(bytes))
        }
        let query_context = unsafe { ptr::addr_of_mut!((*owner.navigation.get()).query) }.cast();
        let mut html = 0;
        if unsafe {
            (physical.payload_lookup)(owner.payload.get(), lookup, query, query_context, &mut html)
        } == 0
        {
            return None;
        }
        unsafe {
            *owner.initialized.get() = true;
            *owner.html.get() = html != 0;
        }
        Some(owner)
    }
}
impl Drop for DefinitionOwner {
    fn drop(&mut self) {
        // Destroy native interior pointers before StableOwner reconstructs and retags the Box.
        if unsafe { *self.initialized.get() } {
            unsafe {
                (self.physical.payload_drop)(self.payload.get());
                *self.initialized.get() = false;
            }
        }
    }
}
impl DefinitionSession {
    pub(super) unsafe fn body(&self) -> xp_ui::scene::Rect {
        unsafe { self.physical.body() }
    }
    pub(super) unsafe fn prepare(
        &self,
        slot: PayloadSlot,
        force_plain: bool,
    ) -> Option<PayloadView> {
        let mut out = MaybeUninit::<PayloadView>::uninit();
        if unsafe {
            (self.physical.payload_prepare)(
                self.payload.get(),
                slot as u8,
                u8::from(*self.html.get()),
                u8::from(force_plain),
                self.body(),
                out.as_mut_ptr(),
            )
        } == 0
        {
            return None;
        }
        Some(unsafe { out.assume_init() })
    }
    pub(super) fn publish_payload(&self, view: PayloadView, page: i32) {
        self.model.with(|model| {
            model.state.pages = view.pages;
            model.state.page = page;
            model.styled = view.styled != 0;
        });
    }
}
#[cfg(target_pointer_width = "32")]
const _: () = {
    use core::mem::{offset_of, size_of};
    assert!(size_of::<DefinitionSession>() == 1104);
    assert!(size_of::<HistoryEntry>() == 40);
    assert!(size_of::<Publication>() == 32);
    assert!(size_of::<Frame>() == 196);
    assert!(offset_of!(DefinitionSession, frame) == 36);
    assert!(offset_of!(DefinitionSession, navigation) == 232);
    assert!(offset_of!(DefinitionSession, payload) == 920);
};

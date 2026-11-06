use super::{
    owned::allocate,
    selection_physical::{DefinitionFill, DefinitionStorage, LookupStorage, Physical},
};
use crate::app::{abi, stable_owner::StableOwner};
use core::{
    cell::UnsafeCell,
    ffi::c_void,
    ptr::{self, NonNull},
};

// The native adapter is at offset zero. It borrows the inline viewer, never owns
// a separately allocated activity. Only definition_drop may destroy this storage.
#[repr(C)]
pub(crate) struct DefinitionData {
    storage: UnsafeCell<DefinitionStorage>,
    destroy: UnsafeCell<Option<unsafe extern "C" fn(*mut DefinitionStorage)>>,
}
pub(crate) struct Definition(StableOwner<DefinitionData>);

impl core::ops::Deref for Definition {
    type Target = DefinitionData;
    fn deref(&self) -> &DefinitionData {
        &self.0
    }
}

impl Definition {
    fn allocate() -> Option<Self> {
        unsafe {
            allocate(|out: *mut DefinitionData| {
                ptr::addr_of_mut!((*out).storage).write_bytes(0, 1);
                ptr::addr_of_mut!((*out).destroy).write(UnsafeCell::new(None));
            })
            .map(StableOwner::from)
            .map(Self)
        }
    }
    fn constructed(owner: Self, physical: &Physical, handle: *mut c_void) -> Option<Self> {
        NonNull::new(handle)?;
        assert_eq!(handle, owner.storage.get().cast());
        unsafe { *owner.destroy.get() = Some(physical.definition_drop) };
        Some(owner)
    }
    /// The fill callback and context are borrowed synchronously; neither escapes.
    pub(crate) unsafe fn try_plain(
        physical: &Physical,
        title: abi::Text,
        length: u32,
        fill: DefinitionFill,
        context: *mut c_void,
    ) -> Option<Self> {
        let owner = Self::allocate()?;
        let handle = unsafe {
            (physical.definition_plain)(owner.storage.get(), title, length, fill, context)
        };
        Self::constructed(owner, physical, handle)
    }
    /// The lookup storage must remain live through this viewer and its descendants.
    pub(super) unsafe fn try_lookup(
        physical: &Physical,
        lookup: *mut LookupStorage,
    ) -> Option<Self> {
        let owner = Self::allocate()?;
        let handle = unsafe { (physical.definition_lookup)(owner.storage.get(), lookup) };
        Self::constructed(owner, physical, handle)
    }
    pub(crate) fn handle(&self) -> NonNull<c_void> {
        debug_assert!(unsafe { (*self.destroy.get()).is_some() });
        NonNull::new(self.storage.get().cast()).expect("inline definition")
    }
}
// Native interior pointers must expire before StableOwner reconstructs the Box.
impl Drop for Definition {
    fn drop(&mut self) {
        if let Some(destroy) = unsafe { (*self.destroy.get()).take() } {
            unsafe { destroy(self.storage.get()) };
        }
    }
}
#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<DefinitionData>() == 448);

pub(crate) struct DefinitionSelectionResult {
    wire: abi::DefinitionSelection,
}
impl Drop for DefinitionSelectionResult {
    fn drop(&mut self) {
        if let Some(release) = self.wire.text.release.take() {
            unsafe { release(self.wire.text.data, self.wire.text.capacity) };
        }
    }
}
impl DefinitionSelectionResult {
    // The leaf consumes on one; on zero this guard retains the allocation.
    pub(crate) unsafe fn deliver(
        mut self,
        parent: NonNull<c_void>,
        accept: unsafe extern "C" fn(*mut c_void, abi::DefinitionSelection) -> u8,
    ) {
        let accepted = unsafe { accept(parent.as_ptr(), ptr::read(&self.wire)) };
        if accepted != 0 {
            self.wire.text.release = None;
        }
    }
}
impl Definition {
    /// The bridge keeps its native result on its own stack. Callbacks cannot escape.
    /// Only raw targets may cross pop/delivery, which can destroy either owner.
    #[inline(never)]
    pub(crate) unsafe fn with_selection_result<P, D>(
        child: NonNull<c_void>,
        bridge: unsafe extern "C" fn(
            *mut c_void,
            abi::DefinitionPop,
            abi::DefinitionSelectionSink,
            *mut c_void,
        ),
        pop: P,
        deliver: D,
    ) where
        P: FnOnce() -> Option<NonNull<c_void>>,
        D: FnOnce(NonNull<c_void>, DefinitionSelectionResult),
    {
        struct Context<P, D> {
            pop: Option<P>,
            deliver: Option<D>,
        }
        unsafe extern "C" fn release<P, D>(context: *mut c_void) -> *mut c_void
        where
            P: FnOnce() -> Option<NonNull<c_void>>,
        {
            let pop = unsafe { (*context.cast::<Context<P, D>>()).pop.take() }
                .expect("Definition pop called twice");
            pop().map_or(ptr::null_mut(), NonNull::as_ptr)
        }
        unsafe extern "C" fn receive<P, D>(
            context: *mut c_void,
            parent: *mut c_void,
            wire: abi::DefinitionSelection,
        ) -> u8
        where
            D: FnOnce(NonNull<c_void>, DefinitionSelectionResult),
        {
            let Some(parent) = NonNull::new(parent) else {
                return 0;
            };
            let deliver = unsafe { (*context.cast::<Context<P, D>>()).deliver.take() }
                .expect("Definition result delivered twice");
            deliver(parent, DefinitionSelectionResult { wire });
            1
        }
        let mut context = Context {
            pop: Some(pop),
            deliver: Some(deliver),
        };
        unsafe {
            bridge(
                child.as_ptr(),
                release::<P, D>,
                receive::<P, D>,
                (&mut context as *mut Context<P, D>).cast(),
            );
        }
        assert!(context.pop.is_none(), "missing Definition pop");
    }
}

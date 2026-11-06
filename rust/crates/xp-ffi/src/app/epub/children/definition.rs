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
    pub(super) unsafe fn try_plain(
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
    pub(super) fn handle(&self) -> NonNull<c_void> {
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

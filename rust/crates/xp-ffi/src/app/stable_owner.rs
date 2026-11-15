extern crate alloc;

use alloc::boxed::Box;
use core::{ops::Deref, ptr::NonNull};

// Sole owner of one existing Box allocation. Moving this handle must not retag
// the payload: App retains targets while moving owners between current/parents.
// Access is shared only; no &mut or ownership escape is provided. Drop restores
// the Box exactly once, after the enclosing owner's lifecycle cleanup.
pub(crate) struct StableOwner<T>(NonNull<T>);

impl<T> From<Box<T>> for StableOwner<T> {
    fn from(owner: Box<T>) -> Self {
        Self(NonNull::new(Box::into_raw(owner)).expect("Box is non-null"))
    }
}
impl<T> StableOwner<T> {
    pub(crate) fn pointer(&self) -> NonNull<T> {
        self.0
    }
}
impl<T> Deref for StableOwner<T> {
    type Target = T;
    fn deref(&self) -> &T {
        // The allocation is live until this sole owner drops; callers get no
        // mutable reference, and references cannot outlive the owner borrow.
        unsafe { self.0.as_ref() }
    }
}
impl<T> Drop for StableOwner<T> {
    fn drop(&mut self) {
        // No other owner can reconstruct the Box. App excludes target users
        // under its render lock before dropping an activity.
        unsafe { drop(Box::from_raw(self.0.as_ptr())) };
    }
}

extern crate alloc;

use alloc::boxed::Box;
use core::cell::UnsafeCell;

/// # Safety
/// Calls must serialize access to one persistent flag and invoke the action once.
/// Unlock must publish the flag before a later caller acquires it.
pub unsafe trait Gate {
    fn with<R>(&self, action: impl FnOnce(&mut bool) -> R) -> R;
}

pub struct Service<S, G: Gate> {
    owner: UnsafeCell<Option<Box<S>>>,
    busy: G,
}

struct Lease<'a, G: Gate>(&'a G);
impl<G: Gate> Drop for Lease<'_, G> {
    fn drop(&mut self) {
        self.0.with(|busy| *busy = false);
    }
}

impl<S, G: Gate> Service<S, G> {
    pub fn new(busy: G) -> Self {
        Self {
            owner: UnsafeCell::new(None),
            busy,
        }
    }

    fn lease(&self) -> Option<Lease<'_, G>> {
        self.busy.with(|busy| {
            if *busy {
                return None;
            }
            *busy = true;
            Some(Lease(&self.busy))
        })
    }

    pub fn install(&self, create: impl FnOnce() -> Option<Box<S>>) -> bool {
        let Some(_lease) = self.lease() else {
            return false;
        };
        let owner = unsafe { &mut *self.owner.get() };
        if owner.is_none() {
            *owner = create();
        }
        owner.is_some()
    }

    pub fn with<R>(&self, action: impl FnOnce(&mut S) -> R) -> Option<R> {
        let _lease = self.lease()?;
        // The lease excludes all other access; no reference escapes the closure.
        let owner = unsafe { &mut *self.owner.get() }.as_deref_mut()?;
        Some(action(owner))
    }
}

extern crate alloc;

use alloc::boxed::Box;
use core::cell::UnsafeCell;

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Phase {
    Idle,
    Shared,
    Exclusive,
}

/// # Safety
/// Calls must serialize access to one persistent phase and invoke the action once.
/// Unlock must publish the phase before a later caller acquires it.
pub unsafe trait Gate {
    fn with<R>(&self, action: impl FnOnce(&mut Phase) -> R) -> R;
}

pub struct Service<S, G: Gate> {
    owner: UnsafeCell<Option<Box<S>>>,
    busy: G,
}

struct Lease<'a, G: Gate>(&'a G);
impl<G: Gate> Drop for Lease<'_, G> {
    fn drop(&mut self) {
        self.0.with(|phase| *phase = Phase::Idle);
    }
}

pub struct Operation<'a, S, G: Gate> {
    service: &'a Service<S, G>,
    _lease: Lease<'a, G>,
}

struct SharedOnDrop<'a, G: Gate>(&'a G);
impl<G: Gate> Drop for SharedOnDrop<'_, G> {
    fn drop(&mut self) {
        self.0.with(|phase| *phase = Phase::Shared);
    }
}

impl<S, G: Gate> Operation<'_, S, G> {
    /// Caller acquires its render guard before entering this exclusive phase.
    pub fn update<R>(&mut self, action: impl FnOnce(&mut S) -> R) -> R {
        self.service.busy.with(|phase| *phase = Phase::Exclusive);
        let _shared = SharedOnDrop(&self.service.busy);
        // The phase transition waits for metadata readers; the lease excludes writers.
        let owner = unsafe { &mut *self.service.owner.get() }
            .as_deref_mut()
            .expect("installed clipping owner");
        action(owner)
    }

    /// Physical I/O may run here while metadata readers share the committed owner.
    pub fn read<R>(&self, action: impl FnOnce(&S) -> R) -> R {
        let owner = unsafe { &*self.service.owner.get() }
            .as_deref()
            .expect("installed clipping owner");
        action(owner)
    }
}

impl<S, G: Gate> Service<S, G> {
    #[cfg(feature = "app-test")]
    pub fn reset_fixture(&self) -> bool {
        let Some(_lease) = self.lease(Phase::Exclusive) else {
            return false;
        };
        unsafe { *self.owner.get() = None };
        true
    }

    pub fn new(busy: G) -> Self {
        Self {
            owner: UnsafeCell::new(None),
            busy,
        }
    }

    fn lease(&self, next: Phase) -> Option<Lease<'_, G>> {
        self.busy.with(|phase| {
            if *phase != Phase::Idle {
                return None;
            }
            *phase = next;
            Some(Lease(&self.busy))
        })
    }

    pub fn install(&self, create: impl FnOnce() -> Option<Box<S>>) -> bool {
        let Some(_lease) = self.lease(Phase::Exclusive) else {
            return false;
        };
        let owner = unsafe { &mut *self.owner.get() };
        if owner.is_none() {
            *owner = create();
        }
        owner.is_some()
    }

    pub fn with<R>(&self, action: impl FnOnce(&mut S) -> R) -> Option<R> {
        Some(self.operation()?.update(action))
    }

    pub fn operation(&self) -> Option<Operation<'_, S, G>> {
        let lease = self.lease(Phase::Shared)?;
        unsafe { &*self.owner.get() }.as_ref()?;
        Some(Operation {
            service: self,
            _lease: lease,
        })
    }

    /// Fixed metadata copies only: no allocation, physical callback or reentry.
    pub fn read<R>(&self, action: impl FnOnce(&S) -> R) -> Option<R> {
        self.busy.with(|phase| {
            if *phase == Phase::Exclusive {
                return None;
            }
            let owner = unsafe { &*self.owner.get() }.as_deref()?;
            Some(action(owner))
        })
    }
}

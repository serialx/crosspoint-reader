#![deny(clippy::type_complexity)]

use core::{alloc::Layout, cell::Cell, ptr};

// This module is compiled only by the standalone production-App test harness.
// Its observer uses fixed TLS storage and never allocates inside the allocator.
#[derive(Clone, Copy)]
struct Ledger {
    watched: Option<Layout>,
    fail_next: bool,
    failures: usize,
    attempts: usize,
    allocations: usize,
    frees: usize,
    live: [*mut u8; 32],
    overflow: bool,
}
impl Ledger {
    const EMPTY: Self = Self {
        watched: None,
        fail_next: false,
        failures: 0,
        attempts: 0,
        allocations: 0,
        frees: 0,
        live: [ptr::null_mut(); 32],
        overflow: false,
    };
}
std::thread_local! { static LEDGER: Cell<Ledger> = const { Cell::new(Ledger::EMPTY) }; }
fn update(f: impl FnOnce(&mut Ledger)) {
    let _ = LEDGER.try_with(|cell| {
        let mut ledger = cell.get();
        f(&mut ledger);
        cell.set(ledger);
    });
}
pub(super) fn denied(layout: Layout) -> bool {
    let mut denied = false;
    update(|l| {
        if l.watched == Some(layout) {
            l.attempts += 1;
        }
        if core::mem::take(&mut l.fail_next) {
            l.failures += 1;
            denied = true;
        }
    });
    denied
}
pub(super) fn allocated(p: *mut u8, layout: Layout) {
    update(|l| {
        if l.watched == Some(layout) && !p.is_null() {
            l.allocations += 1;
            if let Some(slot) = l.live.iter_mut().find(|slot| slot.is_null()) {
                *slot = p;
            } else {
                l.overflow = true;
            }
        }
    });
}
pub(super) fn deallocated(p: *mut u8) {
    update(|l| {
        if let Some(slot) = l.live.iter_mut().find(|slot| **slot == p) {
            *slot = ptr::null_mut();
            l.frees += 1;
        }
    });
}

pub(super) struct Probe;
impl Probe {
    pub(super) fn new(layout: Layout) -> Self {
        LEDGER.with(|cell| {
            assert!(cell.get().watched.is_none());
            cell.set(Ledger {
                watched: Some(layout),
                ..Ledger::EMPTY
            });
        });
        Self
    }
    pub(super) fn fail_next(&self) {
        update(|l| l.fail_next = true);
    }
    pub(super) fn counts(&self) -> (usize, usize, usize, usize, usize) {
        LEDGER.with(|cell| {
            let l = cell.get();
            assert!(!l.overflow && !l.fail_next);
            (
                l.attempts,
                l.allocations,
                l.frees,
                l.live.iter().filter(|p| !p.is_null()).count(),
                l.failures,
            )
        })
    }
}
impl Drop for Probe {
    fn drop(&mut self) {
        LEDGER.with(|cell| {
            let l = cell.replace(Ledger::EMPTY);
            assert!(!l.overflow && !l.fail_next);
            assert!(
                l.live.iter().all(|p| p.is_null()),
                "watched allocation leaked"
            );
        });
    }
}

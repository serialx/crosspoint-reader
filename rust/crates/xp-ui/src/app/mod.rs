//! Application ownership. Platform callbacks run without a navigation borrow.
extern crate alloc;

use alloc::vec::Vec;
use core::cell::{Cell, RefCell};

pub mod input;
pub mod result;

/// A small, stable dispatch target. Copying a target does not transfer ownership.
pub trait Activity {
    type Target: Copy;
    fn target(&self) -> Self::Target;
    fn is_reader(&self) -> bool;
}

pub enum Transition<A> {
    None,
    Push(A),
    Pop,
    Replace(A),
    Sleep(A),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TransitionKind {
    None,
    Push,
    Pop,
    Replace,
    Sleep,
}

impl<A> Transition<A> {
    pub fn kind(&self) -> TransitionKind {
        match self {
            Self::None => TransitionKind::None,
            Self::Push(_) => TransitionKind::Push,
            Self::Pop => TransitionKind::Pop,
            Self::Replace(_) => TransitionKind::Replace,
            Self::Sleep(_) => TransitionKind::Sleep,
        }
    }
}

/// Exit and publication require the guard; enter and result delivery may reenter.
/// A target remains valid until its owner is dropped by this application.
pub trait Services<A: Activity> {
    type RenderGuard;
    type Result;

    fn render_lock(&self) -> Self::RenderGuard;
    fn before_screen_change(&self, reader: bool) -> bool;
    fn publish(&self, target: Option<A::Target>, visit: u32);
    fn clear_header(&self);
    fn enter(&self, target: A::Target);
    fn exit(&self, target: A::Target);
    fn with_result(&self, target: A::Target, consume: impl FnOnce(Self::Result));
    fn deliver_result(&self, parent: A::Target, result: Self::Result);
    fn home(&self) -> Option<A>;
    fn request_update(&self);
    fn missing_pop(&self);
    fn stack_allocation_failed(&self);
    fn prepare_sleep(&self, target: A::Target);
}

struct Navigation<A> {
    current: Option<A>,
    parents: Vec<A>,
}

/// The loop task owns this state. Render uses a separately synchronized target.
/// RefCell guards end before platform calls, including destruction of owners.
pub struct Application<A: Activity> {
    navigation: RefCell<Navigation<A>>,
    pending: RefCell<Transition<A>>,
    visit: Cell<u32>,
}

impl<A: Activity> Default for Application<A> {
    fn default() -> Self {
        Self::new()
    }
}

impl<A: Activity> Application<A> {
    pub fn new() -> Self {
        Self {
            navigation: RefCell::new(Navigation {
                current: None,
                parents: Vec::new(),
            }),
            pending: RefCell::new(Transition::None),
            visit: Cell::new(0),
        }
    }

    pub fn visit(&self) -> u32 {
        self.visit.get()
    }

    pub fn current(&self) -> Option<A::Target> {
        self.navigation.borrow().current.as_ref().map(A::target)
    }

    pub fn parent_count(&self) -> usize {
        self.navigation.borrow().parents.len()
    }

    pub fn has_reader(&self) -> bool {
        let navigation = self.navigation.borrow();
        navigation.current.as_ref().is_some_and(A::is_reader)
            || navigation.parents.iter().any(A::is_reader)
    }

    pub fn pending_kind(&self) -> TransitionKind {
        self.pending.borrow().kind()
    }

    pub fn prepare_sleep(&self, services: &impl Services<A>) {
        let _guard = services.render_lock();
        for index in 0..self.parent_count() {
            let target = self.navigation.borrow().parents[index].target();
            services.prepare_sleep(target);
        }
        if let Some(target) = self.current() {
            services.prepare_sleep(target);
        }
    }

    pub fn queue(&self, transition: Transition<A>) {
        let superseded = self.pending.replace(transition);
        drop(superseded);
    }

    /// Bootstrap entry is synchronous, matching a manager without a current screen.
    pub fn replace(&self, activity: A, services: &impl Services<A>) {
        if self.current().is_some() {
            self.queue(Transition::Replace(activity));
            return;
        }
        let target = activity.target();
        {
            let _guard = services.render_lock();
            self.navigation.borrow_mut().current = Some(activity);
            services.publish(Some(target), self.visit());
        }
        services.enter(target);
    }

    fn next_visit(&self) -> u32 {
        let visit = self.visit.get().wrapping_add(1);
        self.visit.set(visit);
        visit
    }

    fn exit_current(&self, services: &impl Services<A>) {
        if let Some(target) = self.current() {
            services.exit(target);
            let outgoing = self.navigation.borrow_mut().current.take();
            drop(outgoing);
        }
        services.clear_header();
    }

    fn exit_parents(&self, services: &impl Services<A>) {
        loop {
            let target = self.navigation.borrow().parents.last().map(A::target);
            let Some(target) = target else { break };
            services.exit(target);
            let outgoing = self.navigation.borrow_mut().parents.pop();
            drop(outgoing);
        }
    }

    fn pop(&self, services: &impl Services<A>) {
        let guard = services.render_lock();
        let Some(target) = self.current() else {
            services.missing_pop();
            return;
        };
        let visit = self.next_visit();
        services.with_result(target, |result| {
            self.exit_current(services);
            let parent = {
                let mut navigation = self.navigation.borrow_mut();
                navigation.current = navigation.parents.pop();
                navigation.current.as_ref().map(A::target)
            };
            services.publish(parent, visit);
            drop(guard);
            if let Some(parent) = parent {
                services.deliver_result(parent, result);
                if self.pending_kind() == TransitionKind::None {
                    services.request_update();
                }
            } else if let Some(home) = services.home() {
                // With no current screen, native goHome enters immediately at this visit.
                self.replace(home, services);
            }
        });
    }

    fn install(&self, activity: A, push: bool, services: &impl Services<A>) {
        let guard = services.render_lock();
        if push {
            let reserved = self.navigation.borrow_mut().parents.try_reserve(1).is_ok();
            if !reserved {
                services.stack_allocation_failed();
                return;
            }
            {
                let mut navigation = self.navigation.borrow_mut();
                if let Some(parent) = navigation.current.take() {
                    navigation.parents.push(parent);
                }
            }
            services.clear_header();
        } else {
            self.exit_current(services);
            self.exit_parents(services);
        }
        let target = activity.target();
        self.navigation.borrow_mut().current = Some(activity);
        services.publish(Some(target), self.next_visit());
        drop(guard);
        services.enter(target);
    }

    /// Drain callbacks' queued transitions; radio teardown may defer the next step.
    pub fn drain(&self, services: &impl Services<A>) -> bool {
        loop {
            let (kind, reader) = {
                let pending = self.pending.borrow();
                let reader = match &*pending {
                    Transition::Push(a) | Transition::Replace(a) | Transition::Sleep(a) => {
                        a.is_reader()
                    }
                    Transition::None | Transition::Pop => false,
                };
                (pending.kind(), reader)
            };
            if kind == TransitionKind::None {
                return true;
            }
            if kind != TransitionKind::Sleep && !services.before_screen_change(reader) {
                return false;
            }
            let transition = self.pending.replace(Transition::None);
            match transition {
                Transition::None => {}
                Transition::Pop => self.pop(services),
                Transition::Push(activity) => self.install(activity, true, services),
                Transition::Replace(activity) | Transition::Sleep(activity) => {
                    self.install(activity, false, services)
                }
            }
        }
    }
}

#[cfg(test)]
mod tests;

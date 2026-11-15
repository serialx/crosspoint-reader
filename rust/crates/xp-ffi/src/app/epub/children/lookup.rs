use super::{
    definition_owned::DefinitionOwner as Definition, selection_owned::LookupData,
    selection_physical::Physical,
};
use xp_ui::dictionary::popup;

pub(super) enum Attempt {
    Detail(Definition),
    Popup { allocation_failed: bool },
}

impl LookupData {
    /// Called on the loop task. `release_page` must exclude all render loans.
    pub(super) unsafe fn perform(
        &self,
        physical: &'static Physical,
        runtime: &'static crate::app::abi::Runtime,
        now: impl Fn() -> u32,
        release_page: impl FnOnce(),
    ) -> Attempt {
        self.model.with(|model| {
            model.state.pending = 0;
            model.state.message = popup::LOOKING;
            model.state.popup = 1;
        });
        if self.model.with(|model| !model.open_attempted) {
            let mut needs_index = 0;
            let opened =
                unsafe { (physical.lookup_open)(self.native.get(), &mut needs_index) } != 0;
            self.model.with(|model| {
                model.open_attempted = true;
                model.open_ok = opened;
                model.needs_index = needs_index != 0;
            });
        }
        let (mut ok, needs_index) = self.model.with(|model| {
            model.state.message = if model.needs_index {
                popup::INDEXING
            } else {
                popup::LOOKING
            };
            (model.open_ok, model.needs_index)
        });
        // Native paints the busy popup before blocking on dictionary I/O.
        unsafe { (physical.wait_paint)() };
        let mut index_result = 0;
        if ok && needs_index {
            ok = unsafe { (physical.lookup_index)(self.native.get(), &mut index_result) } != 0;
            self.model.with(|model| model.needs_index = !ok);
        }
        let mut lookup_result = 1;
        let found =
            ok && unsafe { (physical.lookup_find)(self.native.get(), &mut lookup_result) } != 0;
        let outcome = if found {
            self.model.with(|model| model.state.popup = 0);
            release_page();
            if let Some(viewer) =
                unsafe { Definition::try_lookup(runtime, &physical.definition, self.native.get()) }
            {
                self.model.with(|model| model.awaiting_detail = true);
                Attempt::Detail(viewer)
            } else {
                self.definition_install_failed(now());
                Attempt::Popup {
                    allocation_failed: true,
                }
            }
        } else {
            let now = now();
            self.model.with(|model| {
                model
                    .state
                    .result(ok, index_result, lookup_result, false, now)
            });
            Attempt::Popup {
                allocation_failed: false,
            }
        };
        // Match the two native attempt-local strings' destruction, including OOM.
        unsafe { (physical.lookup_discard)(self.native.get()) };
        outcome
    }
}

impl LookupData {
    #[inline(never)]
    pub(super) unsafe fn activity_input(
        &self,
        parent: &crate::app::epub::EpubSession,
        now: u32,
    ) -> bool {
        let (pending, failed, expired) = self.model.with(|model| {
            (
                model.state.pending != 0,
                model.failed,
                model.state.expired(now),
            )
        });
        if failed {
            parent.request_update();
        }
        if pending {
            let attempt = unsafe {
                self.perform(
                    &parent.leaves.child_selection_physical,
                    parent.runtime,
                    || (parent.runtime.now_ms)(),
                    || {
                        let _access = parent.lock_resources();
                        self.release_page();
                    },
                )
            };
            match attempt {
                Attempt::Detail(viewer) => parent.push_definition(viewer),
                Attempt::Popup { allocation_failed } => {
                    if allocation_failed {
                        unsafe {
                            (parent.leaves.log)(
                                1,
                                crate::app::abi::Text::from_bytes(b"DICT"),
                                crate::app::abi::Text::from_bytes(b"OOM: definition activity"),
                            )
                        };
                    }
                    parent.request_update();
                }
            }
            return false;
        }
        if expired {
            unsafe {
                *self.result.get() =
                    Some(super::child_result::ReaderChildResult::LookupClosed { cancelled: false })
            };
        }
        expired
    }

    pub(in crate::app) fn definition_install_failed(&self, now: u32) {
        self.model.with(|model| {
            model.awaiting_detail = false;
            model.state.popup = 3;
            model.state.message = popup::LOW_MEMORY;
            model.state.since = now;
        });
    }

    pub(in crate::app) unsafe fn definition_closed(&self, cancelled: bool) {
        self.model.with(|model| model.awaiting_detail = false);
        unsafe {
            *self.result.get() =
                Some(super::child_result::ReaderChildResult::LookupClosed { cancelled })
        };
    }
}

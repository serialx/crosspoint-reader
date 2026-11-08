extern crate std;
use super::*;
use core::mem::{size_of, MaybeUninit};

#[test]
fn in_place_fields_are_valid_before_the_first_borrow() {
    let mut frame = MaybeUninit::<Frame<super::super::wifi_resources::Global>>::uninit();
    unsafe { Frame::initialize(frame.as_mut_ptr()) };
    let mut frame = unsafe { frame.assume_init() };
    assert!(frame.commands.as_slice().is_empty());
    assert!(frame.rows.as_slice().is_empty());
    assert!(frame.text.as_bytes().is_empty());
    frame.text.try_assign(b"prepared").unwrap();
    assert_eq!(frame.text.as_bytes(), b"prepared");
    assert_eq!(frame.state.count, 0);
    assert_eq!(frame.needs_capacity, 0);
    assert!(size_of::<Frame<super::super::wifi_resources::Global>>() > 256);
}
use super::test_host::*;
use core::ffi::CStr;
use std::{vec, vec::Vec};

fn phase(owner: &Wifi<TestHost>) -> u8 {
    owner.model(|m| m.state.phase)
}
fn names(owner: &Wifi<TestHost>) -> Vec<Vec<u8>> {
    let _guard = owner.resource();
    unsafe { (*owner.owned()).rows.as_slice() }
        .iter()
        .map(|r| r.ssid().to_vec())
        .collect()
}
#[test]
fn saved_load_hidden_connect_failure_and_forget_keep_native_storage_in_place() {
    let (owner, f) = rig();
    f.auto.set(true);
    f.data
        .borrow_mut()
        .credentials
        .push((b"1".to_vec(), b"pw".to_vec()));
    owner.enter();
    assert_eq!(phase(&owner), Phase::Scanning as u8);
    assert!(f.last_slot.get().is_null());
    scanned(&owner, &f, vec![]);
    assert!(owner.render());
    key(&f, 0, model::CONFIRM);
    owner.poll();
    owner.keyboard_returned(Some(b"1"));
    key(&f, 0, 0);
    owner.poll();
    assert_eq!(phase(&owner), Phase::Connecting as u8);
    assert!(f.credential_slot.get().is_null());
    f.data.borrow_mut().connection = 1;
    owner.poll();
    assert_eq!(phase(&owner), Phase::Failed as u8);
    assert!(owner.render());
    key(&f, model::BACK, 0);
    owner.poll();
    assert_eq!(phase(&owner), Phase::Forget as u8);
    assert!(owner.render());
    key(&f, model::RIGHT, 0);
    owner.poll();
    key(&f, model::CONFIRM, 0);
    owner.poll();
    assert!(f.data.borrow().credentials.is_empty());
    assert!(f.credential_slot.get().is_null() && f.last_slot.get().is_null());
    owner.exit();
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn scan_dedup_ranking_ties_and_hidden_last_survive_a_stable_owner_move() {
    let (owner, f) = rig();
    f.data
        .borrow_mut()
        .credentials
        .push((b"saved".to_vec(), b"pw".to_vec()));
    owner.enter();
    assert_eq!(phase(&owner), Phase::Scanning as u8);
    scanned(
        &owner,
        &f,
        vec![
            row(b"zeta", -50, true),
            row(b"alpha", -50, false),
            row(b"saved", -90, true),
            row(b"zeta", -30, false),
            row(b"", -1, false),
        ],
    );
    assert_eq!(
        names(&owner),
        vec![
            b"saved".to_vec(),
            b"zeta".to_vec(),
            b"alpha".to_vec(),
            Vec::new()
        ]
    );
    assert_eq!(owner.model(|m| m.state.real_count), 3);
    let target = owner.pointer();
    let mut retained = Vec::new();
    retained.push(owner);
    let owner = retained.pop().unwrap();
    assert_eq!(owner.pointer(), target);
    assert!(unsafe { target.as_ref() }.render());
    assert!(owner.model(|m| m.ready));
    key(&f, model::BACK, 0);
    owner.poll();
    assert_eq!(f.data.borrow().results, vec![false]);
    let events = f.events.borrow();
    assert!(events.iter().position(|e| *e == "result") < events.iter().position(|e| *e == "pop"));
    drop(events);
    owner.exit();
    drop(owner);
    assert_eq!(live(), 0);
    let events = f.events.borrow();
    let sdk = events.iter().position(|e| *e == "sdk_release").unwrap();
    assert_eq!(events.last(), Some(&"native_drop"));
    assert_eq!(events[sdk + 1], "free");
}
#[test]
fn equal_rank_rows_keep_first_scan_order_after_a_stronger_duplicate() {
    let (owner, f) = rig();
    owner.enter();
    scanned(
        &owner,
        &f,
        vec![
            row(b"z", -50, false),
            row(b"a", -20, false),
            row(b"z", -20, true),
            row(b"m", -20, false),
        ],
    );
    assert_eq!(
        names(&owner),
        vec![b"z".to_vec(), b"a".to_vec(), b"m".to_vec(), Vec::new()]
    );
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn every_scan_construction_allocation_failure_keeps_sdk_results_for_retry() {
    for failure in 0..6 {
        let (owner, f) = rig();
        owner.enter();
        f.events.borrow_mut().clear();
        f.data.borrow_mut().scan = vec![
            row(b"long-network-one-1234", -40, false),
            row(b"long-network-two-5678", -50, true),
        ];
        f.data.borrow_mut().scan_complete = 2;
        fail_after(failure);
        owner.poll();
        assert_eq!(phase(&owner), Phase::Scanning as u8, "failure {failure}");
        assert!(
            !f.events.borrow().contains(&"scan_delete"),
            "failure {failure} deleted the scan"
        );
        assert!(owner.model(|m| m.gate.failed()));
        owner.poll();
        assert_eq!(phase(&owner), Phase::List as u8);
        assert_eq!(names(&owner).len(), 3);
        assert!(owner.render());
        drop(owner);
        assert_eq!(live(), 0, "failure {failure}");
    }
}
#[test]
fn saved_auto_credentials_drop_before_connect_and_back_disconnects_before_result() {
    let (owner, f) = rig();
    f.auto.set(true);
    f.data.borrow_mut().credentials.push((
        b"long-saved-network-name".to_vec(),
        b"long-saved-password-value".to_vec(),
    ));
    f.data.borrow_mut().last = b"long-saved-network-name".to_vec();
    owner.enter();
    assert_eq!(phase(&owner), Phase::AutoConnecting as u8);
    assert!(!f.credential.get());
    let events = f.events.borrow();
    assert!(
        events.iter().position(|e| *e == "credential_drop")
            < events.iter().position(|e| *e == "connect")
    );
    drop(events);
    assert!(owner.render());
    key(&f, model::BACK, 0);
    owner.poll();
    assert_eq!(f.data.borrow().results, vec![false]);
    let events = f.events.borrow();
    let result = events.iter().rposition(|e| *e == "result").unwrap();
    assert_eq!(
        events[..result].iter().rev().find(|e| **e == "disconnect"),
        Some(&"disconnect")
    );
    drop(events);
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn password_result_connect_save_and_connected_effect_order_match_native() {
    let (owner, f) = rig();
    owner.enter();
    scanned(&owner, &f, vec![row(b"private", -40, true)]);
    assert!(owner.render());
    key(&f, 0, model::CONFIRM);
    owner.poll();
    assert_eq!(phase(&owner), Phase::Password as u8);
    assert_eq!(f.data.borrow().keyboard, vec![false]);
    owner.keyboard_returned(Some(b"secret"));
    key(&f, 0, 0);
    owner.poll();
    assert_eq!(phase(&owner), Phase::Connecting as u8);
    assert_eq!(
        f.data.borrow().connects,
        vec![(b"private".to_vec(), b"secret".to_vec())]
    );
    f.events.borrow_mut().clear();
    f.data.borrow_mut().connection = 3;
    owner.poll();
    assert_eq!(phase(&owner), Phase::Save as u8);
    let events = f.events.borrow();
    let selected: Vec<_> = events
        .iter()
        .copied()
        .filter(|e| {
            [
                "connected",
                "clock_sync",
                "clock_save",
                "trusted_time",
                "store_last",
                "plugin",
            ]
            .contains(e)
        })
        .collect();
    assert_eq!(
        selected,
        vec![
            "connected",
            "clock_sync",
            "clock_save",
            "trusted_time",
            "store_last",
            "plugin"
        ]
    );
    drop(events);
    assert!(owner.render());
    key(&f, model::CONFIRM, 0);
    owner.poll();
    assert_eq!(f.data.borrow().results, vec![true]);
    let events = f.events.borrow();
    assert!(
        events.iter().position(|e| *e == "store_save") < events.iter().position(|e| *e == "result")
    );
    drop(events);
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn forget_preserves_status_until_rebuild_and_uses_render_resource_transaction() {
    let (owner, f) = rig();
    f.data
        .borrow_mut()
        .credentials
        .push((b"saved".to_vec(), b"pw".to_vec()));
    owner.enter();
    scanned(&owner, &f, vec![row(b"saved", -40, true)]);
    assert!(owner.render());
    key(&f, model::LEFT, 0);
    owner.poll();
    assert_eq!(phase(&owner), Phase::Forget as u8);
    let before = {
        let _guard = owner.resource();
        (unsafe { (*owner.owned()).rows.as_slice() })[0].status()
    };
    let mut call = effects::Call::new(
        &owner,
        owner.model(|m| m.state.list.navigation_revision),
        None,
    );
    wifi::EffectHost::call(&mut call, wifi::Call::Forget, 0);
    drop(call);
    {
        let _guard = owner.resource();
        let rows = unsafe { (*owner.owned()).rows.as_slice() };
        assert!(!rows[0].saved());
        assert_eq!(rows[0].status(), before);
    }
    assert!(f.data.borrow().credentials.is_empty());
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn keyboard_cancel_empty_hidden_and_copy_failure_leave_a_usable_parent() {
    let (owner, f) = rig();
    owner.enter();
    scanned(&owner, &f, vec![]);
    assert!(owner.render());
    key(&f, 0, model::CONFIRM);
    owner.poll();
    assert_eq!(phase(&owner), Phase::HiddenSsid as u8);
    owner.keyboard_returned(Some(b""));
    assert_eq!(phase(&owner), Phase::List as u8);
    key(&f, 0, model::CONFIRM);
    owner.poll();
    owner.keyboard_returned(None);
    assert_eq!(phase(&owner), Phase::List as u8);
    key(&f, 0, model::CONFIRM);
    owner.poll();
    fail_after(0);
    owner.keyboard_returned(Some(b"new-long-hidden-network-name"));
    assert_eq!(phase(&owner), Phase::List as u8);
    assert!(f.events.borrow().contains(&"snapshot_oom"));
    assert!(owner.render());
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn prepared_render_has_no_new_representation_allocations_and_capacity_demand_uses_loop() {
    let (owner, f) = rig();
    owner.enter();
    scanned(&owner, &f, vec![row(b"public", -40, false)]);
    assert!(owner.render());
    let before = live();
    let (painted, allocations) = crate::boot::count_allocations(|| owner.render());
    assert!(painted);
    assert_eq!(allocations, 0);
    assert_eq!(live(), before);
    let mut layout = f.layout.get();
    layout.base.list.row_height = 1;
    layout.base.list.screen.h = 1600;
    layout.base.safe.h = 1600;
    layout.base.list.area.h = 1600;
    f.layout.set(layout);
    // More visible source rows make the new orientation exceed prepared storage.
    {
        let _guard = owner.resource();
        let rows = unsafe { &mut (*owner.owned()).rows };
        rows.clear();
        for i in 0..30 {
            rows.try_push(Row::scan(b"x", -40, false, false, i).unwrap())
                .unwrap();
        }
    }
    owner.model(|m| {
        m.state.count = 30;
        m.state.generation += 1;
    });
    let (rejected, allocations) = crate::boot::count_allocations(|| owner.render());
    assert!(!rejected);
    assert_eq!(allocations, 0);
    assert_eq!(unsafe { (*owner.frame()).needs_capacity }, 1);
    f.data.borrow_mut().scan_complete = -1;
    owner.poll();
    assert_eq!(unsafe { (*owner.frame()).needs_capacity }, 0);
    assert!(owner.render());
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn rejection_before_store_lock_blocks_forget() {
    let (owner, f) = rig();
    f.data
        .borrow_mut()
        .credentials
        .push((b"saved".to_vec(), b"pw".to_vec()));
    owner.enter();
    scanned(&owner, &f, vec![row(b"saved", -40, true)]);
    assert!(owner.render());
    key(&f, model::LEFT, 0);
    owner.poll();
    assert!(owner.render());
    key(&f, model::DOWN, 0);
    owner.poll();
    f.reject_at.set(Some("render_lock"));
    key(&f, model::CONFIRM, 0);
    owner.poll();
    assert!(!f.events.borrow().contains(&"store_remove"));
    assert_eq!(f.data.borrow().credentials.len(), 1);
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn all_translations_fit_inline_mac_and_count_buffers_and_render_every_phase() {
    for language in 0..xp_core::i18n::LANGUAGE_COUNT {
        let mut mac = [0; 64];
        let mut count = [0; 64];
        view::mac(&mut mac, language, &[0xff; 6], true);
        view::count(&mut count, language, 32767).unwrap();
        assert!(CStr::from_bytes_until_nul(&mac)
            .unwrap()
            .to_bytes()
            .ends_with(b"ff-ff-ff-ff-ff-ff"));
        assert!(CStr::from_bytes_until_nul(&count)
            .unwrap()
            .to_bytes()
            .windows(5)
            .any(|p| p == b"32767"));
    }
    let (owner, f) = rig();
    owner.enter();
    for phase in 0..=9 {
        owner.model(|m| {
            m.state.phase = phase;
            m.state.count = 0;
        });
        let state = owner.model(|m| m.state);
        owner.prepare(&state).unwrap();
        let painted = owner.render();
        assert_eq!(
            painted,
            !matches!(
                Phase::read(phase),
                Some(Phase::Password | Phase::HiddenSsid)
            ),
            "phase {phase}"
        );
    }
    owner.exit();
    assert!(!owner.render());
    drop(owner);
    assert_eq!(live(), 0);
    drop(f);
}
#[test]
#[ignore = "strict-Miri negative control: target has been destroyed"]
fn negative_destroyed_wifi_target() {
    let (owner, _f) = rig();
    let target = owner.pointer();
    drop(owner);
    unsafe { target.as_ref() }.reject();
}
#[test]
#[ignore = "strict-Miri negative control: overlapping model loan"]
fn negative_overlapping_wifi_model() {
    let (owner, _f) = rig();
    let model = owner.model.get();
    let first = unsafe { &mut *model };
    let second = unsafe { &mut *model };
    second.state.count = 1;
    core::hint::black_box(first.state.count);
}

#[test]
fn credential_copy_oom_retries_entry_or_restarts_a_completed_auto_scan() {
    for failure in 0..6 {
        let (owner, f) = rig();
        f.auto.set(true);
        f.data.borrow_mut().credentials.push((
            b"long-saved-network-name".to_vec(),
            b"long-saved-password-value".to_vec(),
        ));
        f.data.borrow_mut().last = b"long-saved-network-name".to_vec();
        fail_after(failure);
        owner.enter();
        assert!(f.data.borrow().connects.is_empty());
        assert!(!f.credential.get());
        if failure <= 3 {
            key(&f, model::BACK, 0);
            owner.poll();
            assert_eq!(f.data.borrow().results, vec![false]);
            assert!(!f.events.borrow().contains(&"disconnect"));
        } else {
            owner.poll();
            assert_eq!(phase(&owner), Phase::AutoConnecting as u8);
        }
        drop(owner);
        assert_eq!(live(), 0);
    }
    for failure in 5..8 {
        let (owner, f) = rig();
        f.auto.set(true);
        f.data.borrow_mut().credentials.push((
            b"long-saved-network-name".to_vec(),
            b"long-saved-password-value".to_vec(),
        ));
        owner.enter();
        f.events.borrow_mut().clear();
        fail_after(failure);
        scanned(&owner, &f, vec![row(b"long-saved-network-name", -40, true)]);
        assert!(f.events.borrow().contains(&"scan_delete"));
        assert!(f.data.borrow().connects.is_empty());
        owner.poll();
        assert!(f.events.borrow().contains(&"scan_begin"));
        scanned(&owner, &f, vec![row(b"long-saved-network-name", -40, true)]);
        assert_eq!(phase(&owner), Phase::AutoConnecting as u8);
        drop(owner);
        assert_eq!(live(), 0);
    }
}

#[test]
fn cancelled_child_commits_list_even_if_its_repaint_storage_cannot_be_allocated() {
    let (owner, f) = rig();
    owner.enter();
    scanned(&owner, &f, vec![]);
    assert!(owner.render());
    f.keyboard_fail.set(true);
    fail_after(0);
    key(&f, 0, model::CONFIRM);
    owner.poll();
    assert_eq!(phase(&owner), Phase::List as u8);
    assert!(owner.model(|m| m.gate.failed()));
    key(&f, model::BACK, 0);
    owner.poll();
    assert_eq!(f.data.borrow().results, vec![false]);
    drop(owner);
    assert_eq!(live(), 0);
}
#[test]
fn manual_and_hidden_saved_passwords_drop_native_loans_before_connect_and_complete() {
    for hidden in [false, true] {
        let (owner, f) = rig();
        f.data
            .borrow_mut()
            .credentials
            .push((b"saved".to_vec(), b"saved-password".to_vec()));
        owner.enter();
        scanned(
            &owner,
            &f,
            if hidden {
                vec![]
            } else {
                vec![row(b"saved", -40, true)]
            },
        );
        assert!(owner.render());
        key(&f, 0, model::CONFIRM);
        owner.poll();
        if hidden {
            owner.keyboard_returned(Some(b"saved"));
            key(&f, 0, 0);
            owner.poll();
        }
        assert_eq!(phase(&owner), Phase::Connecting as u8);
        assert!(!f.credential.get());
        assert_eq!(
            f.data.borrow().connects,
            vec![(b"saved".to_vec(), b"saved-password".to_vec())]
        );
        f.data.borrow_mut().connection = 3;
        key(&f, 0, 0);
        owner.poll();
        assert_eq!(f.data.borrow().results, vec![true]);
        assert!(!f.events.borrow().contains(&"store_save"));
        drop(owner);
        assert_eq!(live(), 0);
    }
}
#[test]
fn open_connect_and_all_manual_failure_kinds_keep_native_cancel_and_retry_behavior() {
    for status in [1, 4, 0, 3] {
        let (owner, f) = rig();
        owner.enter();
        scanned(&owner, &f, vec![row(b"open", -40, false)]);
        assert!(owner.render());
        key(&f, 0, model::CONFIRM);
        owner.poll();
        assert_eq!(phase(&owner), Phase::Connecting as u8);
        f.data.borrow_mut().connection = status;
        if status == 0 {
            f.now.set(16001);
        }
        key(&f, 0, 0);
        owner.poll();
        if status == 3 {
            assert_eq!(f.data.borrow().results, vec![true]);
        } else {
            assert_eq!(phase(&owner), Phase::Failed as u8);
            let id = match status {
                1 => xp_core::i18n::ids::STR_ERROR_NETWORK_NOT_FOUND,
                4 => xp_core::i18n::ids::STR_ERROR_GENERAL_FAILURE,
                _ => xp_core::i18n::ids::STR_ERROR_CONNECTION_TIMEOUT,
            };
            let _guard = owner.resource();
            assert_eq!(unsafe { CStr::from_ptr((*owner.owned()).error) }, unsafe {
                CStr::from_ptr(xp_core::i18n::get(0, id))
            });
            drop(_guard);
            key(&f, model::CONFIRM, 0);
            owner.poll();
            assert_eq!(phase(&owner), Phase::List as u8);
            assert!(owner.render());
        }
        drop(owner);
        assert_eq!(live(), 0);
    }
}

#[test]
fn phase_generation_and_count_changes_during_paint_never_publish_old_hits() {
    for change in 1..=3 {
        let (owner, f) = rig();
        owner.enter();
        scanned(&owner, &f, vec![row(b"public", -40, false)]);
        assert!(owner.render());
        f.paint_change.set(change);
        assert!(owner.render());
        assert!(!owner.model(|m| m.ready));
        assert!(f.events.borrow().contains(&"update"));
        assert!(f.data.borrow().connects.is_empty());
        drop(owner);
        assert_eq!(live(), 0);
    }
}

#[test]
fn list_paint_keeps_visible_rows_until_source_retirement_and_paint_return() {
    let (owner, f) = rig();
    owner.enter();
    scanned(&owner, &f, vec![row(b"visible-network", -40, false)]);
    assert!(owner.render());
    f.capture.set(true);
    f.paint_change.set(4);
    assert!(owner.render());
    assert!(f
        .labels
        .borrow()
        .iter()
        .any(|label| label == b"visible-network"));
    assert!(!owner.model(|m| m.ready));
    let _guard = owner.resource();
    assert!(unsafe { (*owner.frame()).rows.as_slice().is_empty() });
    assert!(!unsafe { (*owner.frame()).painting_rows });
    drop(_guard);
    drop(owner);
    assert_eq!(live(), 0);
}

#[test]
fn failed_paint_recovery_discards_a_held_confirm_release_but_accepts_the_next_gesture() {
    let (owner, f) = rig();
    owner.enter();
    scanned(&owner, &f, vec![row(b"private", -40, true)]);
    assert!(owner.render());
    f.input.set((
        Input {
            pressed: model::CONFIRM,
            held: model::CONFIRM,
            ..Input::default()
        },
        false,
    ));
    owner.poll();
    f.paint_ok.set(false);
    assert!(!owner.render());
    f.paint_ok.set(true);
    assert!(owner.render());
    key(&f, 0, model::CONFIRM);
    owner.poll();
    assert_eq!(phase(&owner), Phase::List as u8);
    assert!(f.data.borrow().keyboard.is_empty());
    key(&f, model::CONFIRM, 0);
    owner.poll();
    key(&f, 0, model::CONFIRM);
    owner.poll();
    assert_eq!(phase(&owner), Phase::Password as u8);
    assert_eq!(f.data.borrow().keyboard, vec![false]);
    drop(owner);
    assert_eq!(live(), 0);
}

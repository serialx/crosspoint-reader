extern crate alloc;
extern crate std;
use super::*;
use crate::{
    model::{Input, ListState},
    network::wifi::{self, Call, EffectHost, State},
};
use alloc::{boxed::Box, vec::Vec};
use core::{
    ffi::c_void,
    mem::{align_of, size_of, MaybeUninit},
    ptr,
};

#[test]
fn native_wire_slots_and_callback_table_have_pointer_sized_layouts() {
    assert_eq!(size_of::<Physical>(), 46 * size_of::<usize>());
    assert_eq!(size_of::<LastStorage>(), 7 * size_of::<usize>());
    assert_eq!(size_of::<CredentialStorage>(), 13 * size_of::<usize>());
    assert_eq!(size_of::<KeyboardStorage>(), 6 * size_of::<usize>());
    assert_eq!(align_of::<Snapshot>(), align_of::<usize>());
    assert_eq!(core::mem::offset_of!(Text, bytes), size_of::<usize>());
    assert_eq!(core::mem::offset_of!(Edits, assign), size_of::<usize>());
}
#[test]
fn bounded_snapshot_loans_keep_identity_and_explicit_empty_text() {
    let ssid = Box::new(*b"network");
    let password = Box::new(*b"password");
    // Raw POD copyout does not own native strings or extend their loans.
    let mut snapshot: Snapshot = unsafe { MaybeUninit::zeroed().assume_init() };
    snapshot.strings[0] = Text {
        string: ssid.as_ptr().cast(),
        bytes: Bytes::from_slice(ssid.as_slice()),
    };
    snapshot.strings[3] = Text {
        string: password.as_ptr().cast(),
        bytes: Bytes::from_slice(password.as_slice()),
    };
    snapshot.row.ssid = snapshot.strings[0];
    snapshot.row_valid = 1;
    snapshot.row_count = 1;
    assert_eq!(snapshot.strings[0].string, ssid.as_ptr().cast());
    assert_eq!(unsafe { snapshot.row.ssid.bytes.as_slice() }, b"network");
    assert_eq!(unsafe { snapshot.strings[3].bytes.as_slice() }, b"password");
    assert!(unsafe { Bytes::EMPTY.as_slice() }.is_empty());
    assert!(Text::EMPTY.string.is_null());
    let empty = Text::EMPTY;
    let edits = Edits {
        attempt: ptr::null(),
        assign: [
            &snapshot.strings[0],
            ptr::null(),
            &empty,
            &snapshot.strings[3],
        ],
    };
    assert_eq!(unsafe { (*edits.assign[0]).bytes.as_slice() }, b"network");
    assert!(edits.assign[1].is_null());
    assert!(unsafe { (*edits.assign[2]).bytes.as_slice() }.is_empty());
    // Every reference ends before the native allocations are released.
    drop(password);
    drop(ssid);
}
#[derive(Debug, PartialEq)]
struct Facts {
    list: ListState,
    words: [u32; 17],
}
fn facts(s: &State) -> Facts {
    Facts {
        list: s.list,
        words: [
            s.ui_revision,
            s.flash_index as u32,
            u32::from(s.flash_ticks),
            u32::from(s.ui_invalidated),
            s.connection_at,
            s.generation,
            u32::from(s.count),
            u32::from(s.real_count),
            u32::from(s.phase),
            u32::from(s.automatic),
            u32::from(s.manual_list),
            u32::from(s.used_saved),
            u32::from(s.requires_password),
            u32::from(s.save_selection),
            u32::from(s.forget_selection),
            u32::from(s.allow_auto),
            u32::from_le_bytes([s.reserved[0], s.reserved[1], 0, 0]),
        ],
    }
}
#[derive(Default, Debug, PartialEq)]
struct Record {
    calls: Vec<(u32, u32)>,
    publications: Vec<Facts>,
}
impl EffectHost for Record {
    fn call(&mut self, operation: Call, value: u32) -> u32 {
        self.calls.push((operation as u32, value));
        match operation {
            Call::Now => 1000,
            Call::EntryReset | Call::CredentialFlags | Call::ScanComplete | Call::RowCount => 2,
            Call::LastCredential
            | Call::RowCredential
            | Call::SelectedCredential
            | Call::PasswordPresent => 1,
            Call::ScanRead => u32::from(value == 0),
            Call::ScanFind => u32::MAX,
            Call::ScanRssi => (-42i32) as u32,
            Call::RowFlags => 1,
            Call::ConnectionStatus => 3,
            _ => 0,
        }
    }
    fn publish(&mut self, state: &mut State) {
        self.publications.push(facts(state));
    }
}
unsafe extern "C" fn effect(context: *mut c_void, op: u32, value: u32) -> u32 {
    // Convert only the finite production discriminant emitted by State::run.
    let operations = [
        Call::Now,
        Call::Paint,
        Call::EntryReset,
        Call::ReadMac,
        Call::Exit,
        Call::LastCredential,
        Call::CredentialFlags,
        Call::CopyAutoCredential,
        Call::DropCredential,
        Call::ClearScan,
        Call::WifiSta,
        Call::Disconnect,
        Call::Delay,
        Call::ScanBegin,
        Call::ScanComplete,
        Call::ScanReserve,
        Call::ScanRead,
        Call::ScanFind,
        Call::ScanAppend,
        Call::ScanRssi,
        Call::ScanReplace,
        Call::ScanSort,
        Call::RowCount,
        Call::HiddenAppend,
        Call::RowsRebuild,
        Call::ScanDelete,
        Call::RowFlags,
        Call::RowAttempted,
        Call::RowCredential,
        Call::SelectRow,
        Call::SelectedCredential,
        Call::CopyPassword,
        Call::PasswordPresent,
        Call::LogSaved,
        Call::HiddenReset,
        Call::Keyboard,
        Call::ConnectionReset,
        Call::ConnectionStart,
        Call::ConnectionStatus,
        Call::ConnectedDetails,
        Call::SetError,
        Call::LogComplete,
        Call::Complete,
        Call::LogAutoFailure,
        Call::LogManual,
        Call::Save,
        Call::Forget,
        Call::CopySelected,
        Call::Haptic,
    ];
    unsafe { &mut *context.cast::<Record>() }.call(operations[op as usize], value)
}
unsafe extern "C" fn publish(context: *mut c_void, state: *mut State) {
    unsafe { &mut *context.cast::<Record>() }.publish(unsafe { &mut *state });
}
#[test]
fn effect_trait_and_existing_callback_transport_preserve_order_and_results() {
    for phase in 0..10 {
        for event in 0..4 {
            for released in [0, 1, 2, 16, 32] {
                let mut typed = Record::default();
                let mut callback = Record::default();
                let mut a = State {
                    list: ListState::new(0),
                    phase,
                    count: 2,
                    real_count: 1,
                    allow_auto: 1,
                    generation: 7,
                    flash_index: -1,
                    ..State::default()
                };
                let mut b = a;
                let input = Input {
                    released,
                    now_ms: 2000,
                    ..Input::default()
                };
                let mut services = wifi::Services {
                    context: (&mut callback as *mut Record).cast(),
                    call: Some(effect),
                    publish: Some(publish),
                };
                a.run(&mut typed, event, 0, input);
                b.run(&mut services, event, 0, input);
                assert_eq!(
                    typed, callback,
                    "phase{phase}/event{event}/released{released}"
                );
                assert_eq!(facts(&a), facts(&b));
            }
        }
    }
}
#[test]
#[ignore = "strict-Miri negative control; deliberately reads a released native loan"]
fn negative_released_snapshot_loan() {
    let bytes = Box::new(*b"released");
    let view = Bytes::from_slice(bytes.as_slice());
    drop(bytes);
    core::hint::black_box(unsafe { view.as_slice() }[0]);
}
#[test]
#[ignore = "strict-Miri negative control; deliberately mutates a shared native loan"]
fn negative_mutated_snapshot_loan() {
    let mut bytes = Box::new(*b"borrowed");
    let raw = bytes.as_mut_ptr();
    let view = Bytes::from_slice(bytes.as_slice());
    let borrowed = unsafe { view.as_slice() };
    unsafe { raw.write(b'X') };
    core::hint::black_box(borrowed[0]);
}

use super::super::wifi_physical::{CredentialStorage, CredentialView, LastStorage};
use super::super::wifi_resources::sort_rows;
use super::*;
use core::mem::MaybeUninit;
use xp_core::i18n::{self, ids};
const LAST: u8 = 1;
const CREDENTIAL: u8 = 2;
const FOUND: u8 = 4;
const INPUT: u8 = 8;
const REJECTED: u8 = 16;
const COMMITTED: u8 = 32;
const FAILED: u8 = 64;
const SCAN_DELETED: u8 = 128;
#[derive(Clone, Copy)]
struct Scan {
    ssid: [u8; 33],
    rssi: i32,
    index: u32,
}
union Scratch {
    scan: Scan,
    credential: CredentialView,
}
pub(super) struct Call<'a, H: Host> {
    owner: &'a Wifi<H>,
    last: MaybeUninit<LastStorage>,
    credential: MaybeUninit<CredentialStorage>,
    scratch: MaybeUninit<Scratch>,
    revision: u32,
    flags: u8,
    keyboard_failed: bool,
}
impl<'a, H: Host> Call<'a, H> {
    pub fn new(owner: &'a Wifi<H>, revision: u32, input: Option<Input>) -> Self {
        let action = input.is_some_and(|i| {
            ((i.pressed | i.released | i.held) & !model::BACK) != 0
                || i.touch_flags != 0
                || i.swipe != 0
                || i.action != 0
        });
        Self {
            owner,
            last: MaybeUninit::uninit(),
            credential: MaybeUninit::uninit(),
            scratch: MaybeUninit::uninit(),
            revision,
            flags: if action { INPUT } else { 0 },
            keyboard_failed: false,
        }
    }
    pub fn keyboard_failed(&self) -> bool {
        self.keyboard_failed
    }
    pub fn failed(&self) -> bool {
        self.flags & FAILED != 0
    }
    pub fn scan_deleted(&self) -> bool {
        self.flags & SCAN_DELETED != 0
    }
    pub fn current(&mut self) -> bool {
        if self.failed() {
            return false;
        }
        if self.flags & INPUT == 0 || self.flags & COMMITTED != 0 {
            return true;
        }
        if self
            .owner
            .model(|m| m.gate.failed() || m.state.list.navigation_revision != self.revision)
        {
            self.flags |= REJECTED;
        }
        self.flags & REJECTED == 0
    }
    fn lookup(&mut self, ssid: Bytes) -> u32 {
        self.drop_credential();
        let mut out = MaybeUninit::uninit();
        let found = unsafe {
            (self.owner.physical().credential_lookup)(
                self.credential.as_mut_ptr(),
                ssid,
                out.as_mut_ptr(),
            )
        };
        self.flags |= CREDENTIAL;
        if found != 0 {
            self.flags |= FOUND;
        }
        self.scratch.write(Scratch {
            credential: unsafe { out.assume_init() },
        });
        u32::from(found)
    }
    fn drop_credential(&mut self) {
        if self.flags & CREDENTIAL != 0 {
            unsafe { (self.owner.physical().credential_drop)(self.credential.as_mut_ptr()) };
            self.flags &= !(CREDENTIAL | FOUND);
        }
    }
    fn credential(&self) -> CredentialView {
        unsafe { self.scratch.assume_init_ref().credential }
    }
    #[inline(never)]
    fn connected(&mut self) {
        let o = self.owner;
        let p = o.physical();
        let mut ip = [0; 16];
        unsafe { (p.connection_ip)(ip.as_mut_ptr().cast()) };
        {
            let _guard = o.resource();
            if !self.current() {
                return;
            }
            unsafe { (*o.owned()).ip = ip };
            self.flags |= COMMITTED;
        }
        unsafe {
            (p.connected_diagnostic)();
            if (p.clock_available)() != 0 && (p.clock_synced)() == 0 && (p.clock_sync)() != 0 {
                (p.clock_mark_and_save)();
            }
            (p.trusted_time_start)();
        }
        {
            let _render = o.render_guard();
            if !self.current() {
                return;
            }
            unsafe { (p.store_last)(Bytes::from_slice((*o.owned()).selected.as_bytes())) };
        }
        unsafe { (p.plugin_drain)(o.object()) };
    }
    #[inline(never)]
    fn perform(&mut self, operation: wifi::Call, value: u32) -> Result<u32, StorageError> {
        let o = self.owner;
        let p = o.physical();
        use wifi::Call::*;
        match operation {
            Now => return Ok(o.host.now()),
            Paint => o.host.request_update(),
            EntryReset => {
                {
                    let _render = o.render_guard();
                    if !self.current() {
                        return Ok(0);
                    }
                    unsafe { (p.store_load)() };
                }
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let r = unsafe { &mut *o.owned() };
                r.rows.clear();
                r.selected.clear();
                r.password.clear();
                r.ip = [0; 16];
                r.error = c"".as_ptr();
                r.attempts.clear();
                let count = unsafe { (p.store_count)() };
                r.attempts.try_reserve(count as usize)?;
                return Ok(count);
            }
            ReadMac => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let mut mac = [0; 6];
                let result = unsafe { (p.read_mac)(mac.as_mut_ptr()) };
                view::mac(
                    unsafe { &mut (*o.owned()).mac },
                    o.layout().language,
                    &mac,
                    result == 0,
                );
            }
            Exit => {
                o.diagnostic(Diagnostic::ExitStart, Bytes::EMPTY, 0);
                o.diagnostic(Diagnostic::ScanDelete, Bytes::EMPTY, 0);
                unsafe { (p.scan_delete)() };
                o.diagnostic(Diagnostic::ScanDeleted, Bytes::EMPTY, 0);
                o.diagnostic(Diagnostic::ExitEnd, Bytes::EMPTY, 0);
            }
            LastCredential => {
                let last = unsafe { (p.last_ssid)(self.last.as_mut_ptr()) };
                self.flags |= LAST;
                return Ok(if last.len == 0 { 0 } else { self.lookup(last) });
            }
            CredentialFlags => {
                let _guard = o.resource();
                if !self.current() || self.flags & FOUND == 0 {
                    return Ok(0);
                }
                let cred = self.credential();
                let ssid = unsafe { cred.ssid.as_slice() };
                let attempted = unsafe { (*o.owned()).attempts.as_slice() }
                    .iter()
                    .any(|a| a.as_bytes() == ssid);
                return Ok(u32::from(attempted) | (u32::from(cred.password.len != 0) << 1));
            }
            CopyAutoCredential => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let cred = self.credential();
                let ssid = unsafe { cred.ssid.as_slice() };
                let password = unsafe { cred.password.as_slice() };
                let r = unsafe { &mut *o.owned() };
                // Stage every fallible operation before publishing the attempted credential.
                r.selected.try_reserve(ssid.len())?;
                r.password.try_reserve(password.len())?;
                let mut attempt = SmallZ::new();
                attempt.try_assign(ssid)?;
                if r.attempts.as_slice().len() == r.attempts.capacity() {
                    let capacity = r
                        .attempts
                        .capacity()
                        .checked_mul(2)
                        .ok_or(StorageError::Capacity)?
                        .max(1);
                    r.attempts.try_reserve(capacity)?;
                }
                o.diagnostic(Diagnostic::AutoAttempt, cred.ssid, 0);
                r.attempts.try_push(attempt)?;
                r.selected.write_joined(&[ssid])?;
                r.password.write_joined(&[password])?;
                self.flags |= COMMITTED;
                self.drop_credential();
            }
            DropCredential => self.drop_credential(),
            ClearScan => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                unsafe { (*o.owned()).rows.clear() };
            }
            WifiSta => unsafe { (p.station_mode)() },
            Disconnect => {
                unsafe { (p.disconnect)() };
                self.flags |= COMMITTED;
            }
            Delay => o.host.delay(value),
            ScanBegin => unsafe { (p.scan_begin)() },
            ScanComplete => return Ok(unsafe { (p.scan_complete)() } as u32),
            ScanReserve => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                if value > 32767 {
                    return Err(StorageError::Capacity);
                }
                let r = unsafe { &mut *o.owned() };
                r.rows.clear();
                r.rows.try_reserve(value as usize + 1)?;
                self.flags |= COMMITTED;
            }
            ScanRead => {
                let mut scan = Scan {
                    ssid: [0; 33],
                    rssi: 0,
                    index: value,
                };
                unsafe {
                    (p.scan_ssid)(value, scan.ssid.as_mut_ptr().cast());
                    scan.rssi = (p.scan_rssi)(value)
                };
                let present = scan.ssid[0] != 0;
                self.scratch.write(Scratch { scan });
                return Ok(u32::from(present));
            }
            ScanFind => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let scan = unsafe { self.scratch.assume_init_ref().scan };
                let n = scan.ssid.iter().position(|b| *b == 0).unwrap_or(32);
                return Ok(unsafe { (*o.owned()).rows.as_slice() }
                    .iter()
                    .position(|r| r.ssid() == &scan.ssid[..n])
                    .map_or(u32::MAX, |i| i as u32));
            }
            ScanAppend | ScanReplace => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let scan = unsafe { self.scratch.assume_init_ref().scan };
                let n = scan.ssid.iter().position(|b| *b == 0).unwrap_or(32);
                let r = unsafe { &mut *o.owned() };
                let encrypted = unsafe { (p.scan_encrypted)(scan.index) } != 0;
                if matches!(operation, ScanAppend) {
                    let saved = unsafe { (p.store_has)(Bytes::from_slice(&scan.ssid[..n])) } != 0;
                    let row = Row::scan(
                        &scan.ssid[..n],
                        scan.rssi,
                        encrypted,
                        saved,
                        scan.index as u16,
                    )?;
                    r.rows.try_push(row)?;
                } else if let Some(row) = r.rows.as_mut_slice().get_mut(value as usize) {
                    row.replace_signal(scan.rssi, encrypted);
                }
            }
            ScanRssi => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                return Ok(if value == u32::MAX {
                    unsafe { self.scratch.assume_init_ref().scan.rssi }
                } else {
                    (unsafe { (*o.owned()).rows.as_slice() })[value as usize].rssi()
                } as u32);
            }
            ScanSort => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                sort_rows(unsafe { (*o.owned()).rows.as_mut_slice() });
            }
            RowCount => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                return Ok(unsafe { (*o.owned()).rows.as_slice().len() } as u32);
            }
            HiddenAppend => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                unsafe { (*o.owned()).rows.try_push(Row::hidden()) }?;
            }
            RowsRebuild => {
                let mut future = {
                    let _guard = o.resource();
                    if !self.current() {
                        return Ok(0);
                    }
                    let rows = unsafe { (*o.owned()).rows.as_mut_slice() };
                    for row in rows.iter_mut() {
                        row.rebuild_status();
                    }
                    let mut state = o.model(|m| m.state);
                    state.count = rows.len() as u16;
                    state.real_count = state.count.saturating_sub(1);
                    state
                };
                // Reserve the completed list before the runner deletes SDK scan data.
                future.phase = Phase::List as u8;
                if o.prepare(&future).is_err() {
                    self.flags |= FAILED;
                }
            }
            ScanDelete => {
                unsafe { (p.scan_delete)() };
                self.flags |= COMMITTED | SCAN_DELETED;
            }
            RowFlags => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                return Ok(unsafe { (*o.owned()).rows.as_slice() }
                    .get(value as usize)
                    .map_or(0, |r| {
                        u32::from(r.saved())
                            | (u32::from(r.is_hidden()) << 1)
                            | (u32::from(r.encrypted()) << 2)
                    }));
            }
            RowAttempted => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let r = unsafe { &*o.owned() };
                let ssid = r.rows.as_slice()[value as usize].ssid();
                return Ok(r
                    .attempts
                    .as_slice()
                    .iter()
                    .any(|a| a.as_bytes() == ssid)
                    .into());
            }
            RowCredential | SelectedCredential => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let r = unsafe { &*o.owned() };
                let ssid = if matches!(operation, RowCredential) {
                    r.rows.as_slice()[value as usize].ssid()
                } else {
                    r.selected.as_bytes()
                };
                return Ok(self.lookup(Bytes::from_slice(ssid)));
            }
            SelectRow | CopySelected => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let r = unsafe { &mut *o.owned() };
                r.selected
                    .try_assign(r.rows.as_slice()[value as usize].ssid())?;
                if matches!(operation, SelectRow) {
                    r.password.clear();
                }
                self.flags |= COMMITTED;
            }
            CopyPassword => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                unsafe {
                    (*o.owned())
                        .password
                        .try_assign(self.credential().password.as_slice())
                }?;
                self.flags |= COMMITTED;
                self.drop_credential();
            }
            PasswordPresent => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                return Ok(unsafe { !(*o.owned()).password.is_empty() }.into());
            }
            LogSaved | LogAutoFailure => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let r = unsafe { &*o.owned() };
                let event = if matches!(operation, LogAutoFailure) {
                    Diagnostic::AutoFailure
                } else if value != 0 {
                    Diagnostic::HiddenSaved
                } else {
                    Diagnostic::Saved
                };
                o.diagnostic(
                    event,
                    Bytes::from_slice(r.selected.as_bytes()),
                    r.password.len() as u32,
                );
            }
            HiddenReset => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let r = unsafe { &mut *o.owned() };
                r.selected.clear();
                r.password.clear();
                self.flags |= COMMITTED;
            }
            Keyboard => {
                self.drop_credential();
                o.model(|m| m.keyboard = value != 0);
                self.keyboard_failed = !o.host.keyboard(value != 0);
            }
            ConnectionReset => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                self.drop_credential();
                let r = unsafe { &mut *o.owned() };
                r.ip = [0; 16];
                r.error = c"".as_ptr();
                self.flags |= COMMITTED;
            }
            ConnectionStart => unsafe {
                let r = &*o.owned();
                (p.connect)(
                    r.selected.as_z().as_ptr().cast(),
                    r.password.as_z().as_ptr().cast(),
                    value as u8,
                )
            },
            ConnectionStatus => return Ok(unsafe { (p.connection_status)() } as u32),
            ConnectedDetails => self.connected(),
            SetError => {
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let id = match value {
                    1 => ids::STR_ERROR_NETWORK_NOT_FOUND,
                    2 => ids::STR_ERROR_CONNECTION_TIMEOUT,
                    _ => ids::STR_ERROR_GENERAL_FAILURE,
                };
                unsafe { (*o.owned()).error = i18n::get(o.layout().language, id) };
                self.flags |= COMMITTED;
            }
            LogComplete => o.diagnostic(Diagnostic::Connected, Bytes::EMPTY, 0),
            Complete => {
                {
                    let _guard = o.resource();
                    let r = unsafe { &*o.owned() };
                    let n = r.ip.iter().position(|b| *b == 0).unwrap_or(15);
                    unsafe {
                        (p.prepare_result_retire_zc1)(
                            o.object(),
                            value as u8,
                            Bytes::from_slice(r.selected.as_bytes()),
                            Bytes::from_slice(&r.ip[..n]),
                        )
                    };
                }
                o.host.complete();
            }
            LogManual => o.diagnostic(Diagnostic::Manual, Bytes::EMPTY, 0),
            Save | Forget => {
                let _render = o.render_guard();
                if !self.current() {
                    return Ok(0);
                }
                let _guard = o.resource();
                if !self.current() {
                    return Ok(0);
                }
                let r = unsafe { &mut *o.owned() };
                let ssid = Bytes::from_slice(r.selected.as_bytes());
                if matches!(operation, Save) {
                    unsafe { (p.store_save)(ssid, Bytes::from_slice(r.password.as_bytes())) };
                } else {
                    unsafe { (p.store_remove)(ssid) };
                    if let Some(row) = r
                        .rows
                        .as_mut_slice()
                        .iter_mut()
                        .find(|row| row.ssid() == r.selected.as_bytes())
                    {
                        row.set_saved(false);
                    }
                }
                self.flags |= COMMITTED;
            }
            Haptic => o.host.haptic(value != 0),
        }
        Ok(0)
    }
}
impl<H: Host> Drop for Call<'_, H> {
    fn drop(&mut self) {
        self.drop_credential();
        if self.flags & LAST != 0 {
            unsafe { (self.owner.physical().last_ssid_drop)(self.last.as_mut_ptr()) };
        }
    }
}
impl<H: Host> wifi::EffectHost for Call<'_, H> {
    fn publish(&mut self, next: &mut wifi::State) {
        if !self.current() {
            return;
        }
        let o = self.owner;
        if o.prepare(next).is_err() {
            self.flags |= FAILED;
            return;
        }
        let leaving = o.model(|m| {
            let stale = m.gate.failed() || m.state.list.navigation_revision != self.revision;
            if self.flags & COMMITTED == 0 && self.flags & INPUT != 0 && stale {
                self.flags |= REJECTED;
                return false;
            }
            let leaving = m.state.phase == Phase::List as u8 && next.phase != Phase::List as u8;
            if stale {
                next.list.active = -1;
                next.list.last_repeat = 0;
                next.list.pending_scroll = 0;
                next.list.navigation_revision = m.state.list.navigation_revision.wrapping_add(1);
            }
            m.state.publish_model(next);
            self.flags |= COMMITTED;
            self.revision = m.state.list.navigation_revision;
            leaving
        });
        if leaving {
            o.retire_rows();
        }
    }
    fn call(&mut self, operation: wifi::Call, value: u32) -> u32 {
        if matches!(operation, wifi::Call::DropCredential) {
            self.drop_credential();
            return 0;
        }
        if !self.current() {
            return 0;
        }
        match self.perform(operation, value) {
            Ok(value) => value,
            Err(error) => {
                self.owner.allocation_error(error, false);
                self.flags |= FAILED;
                0
            }
        }
    }
}

extern crate std;
use super::super::wifi_physical::{CredentialStorage, CredentialView, LastStorage, PaintData};
use super::super::wifi_resources::Global;
use super::*;
pub(super) use crate::network::stable_owner::StableOwner;
use crate::scene::{Kind, Rect};
use std::{
    cell::{Cell, RefCell},
    rc::Rc,
    vec::Vec,
};
std::thread_local! {
    static CURRENT:Cell<*const Fixture>=const{Cell::new(ptr::null())};
    static FAIL:Cell<Option<usize>>=const{Cell::new(None)};
    static LIVE:Cell<usize>=const{Cell::new(0)};
    static FAIL_OWNER:Cell<bool>=const{Cell::new(false)};
}
pub fn fail_after(n: usize) {
    FAIL.with(|f| f.set(Some(n)));
}
pub fn live() -> usize {
    LIVE.with(Cell::get)
}
fn current() -> &'static Fixture {
    unsafe { &*CURRENT.with(Cell::get) }
}
pub struct TestMemory;
unsafe impl Memory for TestMemory {
    fn allocate(layout: Layout) -> *mut u8 {
        current().event("allocate");
        if FAIL.with(|f| match f.get() {
            Some(0) => {
                f.set(None);
                true
            }
            Some(n) => {
                f.set(Some(n - 1));
                false
            }
            None => false,
        }) {
            return ptr::null_mut();
        }
        let out = Global::allocate(layout);
        if !out.is_null() {
            LIVE.with(|n| n.set(n.get() + layout.size()));
        }
        out
    }
    unsafe fn release(pointer: *mut u8, layout: Layout) {
        current().event("free");
        LIVE.with(|n| n.set(n.get() - layout.size()));
        unsafe { Global::release(pointer, layout) };
    }
}
#[derive(Clone)]
pub struct ScanRow {
    pub ssid: Vec<u8>,
    pub rssi: i32,
    pub encrypted: bool,
}
#[derive(Default)]
pub struct Data {
    pub scan: Vec<ScanRow>,
    pub scan_complete: i32,
    pub connection: i32,
    pub credentials: Vec<(Vec<u8>, Vec<u8>)>,
    pub last: Vec<u8>,
    pub connects: Vec<(Vec<u8>, Vec<u8>)>,
    pub results: Vec<bool>,
    pub keyboard: Vec<bool>,
}
pub struct Fixture {
    pub data: RefCell<Data>,
    pub events: RefCell<Vec<&'static str>>,
    pub model: Cell<bool>,
    pub resource: Cell<bool>,
    pub render: Cell<bool>,
    pub credential: Cell<bool>,
    pub credential_slot: Cell<*mut CredentialStorage>,
    pub last_slot: Cell<*mut LastStorage>,
    pub target: Cell<*const Wifi<TestHost>>,
    pub reject_at: Cell<Option<&'static str>>,
    pub input: Cell<(Input, bool)>,
    pub layout: Cell<wifi_view::Layout>,
    pub auto: Cell<bool>,
    pub paint_ok: Cell<bool>,
    pub paint_change: Cell<u8>,
    pub keyboard_fail: Cell<bool>,
    pub capture: Cell<bool>,
    pub labels: RefCell<Vec<Vec<u8>>>,
    pub now: Cell<u32>,
}
impl Fixture {
    pub fn event(&self, event: &'static str) {
        assert!(!self.model.get(), "physical operation under ModelLock");
        self.events.borrow_mut().push(event);
        if self.reject_at.get() == Some(event) {
            self.reject_at.set(None);
            unsafe { &*self.target.get() }.reject();
        }
    }
}
pub struct TestHost(pub Rc<Fixture>);
impl Drop for TestHost {
    fn drop(&mut self) {
        self.0.event("native_drop");
    }
}
unsafe impl Host for TestHost {
    type Memory = TestMemory;
    fn allocate_owner(layout: Layout) -> *mut u8 {
        if FAIL_OWNER.with(|f| f.replace(false)) {
            ptr::null_mut()
        } else {
            unsafe { alloc::alloc::alloc(layout) }
        }
    }
    fn physical(&self) -> &Physical {
        &PHYSICAL
    }
    fn object(&self) -> *mut c_void {
        Rc::as_ptr(&self.0).cast_mut().cast()
    }
    fn model_lock(&self) {
        assert!(!self.0.model.replace(true));
    }
    fn model_unlock(&self) {
        assert!(self.0.model.replace(false));
    }
    fn render_lock(&self) -> *mut c_void {
        assert!(!self.0.resource.get(), "Resource -> Render inversion");
        assert!(!self.0.render.replace(true));
        self.0.event("render_lock");
        ptr::without_provenance_mut(1)
    }
    fn render_unlock(&self, _: *mut c_void) {
        assert!(self.0.render.replace(false));
        self.0.event("render_unlock");
    }
    fn enter(&self) {
        self.0.event("enter");
    }
    fn exit(&self) {
        self.0.event("exit");
    }
    fn now(&self) -> u32 {
        self.0.now.get()
    }
    fn delay(&self, _: u32) {
        self.0.event("delay");
    }
    fn sample_input(&self, _: bool) -> (Input, bool) {
        self.0.input.get()
    }
    fn request_update(&self) {
        self.0.event("update");
    }
    fn haptic(&self, _: bool) {
        self.0.event("haptic");
    }
    fn complete(&self) {
        self.0.event("pop");
    }
    fn keyboard(&self, hidden: bool) -> bool {
        self.0.event("keyboard");
        self.0.data.borrow_mut().keyboard.push(hidden);
        !self.0.keyboard_fail.replace(false)
    }
    fn paint(&self, scene: &Scene<'_>, paint: &Paint) -> bool {
        self.0.event("paint");
        let change = self.0.paint_change.replace(0);
        if change == 4 {
            let owner = unsafe { &*self.0.target.get() };
            owner.retire_rows();
            {
                let _guard = owner.resource();
                unsafe {
                    (*owner.owned()).rows.clear();
                    assert!(!(*owner.frame()).rows.as_slice().is_empty());
                }
            }
        }
        if change != 0 {
            unsafe { &*self.0.target.get() }.model(|m| match change {
                1 | 4 => m.state.phase = Phase::Connecting as u8,
                2 => m.state.generation = m.state.generation.wrapping_add(1),
                3 => m.state.count += 1,
                _ => unreachable!(),
            });
        }
        if !self.0.paint_ok.get() {
            return false;
        }
        for command in scene.commands {
            if command.resource == 0 {
                continue;
            }
            assert!(matches!(command.kind,k if k==Kind::Bitmap as u8||k==Kind::TextHandle as u8));
            if unsafe {
                paint.resource_valid.unwrap()(paint.context, command.resource, command.generation)
            } == 0
            {
                return false;
            }
            unsafe { paint.resource.unwrap()(paint.context, ptr::null(), command, command.rect) };
        }
        true
    }
    fn refresh(&self, _: Refresh) {
        self.0.event("refresh");
    }
}
unsafe extern "C" fn resource(object: *mut c_void, acquire: u8) {
    let f = unsafe { &*object.cast::<Fixture>() };
    assert!(!f.model.get());
    if acquire != 0 {
        assert!(!f.resource.replace(true));
        f.event("resource_lock");
    } else {
        assert!(f.resource.replace(false));
        f.event("resource_unlock");
    }
}
unsafe extern "C" fn lookup(
    slot: *mut CredentialStorage,
    key: Bytes,
    out: *mut CredentialView,
) -> u8 {
    let f = current();
    f.event("credential");
    assert!(!f.credential.replace(true));
    assert!(f.credential_slot.replace(slot).is_null());
    let found = f
        .data
        .borrow()
        .credentials
        .iter()
        .find(|(s, _)| s.as_slice() == unsafe { key.as_slice() })
        .cloned();
    let present = found.is_some();
    let loan = slot.cast::<Option<(Vec<u8>, Vec<u8>)>>();
    unsafe {
        loan.write(found);
        out.write(if let Some((ssid, password)) = &*loan {
            CredentialView {
                ssid: Bytes::from_slice(ssid),
                password: Bytes::from_slice(password),
            }
        } else {
            CredentialView {
                ssid: Bytes::EMPTY,
                password: Bytes::EMPTY,
            }
        })
    };
    present.into()
}
unsafe extern "C" fn drop_credential(slot: *mut CredentialStorage) {
    let f = current();
    assert_eq!(
        f.credential_slot.replace(ptr::null_mut()),
        slot,
        "native credential storage moved"
    );
    assert!(f.credential.replace(false));
    f.event("credential_drop");
    unsafe { ptr::drop_in_place(slot.cast::<Option<(Vec<u8>, Vec<u8>)>>()) };
}
macro_rules! c {
    ($($name:ident : $ty:ty),*; $ret:ty; $body:expr) => {{
        unsafe extern "C" fn callback($($name:$ty),*) -> $ret { { let body: fn($($ty),*) -> $ret = $body; body($($name),*) } }
        callback
    }};
}
static PHYSICAL: Physical = Physical {
    allow_auto: c!(a0: *mut c_void; u8; |_| current().auto.get().into()),
    resource_lock: resource,
    release_resources: c!(a0: *mut c_void; (); |_| current().event("sdk_release")),
    layout: c!(a0: *mut c_void, a1: *mut wifi_view::Layout; (); |_, out| unsafe { out.write(current().layout.get()) }),
    width: c!(a0: *mut c_void, a1: *const c_char, a2: i32, a3: u8; i32; |_, text, _, _| unsafe { CStr::from_ptr(text) }.to_bytes().len() as i32 * 8),
    height: c!(a0: *mut c_void, a1: *const c_char, a2: i32, a3: u8, a4: u8, a5: i16; i32; |_, text, _, _, lines, width| {
        let n = unsafe { CStr::from_ptr(text) }.to_bytes().len() as i32 * 8;
        ((n + i32::from(width).max(1) - 1) / i32::from(width).max(1)).min(i32::from(lines).max(1))
            * 20
    }),
    line: c!(a0: *mut c_void, a1: i32; i32; |_, _| 20),
    paint: c!(a0: *mut c_void, a1: *const Command, a2: Rect, a3: *const PaintData; (); |_, command, _, data| {
        let f = current();
        f.event("draw");
        let data = unsafe { &*data };
        let flow = matches!(data.kind, super::super::wifi_physical::DrawKind::FlowText);
        if flow {
            assert!(f.resource.get());
            assert!(matches!(unsafe { (*command).max_lines }, 1 | 2));
            assert!(unsafe { CStr::from_ptr(data.labels[0]) }.to_bytes().len() + 4 <= 64);
            let owner = unsafe { &*f.target.get() };
            let frame = owner.frame();
            assert_eq!(data.labels[1], unsafe { ptr::addr_of!((*frame).mac).cast() });
            assert_eq!(data.labels[2], unsafe { ptr::addr_of!((*frame).count_text).cast() });
            for pointer in &data.labels[1..3] {
                assert_ne!(*pointer, data.labels[0]);
                // Exercise the entire mutable loan; the next frame must restore its headers.
                unsafe { ptr::write_bytes(pointer.cast_mut(), b'x', 63) };
                unsafe { pointer.cast_mut().add(63).write(0) };
            }
        }
        if f.capture.get() {
            for label in &data.labels[..if flow { 1 } else { 4 }] {
                f.labels
                    .borrow_mut()
                    .push(unsafe { CStr::from_ptr(*label) }.to_bytes().to_vec());
            }
        }
    }),
    read_mac: c!(a0: *mut u8; i32; |out| {
        unsafe { ptr::copy_nonoverlapping([0, 1, 2, 3, 4, 255].as_ptr(), out, 6) };
        0
    }),
    last_ssid_drop: c!(a0: *mut LastStorage; (); |slot| {
        assert_eq!(current().last_slot.replace(ptr::null_mut()), slot, "native last-SSID storage moved");
        current().event("last_drop");
        unsafe { ptr::drop_in_place(slot.cast::<Vec<u8>>()) };
    }),
    last_ssid: c!(a0: *mut LastStorage; Bytes; |slot| {
        assert!(current().last_slot.replace(slot).is_null());
        current().event("last");
        let value = current().data.borrow().last.clone();
        let out = slot.cast::<Vec<u8>>();
        unsafe {
            out.write(value);
            Bytes::from_slice(&*out)
        }
    }),
    credential_lookup: lookup,
    credential_drop: drop_credential,
    store_load: c!(; (); || {
        assert!(current().render.get());
        assert!(!current().resource.get());
        current().event("store_load");
    }),
    store_save: c!(a0: Bytes, a1: Bytes; (); |s, p| {
        let f = current();
        assert!(f.render.get() && f.resource.get());
        f.event("store_save");
        f.data.borrow_mut().credentials.push((
            unsafe { s.as_slice() }.to_vec(),
            unsafe { p.as_slice() }.to_vec(),
        ));
    }),
    store_remove: c!(a0: Bytes; (); |key| {
        let f = current();
        assert!(f.render.get() && f.resource.get());
        f.event("store_remove");
        f.data
            .borrow_mut()
            .credentials
            .retain(|(s, _)| s.as_slice() != unsafe { key.as_slice() });
    }),
    store_last: c!(a0: Bytes; (); |key| {
        let f = current();
        assert!(f.render.get() && !f.resource.get());
        f.event("store_last");
        f.data.borrow_mut().last = unsafe { key.as_slice() }.to_vec();
    }),
    store_count: c!(; u32; || current().data.borrow().credentials.len() as u32),
    store_has: c!(a0: Bytes; u8; |key| {
        current()
            .data
            .borrow()
            .credentials
            .iter()
            .any(|(s, _)| s.as_slice() == unsafe { key.as_slice() })
            .into()
    }),
    station_mode: c!(; (); || current().event("station")),
    disconnect: c!(; (); || current().event("disconnect")),
    scan_begin: c!(; (); || { current().event("scan_begin"); current().data.borrow_mut().scan_complete = -1; }),
    scan_complete: c!(; i32; || current().data.borrow().scan_complete),
    scan_ssid: c!(a0: u32, a1: *mut c_char; (); |index, out| {
        let f = current();
        let d = f.data.borrow();
        let s = &d.scan[index as usize].ssid;
        let mut z = [0; 33];
        copy_z(&mut z, s);
        unsafe { ptr::copy_nonoverlapping(z.as_ptr(), out.cast(), 33) };
    }),
    scan_rssi: c!(a0: u32; i32; |index| current().data.borrow().scan[index as usize].rssi),
    scan_encrypted: c!(a0: u32; u8; |index| {
        current().data.borrow().scan[index as usize]
            .encrypted
            .into()
    }),
    scan_delete: c!(; (); || { current().event("scan_delete"); let mut data=current().data.borrow_mut(); data.scan_complete = -2; data.scan.clear(); }),
    connect: c!(a0: *const c_char, a1: *const c_char, a2: u8; (); |s, p, use_password| {
        let f = current();
        assert!(!f.credential.get(), "credential retained through connect");
        f.event("connect");
        f.data.borrow_mut().connects.push((
            unsafe { CStr::from_ptr(s) }.to_bytes().to_vec(),
            if use_password != 0 {
                unsafe { CStr::from_ptr(p) }.to_bytes().to_vec()
            } else {
                Vec::new()
            },
        ));
    }),
    connection_status: c!(; i32; || current().data.borrow().connection),
    connection_ip: c!(a0: *mut c_char; (); |out| unsafe {
        ptr::copy_nonoverlapping(c"192.0.2.1".as_ptr(), out, 10)
    }),
    connected_diagnostic: c!(; (); || current().event("connected")),
    clock_available: c!(; u8; || 1),
    clock_synced: c!(; u8; || 0),
    clock_sync: c!(; u8; || {
        current().event("clock_sync");
        1
    }),
    clock_mark_and_save: c!(; (); || current().event("clock_save")),
    trusted_time_start: c!(; (); || current().event("trusted_time")),
    plugin_drain: c!(a0: *mut c_void; (); |_| current().event("plugin")),
    diagnostic: c!(a0: Diagnostic, a1: Bytes, a2: u32; (); |kind, _, _| {
        current().event(match kind {
            Diagnostic::SnapshotOom => "snapshot_oom",
            Diagnostic::SceneOom => "scene_oom",
            Diagnostic::InvalidScene => "invalid_scene",
            _ => "diagnostic",
        })
    }),
    prepare_result_retire_zc1: c!(a0: *mut c_void, a1: u8, a2: Bytes, a3: Bytes; (); |_, connected, _, _| {
        current().event("result");
        current().data.borrow_mut().results.push(connected != 0);
    }),
};
use core::ffi::{c_char, CStr};
pub fn rig() -> (StableOwner<Wifi<TestHost>>, Rc<Fixture>) {
    assert_eq!(live(), 0);
    FAIL.with(|f| f.set(None));
    let screen = Rect {
        x: 0,
        y: 0,
        w: 480,
        h: 800,
    };
    let mut layout = wifi_view::Layout::default();
    layout.base.safe = screen;
    layout.base.list.screen = screen;
    layout.base.list.area = screen;
    layout.base.list.row_height = 40;
    layout.base.header_height = 40;
    layout.base.tab_height = 24;
    layout.base.spacing = 8;
    let fixture = Rc::new(Fixture {
        data: RefCell::new(Data {
            scan_complete: -1,
            ..Data::default()
        }),
        events: RefCell::new(Vec::with_capacity(4096)),
        model: Cell::new(false),
        resource: Cell::new(false),
        render: Cell::new(false),
        credential: Cell::new(false),
        credential_slot: Cell::new(ptr::null_mut()),
        last_slot: Cell::new(ptr::null_mut()),
        target: Cell::new(ptr::null()),
        reject_at: Cell::new(None),
        input: Cell::new((Input::default(), false)),
        layout: Cell::new(layout),
        auto: Cell::new(false),
        paint_ok: Cell::new(true),
        paint_change: Cell::new(0),
        keyboard_fail: Cell::new(false),
        capture: Cell::new(false),
        labels: RefCell::new(Vec::new()),
        now: Cell::new(1000),
    });
    CURRENT.with(|c| c.set(Rc::as_ptr(&fixture)));
    let owner = StableOwner::from(Wifi::try_new(TestHost(fixture.clone())).unwrap());
    fixture.target.set(owner.pointer().as_ptr());
    (owner, fixture)
}
pub fn key(f: &Fixture, pressed: u32, released: u32) {
    f.input.set((
        Input {
            pressed,
            released,
            now_ms: f.now.get(),
            ..Input::default()
        },
        false,
    ));
}
pub fn row(s: &[u8], rssi: i32, encrypted: bool) -> ScanRow {
    ScanRow {
        ssid: s.to_vec(),
        rssi,
        encrypted,
    }
}
pub fn scanned(owner: &Wifi<TestHost>, f: &Fixture, rows: Vec<ScanRow>) {
    let n = rows.len() as i32;
    f.data.borrow_mut().scan = rows;
    f.data.borrow_mut().scan_complete = n;
    owner.poll();
}

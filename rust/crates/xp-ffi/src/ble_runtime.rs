//! Singleton runtime with the same main/render/start-task ownership as the host API.
use super::ble::*;
#[cfg(not(target_has_atomic = "ptr"))]
use super::ble_atomic::{AtomicBool, AtomicU8};
use core::cell::{Cell, UnsafeCell};
use core::ffi::c_char;
use core::sync::atomic::Ordering::{AcqRel, Acquire, Relaxed, Release};
#[cfg(target_has_atomic = "ptr")]
use core::sync::atomic::{AtomicBool, AtomicU8};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Scene {
    pub location: u8,
    pub visit: u32,
    pub page_shown: bool,
    pub book_indexing: bool,
    pub storage_busy: bool,
    pub wifi_on: bool,
    pub sleeping: bool,
    pub local_key: bool,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Peer {
    pub addr: *const u8,
    pub name: *const c_char,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct RawEdge {
    pub report_id: u8,
    pub byte_index: u8,
    pub value: u8,
    pub pressed: bool,
    pub keycode: u8,
    pub mods: u8,
    pub was_rest: bool,
    pub at_ms: u32,
}
impl RawEdge {
    fn code(self) -> u32 {
        u32::from(self.value) | u32::from(self.byte_index) << 8 | u32::from(self.report_id) << 16
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct KeyPress {
    pub keycode: u8,
    pub mods: u8,
    pub pressed: bool,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ConnectionActivity {
    pub busy: bool,
    pub advertisements: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Event {
    pub code: u32,
    pub pressed: bool,
    pub at_ms: u32,
    pub was_rest: bool,
}
#[repr(C)]
pub struct Status {
    pub compiled_in: bool,
    pub running: bool,
    pub stopping: bool,
    pub starting: bool,
    pub scanning: bool,
    pub connected: bool,
    pub connecting: bool,
    pub idle_stopped: bool,
    pub reader_deferred: bool,
}

// Borrowed immutable boot-lifetime callbacks. They retain no Rust-owned pointers.
// Diagnostic event payloads are documented in BleRust.h; formatting stays in C++.
#[repr(C)]
pub struct Services {
    pub heap: unsafe extern "C" fn() -> Heap,
    pub release: unsafe extern "C" fn() -> bool,
    pub deliver: unsafe extern "C" fn(u8) -> bool,
    pub yield_radio: unsafe extern "C" fn() -> bool,
    pub transfer: unsafe extern "C" fn() -> bool,
    pub restart: unsafe extern "C" fn(),
    pub hold: unsafe extern "C" fn(bool),
    pub diagnostic: Option<unsafe extern "C" fn(u32, u32, u32, u32, u32)>,
    pub heap_map: unsafe extern "C" fn(),
    pub compiled_in: unsafe extern "C" fn() -> bool,
    pub begin: unsafe extern "C" fn() -> bool,
    pub end: unsafe extern "C" fn(u32) -> bool,
    pub running: unsafe extern "C" fn() -> bool,
    pub stopping: unsafe extern "C" fn() -> bool,
    pub connected: unsafe extern "C" fn() -> bool,
    pub connecting: unsafe extern "C" fn() -> bool,
    pub activity: unsafe extern "C" fn() -> ConnectionActivity,
    pub scanning: unsafe extern "C" fn() -> bool,
    pub poll: unsafe extern "C" fn(),
    pub pop_raw: unsafe extern "C" fn(*mut RawEdge) -> bool,
    pub pop_key: unsafe extern "C" fn(*mut KeyPress) -> bool,
    pub arm: unsafe extern "C" fn(u8, *const u8) -> bool,
    pub linked: unsafe extern "C" fn() -> Peer,
    pub scan: unsafe extern "C" fn(u32),
    pub connect: unsafe extern "C" fn(*const u8) -> bool,
    pub disconnect: unsafe extern "C" fn(),
    pub forget: unsafe extern "C" fn(*const u8),
    pub spawn: unsafe extern "C" fn() -> bool,
    pub now: unsafe extern "C" fn() -> u32,
    pub sleep: unsafe extern "C" fn(u32),
    pub memo: unsafe extern "C" fn() -> *mut u32,
    pub default_table: DefaultTable,
    pub profile: *const Profile,
}
struct Bindings {
    services: *const Services,
    config: *mut Config,
}
struct BoundServices(UnsafeCell<Bindings>);
// Initialized before task startup; rebinding requires all runtime calls to stop,
// as with the C++ Host/Config pointers. Concurrent readers never write these pointers.
unsafe impl Sync for BoundServices {}
static BINDINGS: BoundServices = BoundServices(UnsafeCell::new(Bindings {
    services: core::ptr::null(),
    config: core::ptr::null_mut(),
}));
static IN_FLIGHT: AtomicBool = AtomicBool::new(false);
static CANCELLED: AtomicBool = AtomicBool::new(false);
static BUILD_WAITING: AtomicBool = AtomicBool::new(false);
static DEFERRED: AtomicBool = AtomicBool::new(false);
static IDLE_STOPPED: AtomicBool = AtomicBool::new(false);
static REARM: AtomicBool = AtomicBool::new(false);
static HELD: AtomicBool = AtomicBool::new(false);
static REFUSAL: AtomicU8 = AtomicU8::new(0);
static FROM_HOME: AtomicBool = AtomicBool::new(false);
static RESTART_WANTED: AtomicBool = AtomicBool::new(false);
static NOTE: AtomicU8 = AtomicU8::new(0);
// A single starter owns the streak; the main loop reads wanted after the release.
static FRAGMENTED: AtomicU8 = AtomicU8::new(0);

#[derive(Clone, Copy, Default)]
struct Visit {
    id: u32,
    attempted: bool,
    reconnect_armed: bool,
    manual: bool,
    retry_at: u32,
    retries: u8,
}
struct MainState {
    visit: Cell<Visit>,
    cleanup: Cell<u32>,
    idle_since: Cell<u32>,
    idle_clock: Cell<bool>,
    advertisements: Cell<u32>,
    router: Cell<Router>,
    scene: Cell<Scene>,
    pair_addr: Cell<[u8; 18]>,
    pair_name: Cell<[u8; 32]>,
    note_up: Cell<bool>,
    note_since: Cell<u32>,
    connect: Cell<bool>,
}
// Only the main task accesses these cells. Start/render entry points use atomics
// and callbacks only. Cells avoid holding mutable references across host callbacks.
unsafe impl Sync for MainState {}
static MAIN: MainState = MainState {
    visit: Cell::new(Visit {
        id: 0,
        attempted: false,
        reconnect_armed: false,
        manual: false,
        retry_at: 0,
        retries: 0,
    }),
    cleanup: Cell::new(0),
    idle_since: Cell::new(0),
    idle_clock: Cell::new(false),
    advertisements: Cell::new(0),
    router: Cell::new(Router::EMPTY),
    scene: Cell::new(Scene {
        location: 0,
        visit: 0,
        page_shown: false,
        book_indexing: false,
        storage_busy: false,
        wifi_on: false,
        sleeping: false,
        local_key: false,
    }),
    pair_addr: Cell::new([0; 18]),
    pair_name: Cell::new([0; 32]),
    note_up: Cell::new(false),
    note_since: Cell::new(0),
    connect: Cell::new(false),
};
unsafe fn svc() -> &'static Services {
    &*(*BINDINGS.0.get()).services
}
unsafe fn cfg() -> *mut Config {
    (*BINDINGS.0.get()).config
}
unsafe fn now() -> u32 {
    (svc().now)()
}
unsafe fn profile() -> &'static Profile {
    &*svc().profile
}
unsafe fn running() -> bool {
    (svc().running)()
}
unsafe fn stopping() -> bool {
    (svc().stopping)()
}
unsafe fn connected() -> bool {
    (svc().connected)()
}
unsafe fn emit(event: u32, a: u32, b: u32, c: u32, d: u32) {
    if let Some(f) = svc().diagnostic {
        f(event, a, b, c, d);
    }
}
unsafe fn busy() -> bool {
    IN_FLIGHT.load(Acquire) || running() || stopping()
}
unsafe fn restart_event(event: RestartEvent, why: Why) {
    let mut count = FRAGMENTED.load(Relaxed);
    let mut pending = RESTART_WANTED.load(Relaxed);
    let memo = (svc().memo)();
    let previous_magic = *memo;
    let mut magic = previous_magic;
    restart_transition(event, why as u8, &mut count, &mut magic, &mut pending);
    FRAGMENTED.store(count, Relaxed);
    if magic != previous_magic {
        *memo = magic;
    }
    RESTART_WANTED.store(pending, Release);
}
unsafe fn inconclusive() {
    restart_event(RestartEvent::Inconclusive, Why::Ok);
}
unsafe fn refused(why: Why) {
    restart_event(RestartEvent::Refused, why);
}
unsafe fn heap_inputs(phase: u8, heap: Heap, storage_busy: bool) -> RadioInputs {
    RadioInputs {
        phase,
        location: if FROM_HOME.load(Relaxed) { HOME } else { 0 },
        heap,
        storage_busy,
        ..RadioInputs::default()
    }
}
unsafe fn inputs(phase: u8, s: Scene) -> RadioInputs {
    RadioInputs {
        phase,
        enabled: (*cfg()).enabled != 0,
        location: s.location,
        page_shown: s.page_shown,
        book_indexing: s.book_indexing,
        storage_busy: s.storage_busy,
        held_for_build: HELD.load(Relaxed),
        idle_stopped: IDLE_STOPPED.load(Relaxed),
        has_bond: (*cfg()).peer_addr[0] != 0,
        linked: true,
        ..RadioInputs::default()
    }
}
unsafe fn skipped(storage: bool, heap: Heap) {
    emit(
        if storage { 1 } else { 2 },
        heap.free_bytes as u32,
        heap.largest_block as u32,
        0,
        0,
    );
    (svc().heap_map)();
}
unsafe fn begin_owned() -> bool {
    if CANCELLED.load(Acquire) || stopping() {
        return false;
    }
    if running() {
        return true;
    }
    let before = (svc().heap)();
    if verdict(&heap_inputs(1, before, (svc().transfer)()), profile()) == Why::StorageBusy {
        skipped(true, before);
        return false;
    }
    while !(svc().release)() {
        if CANCELLED.load(Acquire) || BUILD_WAITING.load(Acquire) {
            return false;
        }
        (svc().sleep)(10);
    }
    let heap = (svc().heap)();
    let checked = verdict(&heap_inputs(1, heap, false), profile());
    REFUSAL.store(checked as u8, Relaxed);
    if checked != Why::Ok {
        skipped(false, heap);
        refused(checked);
        return false;
    }
    emit(3, heap.free_bytes as u32, heap.largest_block as u32, 0, 0);
    if CANCELLED.load(Acquire) || (svc().transfer)() || !(svc().begin)() {
        inconclusive();
        return false;
    }
    if CANCELLED.load(Acquire) {
        inconclusive();
        (svc().end)(0);
        return false;
    }
    let after = (svc().heap)();
    let started = verdict(&heap_inputs(2, after, (svc().transfer)()), profile());
    REFUSAL.store(started as u8, Relaxed);
    if started == Why::HeapInPieces {
        emit(4, after.free_bytes as u32, after.largest_block as u32, 0, 0);
        refused(started);
    } else if started != Why::Ok {
        inconclusive();
    }
    if started != Why::Ok {
        (svc().end)(0);
        return false;
    }
    emit(5, after.free_bytes as u32, after.largest_block as u32, 0, 0);
    restart_event(RestartEvent::RadioUp, Why::Ok);
    true
}
unsafe fn finish(started: bool, report: bool) {
    if report {
        DEFERRED.store(!started && !CANCELLED.load(Acquire), Relaxed);
    }
    if !running() && !stopping() {
        (svc().hold)(false);
    }
    IN_FLIGHT.store(false, Release);
}
unsafe fn start_sync() -> bool {
    FROM_HOME.store(false, Relaxed);
    IDLE_STOPPED.store(false, Relaxed);
    DEFERRED.store(false, Relaxed);
    if IN_FLIGHT
        .compare_exchange(false, true, AcqRel, Acquire)
        .is_err()
    {
        return false;
    }
    CANCELLED.store(false, Release);
    (svc().hold)(true);
    let started = begin_owned();
    finish(started, false);
    started
}
unsafe fn suspend(timeout: u32) -> bool {
    CANCELLED.store(true, Release);
    if IN_FLIGHT.load(Acquire) {
        return false;
    }
    if running() || stopping() {
        (svc().hold)(true);
        if !(svc().end)(timeout) {
            return false;
        }
    }
    (svc().hold)(false);
    true
}
unsafe fn act(action: u8) -> bool {
    if action == 0 {
        return false;
    }
    let taken = (svc().deliver)(action);
    taken || action == 5 || action == 6
}
unsafe fn drain() {
    let mut raw = RawEdge::default();
    let mut key = KeyPress::default();
    while (svc().pop_raw)(&mut raw) {}
    while (svc().pop_key)(&mut key) {}
}
unsafe fn arm() {
    let mut v = MAIN.visit.get();
    if v.reconnect_armed || stopping() {
        return;
    }
    v.reconnect_armed = true;
    MAIN.visit.set(v);
    if !(svc().arm)((*cfg()).pick, (*cfg()).peer_addr.as_ptr()) {
        emit(6, 0, 0, 0, 0);
    }
}
unsafe fn serve_reader() -> bool {
    let mut acted = false;
    DEFERRED.store(false, Relaxed);
    arm();
    (svc().poll)();
    let mut router = MAIN.router.get();
    let linked = connected();
    if router.linked != linked {
        router = Router {
            linked,
            ..Router::EMPTY
        };
    }
    if router.linked && !router.chosen {
        drain();
        let peer = (svc().linked)();
        router.table = table_for(
            (*cfg()).remotes.as_ptr(),
            (*cfg()).remote_count,
            peer.addr,
            peer.name,
            svc().default_table,
        );
        router.chosen = true;
    }
    MAIN.router.set(router);
    let via_table = !router.table.is_null() && (*router.table).count > 0;
    let mut raw = RawEdge::default();
    while (svc().pop_raw)(&mut raw) {
        if !via_table {
            continue;
        }
        let action = raw_edge(
            &*router.table,
            raw.code(),
            raw.pressed,
            raw.at_ms,
            page_action(&*cfg(), raw.keycode, raw.mods),
            &mut router.wait,
        );
        MAIN.router.set(router);
        acted = act(action) || acted;
        emit(7, raw.code(), u32::from(raw.pressed), u32::from(action), 0);
    }
    if via_table {
        let held = poll_hold(&mut router.wait, now());
        MAIN.router.set(router);
        acted = act(held) || acted;
        if held != 0 {
            emit(8, held.into(), 0, 0, 0);
        }
    }
    let mut key = KeyPress::default();
    while (svc().pop_key)(&mut key) {
        let action = if via_table {
            0
        } else {
            page_action(&*cfg(), key.keycode, key.mods)
        };
        emit(
            9,
            key.keycode.into(),
            key.mods.into(),
            u32::from(key.pressed),
            action.into(),
        );
        if (action == 1 || action == 2) && key.pressed && (svc().deliver)(action) {
            acted = true;
        }
    }
    acted
}
unsafe fn step_note(entered: bool, acknowledged: bool) {
    let v = MAIN.visit.get();
    let input = NoteInputs {
        entered,
        acknowledged,
        reading: MAIN.scene.get().location == READER,
        enabled: (*cfg()).enabled != 0,
        idle_stopped: IDLE_STOPPED.load(Relaxed),
        linked: connected(),
        refused: v.attempted && DEFERRED.load(Relaxed) && !IN_FLIGHT.load(Acquire),
        running_ms: if MAIN.note_up.get() {
            now().wrapping_sub(MAIN.note_since.get())
        } else {
            0
        },
        has_bond: (*cfg()).peer_addr[0] != 0,
    };
    let mut note = next_note(NOTE.load(Relaxed), &input);
    if v.manual && input.refused && note == 2 && matches!(REFUSAL.load(Relaxed), 9 | 10) {
        note = 4;
    }
    NOTE.store(note, Relaxed);
}

/// # Safety
/// Services and Config must remain valid until all radio tasks stop. Call once before
/// any runtime entry, or rebind from the main task with no concurrent runtime calls.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_init(services: *mut Services, config: *mut Config) {
    *BINDINGS.0.get() = Bindings { services, config };
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_start_task() {
    let started = begin_owned();
    finish(started, true);
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_start_async() -> bool {
    IDLE_STOPPED.store(false, Relaxed);
    DEFERRED.store(false, Relaxed);
    if IN_FLIGHT
        .compare_exchange(false, true, AcqRel, Acquire)
        .is_err()
    {
        return false;
    }
    if stopping() || running() {
        let r = running();
        IN_FLIGHT.store(false, Release);
        return r;
    }
    CANCELLED.store(false, Release);
    (svc().hold)(true);
    if !(svc().spawn)() {
        finish(false, true);
        return false;
    }
    true
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_suspend(timeout: u32) -> bool {
    suspend(timeout)
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_stop_idle() -> bool {
    IDLE_STOPPED.store(true, Relaxed);
    DEFERRED.store(false, Relaxed);
    suspend(0)
}
#[no_mangle]
pub extern "C" fn xp_ble_restart_wanted() -> bool {
    RESTART_WANTED.load(Acquire)
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_request_connect() {
    MAIN.connect.set(true);
}
/// # Safety
/// The runtime must be initialized. scene must be readable for this main-task call.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_tick(scene: *const Scene) -> bool {
    let s = *scene;
    let old = MAIN.scene.get();
    let entered = s.location == READER && (old.location != READER || s.visit != old.visit);
    MAIN.scene.set(s);
    if stopping() && now().wrapping_sub(MAIN.cleanup.get()) >= 250 {
        MAIN.cleanup.set(now());
        suspend(0);
    }
    if s.storage_busy {
        MAIN.idle_since.set(0);
        MAIN.idle_clock.set(false);
        suspend(0);
    }
    let reader = s.location == READER;
    let home = s.location == HOME && profile().home_start_largest != 0;
    let mut v = MAIN.visit.get();
    if !(reader || home) || (*cfg()).enabled == 0 || s.visit != v.id {
        if s.visit != v.id {
            HELD.store(false, Relaxed);
        }
        v.attempted = false;
        v.reconnect_armed = false;
        v.manual = false;
        v.retry_at = 0;
        v.retries = 0;
        MAIN.router.set(Router::EMPTY);
        MAIN.idle_since.set(0);
        MAIN.idle_clock.set(false);
    }
    v.id = s.visit;
    let requested = MAIN.connect.get();
    let painted = REARM.swap(false, Relaxed);
    if reader
        && (*cfg()).enabled != 0
        && xp_ble_rearm(IDLE_STOPPED.load(Relaxed), entered, requested, painted)
    {
        IDLE_STOPPED.store(false, Relaxed);
        if !IN_FLIGHT.load(Acquire) && !running() {
            v.attempted = false;
            v.reconnect_armed = false;
            v.retry_at = 0;
            v.retries = 0;
            DEFERRED.store(false, Relaxed);
        }
    }
    if requested {
        MAIN.connect.set(false);
        if reader && (*cfg()).enabled != 0 {
            if connected() {
                NOTE.store(3, Relaxed);
            } else {
                v.manual = true;
                MAIN.note_since.set(now());
                NOTE.store(1, Relaxed);
            }
        }
    }
    MAIN.visit.set(v);
    let mut input = inputs(3, s);
    let must = verdict(&input, profile());
    let mut acted = false;
    if matches!(must, Why::Off | Why::StorageBusy) {
        MAIN.idle_since.set(0);
        MAIN.idle_clock.set(false);
        suspend(0);
    } else {
        if reader
            && v.attempted
            && DEFERRED.load(Relaxed)
            && !running()
            && !IN_FLIGHT.load(Acquire)
            && !IDLE_STOPPED.load(Relaxed)
            && v.retries < 6
        {
            if v.retry_at == 0 {
                v.retry_at = now().wrapping_add(5000);
            } else if now() >= v.retry_at {
                v.retry_at = 0;
                v.retries += 1;
                v.attempted = false;
                emit(10, v.retries.into(), 0, 0, 0);
            }
        } else {
            v.retry_at = 0;
            if running() {
                v.retries = 0;
            }
        }
        MAIN.visit.set(v);
        input.phase = 0;
        if !v.attempted
            && !stopping()
            && verdict(&input, profile()) == Why::Ok
            && (running() || (svc().yield_radio)())
        {
            v.attempted = true;
            MAIN.visit.set(v);
            if !running() {
                FROM_HOME.store(home, Relaxed);
                let started = xp_ble_start_async();
                if !started {
                    DEFERRED.store(true, Relaxed);
                }
                emit(if started { 11 } else { 12 }, 0, 0, 0, 0);
            }
        }
        if !IN_FLIGHT.load(Acquire) && running() {
            input.linked = connected();
            let activity = (svc().activity)();
            let clock = now();
            input.connection_busy = activity.busy;
            if input.linked
                || activity.busy
                || !MAIN.idle_clock.get()
                || activity.advertisements != MAIN.advertisements.get()
            {
                MAIN.idle_since.set(clock);
            }
            MAIN.idle_clock.set(!input.linked && !activity.busy);
            MAIN.advertisements.set(activity.advertisements);
            input.phase = 3;
            input.idle_ms = clock.wrapping_sub(MAIN.idle_since.get());
            if verdict(&input, profile()) == Why::IdleNoLink {
                emit(13, IDLE_MS, 0, 0, 0);
                xp_ble_stop_idle();
                MAIN.idle_since.set(0);
                MAIN.idle_clock.set(false);
            }
        } else {
            MAIN.idle_since.set(0);
            MAIN.idle_clock.set(false);
        }
        if reader && !IN_FLIGHT.load(Acquire) && running() {
            acted = serve_reader();
        }
        if home && !IN_FLIGHT.load(Acquire) && running() {
            arm();
            (svc().poll)();
            drain();
        }
    }
    let up = reader && running() && !IN_FLIGHT.load(Acquire);
    if !up {
        MAIN.note_up.set(false);
    } else if !MAIN.note_up.get() || entered {
        MAIN.note_up.set(true);
        MAIN.note_since.set(now());
    }
    step_note(entered && !requested, false);
    let v = MAIN.visit.get();
    if RESTART_WANTED.load(Acquire)
        && !v.manual
        && (*cfg()).enabled != 0
        && v.attempted
        && reader
        && s.page_shown
        && !s.sleeping
        && !s.storage_busy
        && !busy()
        && !s.wifi_on
    {
        let heap = (svc().heap)();
        emit(14, heap.free_bytes as u32, heap.largest_block as u32, 0, 0);
        restart_event(RestartEvent::Restarting, Why::Ok);
        (svc().restart)();
    }
    acted
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_before_screen(timeout: u32, next_reader: bool) -> bool {
    if next_reader
        && profile().home_start_largest != 0
        && (*cfg()).enabled != 0
        && running()
        && !stopping()
        && !IN_FLIGHT.load(Acquire)
    {
        return true;
    }
    suspend(timeout)
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_stop_now(timeout: u32) -> bool {
    let started = now();
    while !suspend(0) {
        if now().wrapping_sub(started) >= timeout {
            return false;
        }
        (svc().sleep)(20);
    }
    true
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_settle_start() {
    if !IN_FLIGHT.load(Acquire) {
        return;
    }
    let started = now();
    BUILD_WAITING.store(true, Release);
    while IN_FLIGHT.load(Acquire) && now().wrapping_sub(started) < 3000 {
        (svc().sleep)(5);
    }
    BUILD_WAITING.store(false, Release);
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_before_build() -> u8 {
    if (*cfg()).enabled == 0 || IDLE_STOPPED.load(Relaxed) || HELD.load(Relaxed) {
        return 0;
    }
    xp_ble_settle_start();
    if IN_FLIGHT.load(Acquire) {
        return 0;
    }
    HELD.store(true, Relaxed);
    DEFERRED.store(false, Relaxed);
    let heap = (svc().heap)();
    emit(15, heap.free_bytes as u32, heap.largest_block as u32, 0, 0);
    let started = now();
    let mut stopped = suspend(0);
    while !stopped && now().wrapping_sub(started) < 3000 {
        (svc().sleep)(20);
        stopped = suspend(0);
    }
    if !stopped {
        emit(16, 0, 0, 0, 0);
    }
    if stopped {
        1
    } else {
        2
    }
}
#[no_mangle]
pub extern "C" fn xp_ble_after_paint() {
    if HELD.swap(false, Relaxed) {
        REARM.store(true, Relaxed);
    }
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_holds_heap() -> bool {
    (*cfg()).enabled != 0 && (busy() || DEFERRED.load(Relaxed))
}
/// # Safety
/// The runtime must be initialized. out must be writable and exclusively borrowed.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_status(out: *mut Status) {
    *out = Status {
        compiled_in: (svc().compiled_in)(),
        running: running(),
        stopping: stopping(),
        starting: IN_FLIGHT.load(Acquire),
        scanning: (svc().scanning)(),
        connected: connected(),
        connecting: (svc().connecting)(),
        idle_stopped: IDLE_STOPPED.load(Relaxed),
        reader_deferred: DEFERRED.load(Relaxed),
    };
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_why() -> u8 {
    let mut input = inputs(if running() { 3 } else { 0 }, MAIN.scene.get());
    input.linked = connected();
    verdict(&input, profile()) as u8
}
#[no_mangle]
pub extern "C" fn xp_ble_note() -> u8 {
    NOTE.load(Relaxed)
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_acknowledge() {
    step_note(false, true);
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_switch_on() -> bool {
    start_sync()
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_switch_off() -> bool {
    IDLE_STOPPED.store(false, Relaxed);
    DEFERRED.store(false, Relaxed);
    suspend(1000)
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_service() -> bool {
    (svc().poll)();
    let addr = MAIN.pair_addr.get();
    if addr[0] == 0 || !connected() || !str_equal((svc().linked)().addr, addr.as_ptr(), 18) {
        return false;
    }
    (*cfg()).peer_addr = addr;
    (*cfg()).peer_name = MAIN.pair_name.get();
    MAIN.pair_addr.set([0; 18]);
    MAIN.pair_name.set([0; 32]);
    true
}
/// # Safety
/// The runtime must be initialized. Strings must be readable and NUL-terminated;
/// call on the main task. The callbacks must not retain these argument pointers.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_pair(addr: *const u8, name: *const u8) -> bool {
    MAIN.pair_addr.set([0; 18]);
    MAIN.pair_name.set([0; 32]);
    if (*cfg()).enabled == 0 || !start_sync() || !(svc().connect)(addr) {
        return false;
    }
    let mut a = [0; 18];
    let mut n = [0; 32];
    copy_string(&mut a, addr);
    copy_string(&mut n, name);
    MAIN.pair_addr.set(a);
    MAIN.pair_name.set(n);
    true
}
/// # Safety
/// The runtime must be initialized. Strings must be readable and NUL-terminated;
/// call on the main task. The callbacks must not retain these argument pointers.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_forget(addr: *const u8) -> bool {
    (svc().forget)(addr);
    let c = cfg();
    let mut changed = forget_remote((*c).remotes.as_mut_ptr(), &mut (*c).remote_count, addr);
    if str_equal((*c).peer_addr.as_ptr(), addr, 18) {
        (*c).peer_addr[0] = 0;
        (*c).peer_name[0] = 0;
        changed = true;
    }
    changed
}
/// # Safety
/// The runtime must be initialized. out must be writable; call on the main task.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_poll_event(out: *mut Event) -> bool {
    let mut key = KeyPress::default();
    while (svc().pop_key)(&mut key) {}
    let mut raw = RawEdge::default();
    if !(svc().pop_raw)(&mut raw) {
        return false;
    }
    *out = Event {
        code: raw.code(),
        pressed: raw.pressed,
        at_ms: raw.at_ms,
        was_rest: raw.was_rest,
    };
    true
}
/// # Safety
/// The runtime must be initialized and this call must obey the main/render/start
/// task ownership contract in BlePageTurner.h. Callbacks and Config must remain live.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_probe_table() {
    if !connected() {
        let mut r = MAIN.router.get();
        r.table = (svc().default_table)(c"Free3".as_ptr());
        r.chosen = true;
        MAIN.router.set(r);
    }
}
/// # Safety
/// All runtime tasks must be stopped; only the test main thread may reset the state.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_reset() {
    for a in [
        &IN_FLIGHT,
        &CANCELLED,
        &BUILD_WAITING,
        &DEFERRED,
        &IDLE_STOPPED,
        &REARM,
        &HELD,
        &FROM_HOME,
        &RESTART_WANTED,
    ] {
        a.store(false, Relaxed);
    }
    REFUSAL.store(0, Relaxed);
    FRAGMENTED.store(0, Relaxed);
    NOTE.store(0, Relaxed);
    MAIN.visit.set(Visit::default());
    MAIN.cleanup.set(0);
    MAIN.idle_since.set(0);
    MAIN.idle_clock.set(false);
    MAIN.advertisements.set(0);
    MAIN.router.set(Router::EMPTY);
    MAIN.scene.set(Scene::default());
    MAIN.pair_addr.set([0; 18]);
    MAIN.pair_name.set([0; 32]);
    MAIN.note_up.set(false);
    MAIN.note_since.set(0);
    MAIN.connect.set(false);
}

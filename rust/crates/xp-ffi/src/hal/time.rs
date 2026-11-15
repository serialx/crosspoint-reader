const MIN_EPOCH: i64 = 1_735_689_600;
const MAX_EPOCH: i64 = 4_102_444_800;
const MAX_RTC_LEAD: i64 = 31_536_000;
const MIN_ADVANCE: i64 = 60;
#[repr(C)]
pub struct Services {
    now: unsafe extern "C" fn() -> i64,
    read_floor: unsafe extern "C" fn() -> i64,
    write_floor: unsafe extern "C" fn(i64),
    raise_floor: unsafe extern "C" fn(i64) -> i64,
    set_time: unsafe extern "C" fn(i64),
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_time_raise(floor: *mut i64, value: i64) -> i64 {
    let Some(floor) = floor.as_mut() else {
        return 0;
    };
    *floor = (*floor).max(value);
    *floor
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_time_init(services: *const Services) -> bool {
    let Some(s) = services.as_ref() else {
        return false;
    };
    let floor = (s.read_floor)();
    if floor < MIN_EPOCH {
        return false;
    }
    (s.raise_floor)(floor);
    if (s.now)() < floor {
        (s.set_time)(floor);
        return true;
    }
    false
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_time_note(services: *const Services) {
    let Some(s) = services.as_ref() else { return };
    let now = (s.now)();
    if now < MIN_EPOCH {
        return;
    }
    (s.raise_floor)(now);
    if now.wrapping_sub((s.read_floor)()) >= MIN_ADVANCE {
        (s.write_floor)(now);
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_time_adopt(services: *const Services, epoch: i64) {
    let Some(s) = services.as_ref() else { return };
    if !(MIN_EPOCH..=MAX_EPOCH).contains(&epoch) {
        return;
    }
    let stored = (s.read_floor)();
    if stored >= MIN_EPOCH && epoch.wrapping_sub(stored) > MAX_RTC_LEAD {
        return;
    }
    let now = (s.now)();
    let candidate = if (MIN_EPOCH..=MAX_EPOCH).contains(&now) && now > epoch {
        now
    } else {
        epoch
    };
    if candidate < (s.raise_floor)(0) {
        return;
    }
    (s.raise_floor)(candidate);
    (s.set_time)(candidate);
    if candidate.wrapping_sub((s.read_floor)()) >= MIN_ADVANCE {
        (s.write_floor)(candidate);
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_time_now(services: *const Services) -> i64 {
    let Some(s) = services.as_ref() else { return 0 };
    let now = (s.now)();
    let floor = (s.raise_floor)(if now >= MIN_EPOCH { now } else { 0 });
    if floor >= MIN_EPOCH {
        floor
    } else {
        0
    }
}
#[no_mangle]
pub extern "C" fn xp_hal_time_wait(deadline: u32, now: u32) -> bool {
    deadline.wrapping_sub(now) as i32 > 0
}
#[repr(C)]
pub struct SyncServices {
    completed: unsafe extern "C" fn() -> bool,
    millis: unsafe extern "C" fn() -> u32,
    delay: unsafe extern "C" fn(u32),
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_time_sync(
    time: *const Services,
    sync: *const SyncServices,
    timeout_ms: u32,
) -> bool {
    let (Some(_), Some(s)) = (time.as_ref(), sync.as_ref()) else {
        return false;
    };
    let deadline = (s.millis)().wrapping_add(timeout_ms);
    while !(s.completed)() && xp_hal_time_wait(deadline, (s.millis)()) {
        (s.delay)(100);
    }
    let synced = (s.completed)();
    if synced {
        xp_hal_time_note(time);
    }
    synced
}

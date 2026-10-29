//! Reader gestures retain lazy mapped-frame probes, including their side effects.
use crate::model::{BACK, LEFT, PAGE_BACK, PAGE_FORWARD, POWER, RIGHT, SWIPE_LEFT, SWIPE_RIGHT};
use core::ffi::c_void;

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum ButtonQuery {
    Press,
    Release,
    Long,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    HasTouch,
    Swipe,
    HeldMs,
    TouchHeldMs,
    TiltNext,
    TiltPrev,
    NavSwapped,
    HomeAction,
    BackGesture,
    MenuGesture,
    MenuSwipeUp,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub button: Option<unsafe extern "C" fn(*mut c_void, u32, u8, u32) -> u8>,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub tap: Option<unsafe extern "C" fn(*mut c_void, *mut i32, *mut i32) -> u8>,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Config {
    pub long_press: u8,
    pub short_power: u8,
    pub tilt: u8,
    pub touch_controls: u8,
    pub next_gesture: u8,
    pub prev_gesture: u8,
    pub menu_gesture: u8,
    pub back_short_browser: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Turn {
    pub held_ms: u32,
    pub prev: u8,
    pub next: u8,
    pub from_tilt: u8,
    pub reserved: u8,
}
struct Probe {
    context: *mut c_void,
    button: unsafe extern "C" fn(*mut c_void, u32, u8, u32) -> u8,
    read: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    tap: unsafe extern "C" fn(*mut c_void, *mut i32, *mut i32) -> u8,
}
impl Probe {
    fn new(services: Services) -> Option<Self> {
        Some(Self {
            context: services.context,
            button: services.button?,
            read: services.read?,
            tap: services.tap?,
        })
    }
    fn read(&self, query: Query) -> u32 {
        unsafe { (self.read)(self.context, query as u8) }
    }
    fn button(&self, mask: u32, query: ButtonQuery, threshold: u32) -> bool {
        unsafe { (self.button)(self.context, mask, query as u8, threshold) != 0 }
    }
    fn tap(&self) -> Option<(i32, i32)> {
        let (mut x, mut y) = (0, 0);
        (unsafe { (self.tap)(self.context, &mut x, &mut y) } != 0).then_some((x, y))
    }
}
/// # Safety
/// Callbacks/context remain valid for synchronous probes and do not retain output pointers.
pub unsafe fn buttons(services: Services, config: Config) -> Turn {
    let Some(probe) = Probe::new(services) else {
        return Turn::default();
    };
    let use_press = config.long_press == 0;
    let tilt_next = config.tilt != 0 && probe.read(Query::TiltNext) != 0;
    let tilt_prev = config.tilt != 0 && probe.read(Query::TiltPrev) != 0;
    let swapped = probe.read(Query::NavSwapped) != 0;
    let (prev_button, next_button) = if swapped {
        (RIGHT, LEFT)
    } else {
        (LEFT, RIGHT)
    };
    let triggered = |button| {
        if use_press {
            probe.button(button, ButtonQuery::Press, 0)
        } else {
            probe.button(button, ButtonQuery::Long, 700)
                || probe.button(button, ButtonQuery::Release, 0)
        }
    };
    let prev = tilt_prev || triggered(PAGE_BACK) || triggered(prev_button);
    let power = config.short_power == 2 && probe.button(POWER, ButtonQuery::Release, 0);
    let next = probe.read(Query::HomeAction) == 2
        || tilt_next
        || triggered(PAGE_FORWARD)
        || power
        || triggered(next_button);
    Turn {
        prev: u8::from(prev),
        next: u8::from(next),
        from_tilt: u8::from(tilt_prev || tilt_next),
        ..Turn::default()
    }
}
pub fn gesture(value: u8) -> u8 {
    u8::from(matches!(value, 0 | 1 | 3)) | (u8::from(matches!(value, 0 | 2)) << 1)
}
fn center(x: i32, y: i32, width: i32, height: i32) -> bool {
    x >= width / 3 && x < width - width / 3 && y >= height / 3 && y < height - height / 3
}
/// # Safety
/// Same callback contract as buttons.
pub unsafe fn touch(
    services: Services,
    config: Config,
    width: i32,
    height: i32,
    rtl: bool,
) -> Turn {
    let Some(probe) = Probe::new(services) else {
        return Turn::default();
    };
    if config.touch_controls == 0 || probe.read(Query::HasTouch) == 0 {
        return Turn::default();
    }
    let swipe = probe.read(Query::Swipe);
    if swipe != 0 {
        let (next, prev) = if rtl {
            (SWIPE_RIGHT, SWIPE_LEFT)
        } else {
            (SWIPE_LEFT, SWIPE_RIGHT)
        };
        return Turn {
            next: u8::from(swipe == next as u32 && gesture(config.next_gesture) & 2 != 0),
            prev: u8::from(swipe == prev as u32 && gesture(config.prev_gesture) & 2 != 0),
            ..Turn::default()
        };
    }
    let next_taps = gesture(config.next_gesture) & 1 != 0;
    let prev_taps = gesture(config.prev_gesture) & 1 != 0;
    if !next_taps && !prev_taps {
        return Turn::default();
    }
    let Some((x, y)) = probe.tap() else {
        return Turn::default();
    };
    if config.menu_gesture == 1 && center(x, y, width, height) {
        return Turn::default();
    }
    let inverted = (config.next_gesture == 3 || config.prev_gesture == 3) != rtl;
    let next_zone = if inverted {
        x < width.saturating_mul(2) / 3
    } else {
        x >= width / 3
    };
    Turn {
        next: u8::from(next_taps && (!prev_taps || next_zone)),
        prev: u8::from(prev_taps && (!next_taps || !next_zone)),
        held_ms: probe.read(Query::TouchHeldMs),
        ..Turn::default()
    }
}
fn menu_tap(probe: &Probe, gesture: u8, width: i32, height: i32) -> bool {
    if probe.read(Query::HasTouch) == 0 || gesture != 1 {
        return false;
    }
    probe
        .tap()
        .is_some_and(|(x, y)| center(x, y, width, height))
}
/// # Safety
/// Same callback contract as buttons.
pub unsafe fn menu(services: Services, gesture: u8, width: i32, height: i32) -> bool {
    let Some(probe) = Probe::new(services) else {
        return false;
    };
    if probe.read(Query::HasTouch) == 0 {
        return false;
    }
    probe.read(Query::MenuGesture) != 0
        || (gesture == 2 && probe.read(Query::MenuSwipeUp) != 0)
        || menu_tap(&probe, gesture, width, height)
}
/// # Safety
/// Same callback contract as buttons.
pub unsafe fn back(services: Services, short_browser: bool) -> u8 {
    let Some(probe) = Probe::new(services) else {
        return 0;
    };
    if probe.read(Query::BackGesture) != 0 {
        return 0;
    }
    if !probe.button(BACK, ButtonQuery::Long, 1000) && !probe.button(BACK, ButtonQuery::Release, 0)
    {
        return 0;
    }
    if (probe.read(Query::HeldMs) >= 1000) != short_browser {
        1
    } else {
        2
    }
}
pub fn language_rtl(tag: &[u8]) -> bool {
    if tag.len() < 2 || (tag.len() > 2 && !matches!(tag[2], b'-' | b'_')) {
        return false;
    }
    matches!(
        (tag[0].to_ascii_lowercase(), tag[1].to_ascii_lowercase()),
        (b'h', b'e') | (b'i', b'w') | (b'a', b'r') | (b'f', b'a')
    )
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ChapterDisplay {
    pub display_page: i32,
    pub fraction: f32,
    pub has_total: u8,
    pub reserved: [u8; 3],
}
pub fn chapter_display(page: i32, total: i32) -> ChapterDisplay {
    ChapterDisplay {
        display_page: page.wrapping_add(1),
        fraction: if total > 0 {
            page as f32 / total as f32
        } else {
            0.0
        },
        has_total: u8::from(total > 0),
        reserved: [0; 3],
    }
}
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(
        size_of::<Services>() == 4 * size_of::<usize>()
            && align_of::<Services>() == align_of::<usize>()
    );
    assert!(
        offset_of!(Services, context) == 0 && offset_of!(Services, button) == size_of::<usize>()
    );
    assert!(
        offset_of!(Services, read) == 2 * size_of::<usize>()
            && offset_of!(Services, tap) == 3 * size_of::<usize>()
    );
    assert!(size_of::<Config>() == 8 && align_of::<Config>() == 1);
    assert!(offset_of!(Config, long_press) == 0 && offset_of!(Config, short_power) == 1);
    assert!(offset_of!(Config, tilt) == 2 && offset_of!(Config, touch_controls) == 3);
    assert!(offset_of!(Config, next_gesture) == 4 && offset_of!(Config, prev_gesture) == 5);
    assert!(offset_of!(Config, menu_gesture) == 6 && offset_of!(Config, back_short_browser) == 7);
    assert!(size_of::<Turn>() == 8 && align_of::<Turn>() == 4);
    assert!(
        offset_of!(Turn, held_ms) == 0
            && offset_of!(Turn, prev) == 4
            && offset_of!(Turn, next) == 5
    );
    assert!(offset_of!(Turn, from_tilt) == 6 && offset_of!(Turn, reserved) == 7);
    assert!(size_of::<ChapterDisplay>() == 12 && align_of::<ChapterDisplay>() == 4);
    assert!(
        offset_of!(ChapterDisplay, display_page) == 0 && offset_of!(ChapterDisplay, fraction) == 4
    );
    assert!(
        offset_of!(ChapterDisplay, has_total) == 8 && offset_of!(ChapterDisplay, reserved) == 9
    );
};

pub fn toolbar_reset(state: &mut crate::model::ListState) {
    *state = crate::model::ListState::new(0);
}
pub fn toolbar_page_rows(state: &crate::model::ListState) -> i32 {
    if state.drawn > 0 {
        state.drawn
    } else {
        state.visible
    }
}
pub fn toolbar_scroll(state: &mut crate::model::ListState, delta: i32, count: i32) -> bool {
    let trusts = state.drawn > 0 && (state.drawn_count == 0 || state.drawn_count == count);
    let page = if trusts && state.drawn < state.visible {
        state.drawn
    } else {
        state.visible
    };
    let max_top = count.saturating_sub(page).max(0);
    let next = state.top.saturating_add(delta).clamp(0, max_top);
    if next == state.top {
        return false;
    }
    state.top = next;
    true
}

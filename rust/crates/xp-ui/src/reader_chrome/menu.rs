//! Reader menu decisions; platform bindings apply effects after the model lock.
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct State {
    pub orientation: u8,
    pub page_turn: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Result {
    pub action: i32,
    pub orientation: u8,
    pub page_turn: u8,
    pub cancelled: u8,
    pub effect: u8,
}
#[repr(u8)]
pub enum Effect {
    None,
    ShowOrientation,
    ShowAutoTurn,
    ToggleNight,
    ToggleBluetooth,
    ToggleFrontlight,
    Finish,
    ApplyOrientation,
    Redraw,
}
pub const NIGHT: i32 = 3;
pub const FRONTLIGHT: i32 = 4;
pub const BLUETOOTH: i32 = 5;
pub const AUTO_TURN: i32 = 7;
pub const ORIENTATION: i32 = 8;
pub const LAST_ACTION: i32 = 18;
impl State {
    pub fn new(orientation: u8) -> Self {
        Self {
            orientation: if orientation <= 3 { orientation } else { 0 },
            ..Self::default()
        }
    }
    fn result(self, action: i32, effect: Effect, cancelled: bool) -> Result {
        Result {
            action,
            orientation: self.orientation,
            page_turn: self.page_turn,
            cancelled: u8::from(cancelled),
            effect: effect as u8,
        }
    }
    pub fn invalid(self) -> Result {
        self.result(-1, Effect::None, true)
    }
    pub fn activate(self, action: i32) -> Result {
        if !(0..=LAST_ACTION).contains(&action) {
            return self.invalid();
        }
        let effect = match action {
            ORIENTATION => Effect::ShowOrientation,
            AUTO_TURN => Effect::ShowAutoTurn,
            NIGHT => Effect::ToggleNight,
            BLUETOOTH => Effect::ToggleBluetooth,
            FRONTLIGHT => Effect::ToggleFrontlight,
            _ => Effect::Finish,
        };
        self.result(action, effect, false)
    }
    pub fn cancel(self) -> Result {
        self.result(-1, Effect::Finish, true)
    }
    pub fn choose(&mut self, option: u8, selection: i32) -> Result {
        let effect = match option {
            0 if (0..4).contains(&selection) => {
                self.orientation = selection as u8;
                Effect::ApplyOrientation
            }
            1 if (0..5).contains(&selection) => {
                self.page_turn = selection as u8;
                Effect::Redraw
            }
            _ => return self.invalid(),
        };
        self.result(-1, effect, false)
    }
}
const _: () = {
    assert!(
        NIGHT == 3
            && FRONTLIGHT == 4
            && BLUETOOTH == 5
            && AUTO_TURN == 7
            && ORIENTATION == 8
            && LAST_ACTION == 18
    );
    assert!(Effect::None as u8 == 0);
    assert!(Effect::ShowOrientation as u8 == 1);
    assert!(Effect::ShowAutoTurn as u8 == 2);
    assert!(Effect::ToggleNight as u8 == 3);
    assert!(Effect::ToggleBluetooth as u8 == 4);
    assert!(Effect::ToggleFrontlight as u8 == 5);
    assert!(Effect::Finish as u8 == 6);
    assert!(Effect::ApplyOrientation as u8 == 7);
    assert!(Effect::Redraw as u8 == 8);
    assert!(core::mem::size_of::<State>() == 4);
    assert!(core::mem::align_of::<State>() == 1);
    assert!(core::mem::offset_of!(State, orientation) == 0);
    assert!(core::mem::offset_of!(State, page_turn) == 1);
    assert!(core::mem::offset_of!(State, reserved) == 2);
    assert!(core::mem::size_of::<Result>() == 8);
    assert!(core::mem::align_of::<Result>() == 4);
    assert!(core::mem::offset_of!(Result, action) == 0);
    assert!(core::mem::offset_of!(Result, orientation) == 4);
    assert!(core::mem::offset_of!(Result, page_turn) == 5);
    assert!(core::mem::offset_of!(Result, cancelled) == 6);
    assert!(core::mem::offset_of!(Result, effect) == 7);
};

//! Home-key timing and transfer deferral; physical mapping stays in the platform.

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum HomeAction {
    Home = 0,
    #[default]
    Ignore = 1,
    NextPage = 2,
    Refresh = 3,
    Footnotes = 4,
    Confirm = 5,
    Sync = 6,
    Bookmark = 7,
    Dictionary = 8,
    ReaderMenu = 9,
    ToggleFrontlight = 10,
    CreateClipping = 11,
}

impl TryFrom<u8> for HomeAction {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        Ok(match value {
            0 => Self::Home,
            1 => Self::Ignore,
            2 => Self::NextPage,
            3 => Self::Refresh,
            4 => Self::Footnotes,
            5 => Self::Confirm,
            6 => Self::Sync,
            7 => Self::Bookmark,
            8 => Self::Dictionary,
            9 => Self::ReaderMenu,
            10 => Self::ToggleFrontlight,
            11 => Self::CreateClipping,
            _ => return Err(()),
        })
    }
}

#[derive(Clone, Copy)]
pub struct HomeBindings {
    pub tap: HomeAction,
    pub double_tap: HomeAction,
    pub long_press: HomeAction,
}

#[derive(Clone, Copy, Default)]
pub struct HomeContact {
    pub tapped: bool,
    pub long_pressed: bool,
    pub swiped: bool,
    pub pressed: bool,
}

#[derive(Default)]
enum TapState {
    #[default]
    Idle,
    Waiting {
        since: u32,
        second_contact: bool,
    },
}

#[derive(Default)]
pub struct HomeButton {
    tap: TapState,
}

impl HomeButton {
    pub const DOUBLE_TAP_MS: u32 = 350;

    pub fn reset(&mut self) {
        self.tap = TapState::Idle;
    }

    pub fn update(&mut self, now: u32, contact: HomeContact, bindings: HomeBindings) -> HomeAction {
        if contact.swiped {
            self.reset();
            return HomeAction::Ignore;
        }
        if contact.long_pressed {
            self.reset();
            return bindings.long_press;
        }
        if let TapState::Waiting {
            since,
            second_contact,
        } = &mut self.tap
        {
            if !*second_contact && now.wrapping_sub(*since) > Self::DOUBLE_TAP_MS {
                self.tap = if contact.tapped {
                    TapState::Waiting {
                        since: now,
                        second_contact: false,
                    }
                } else {
                    TapState::Idle
                };
                return bindings.tap;
            }
            if contact.pressed {
                *second_contact = true;
            }
        }
        if !contact.tapped {
            return HomeAction::Ignore;
        }
        if bindings.double_tap == HomeAction::Ignore {
            self.reset();
            return bindings.tap;
        }
        match self.tap {
            TapState::Waiting { .. } => {
                self.reset();
                bindings.double_tap
            }
            TapState::Idle => {
                self.tap = TapState::Waiting {
                    since: now,
                    second_contact: false,
                };
                HomeAction::Ignore
            }
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Feedback {
    #[default]
    None,
    Tap,
    LongPress,
}

#[derive(Default)]
pub struct HomeInput {
    button: HomeButton,
    deferred: Option<HomeAction>,
    action: HomeAction,
}

impl HomeInput {
    pub fn action(&self) -> HomeAction {
        self.action
    }

    pub fn reset(&mut self) {
        self.button.reset();
        self.deferred = None;
        // Navigation cancels future actions, not the already sampled frame.
    }

    /// Haptics belong to this contact, before power fallback or deferred replay.
    pub fn update(
        &mut self,
        now: u32,
        contact: Option<HomeContact>,
        bindings: HomeBindings,
        power_action: HomeAction,
        defer: bool,
    ) -> Feedback {
        let feedback = self.sample_contact(now, contact, bindings);
        self.finish_frame(power_action, defer);
        feedback
    }

    pub fn sample_contact(
        &mut self,
        now: u32,
        contact: Option<HomeContact>,
        bindings: HomeBindings,
    ) -> Feedback {
        self.action = HomeAction::Ignore;
        let mut feedback = Feedback::None;
        if let Some(contact) = contact {
            self.action = self.button.update(now, contact, bindings);
            if self.action != HomeAction::Ignore {
                feedback = if contact.long_pressed {
                    Feedback::LongPress
                } else {
                    Feedback::Tap
                };
            }
        }
        feedback
    }

    pub fn finish_frame(&mut self, power_action: HomeAction, defer: bool) {
        if self.action == HomeAction::Ignore {
            self.action = power_action;
        }
        if defer {
            if self.action != HomeAction::Ignore && self.deferred.is_none() {
                self.deferred = Some(self.action);
            }
        } else if let Some(action) = self.deferred.take() {
            self.action = action;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use HomeAction as A;

    const BINDINGS: HomeBindings = HomeBindings {
        tap: A::Home,
        double_tap: A::ToggleFrontlight,
        long_press: A::ReaderMenu,
    };

    #[test]
    fn native_home_gesture_sequence() {
        let mut input = HomeButton::default();
        // Same ordered sequence as test/home_button/HomeButtonInputTest.cpp.
        let cases = [
            (0, true, false, false, false, A::Ignore),
            (350, false, false, false, false, A::Ignore),
            (351, false, false, false, false, A::Home),
            (352, false, false, false, false, A::Ignore),
            (1000, true, false, false, false, A::Ignore),
            (1350, true, false, false, false, A::ToggleFrontlight),
            (1701, false, false, false, false, A::Ignore),
            (2000, false, true, false, false, A::ReaderMenu),
            (2001, false, false, false, false, A::Ignore),
            (3000, true, false, false, false, A::Ignore),
            (3200, false, false, false, true, A::Ignore),
            (3400, false, false, false, false, A::Ignore),
            (3900, false, true, false, false, A::ReaderMenu),
            (4000, false, false, false, false, A::Ignore),
            (5000, true, false, false, false, A::Ignore),
            (5100, true, false, true, false, A::Ignore),
            (5500, false, false, false, false, A::Ignore),
            (6000, true, false, false, false, A::Ignore),
        ];
        for (now, tapped, long_pressed, swiped, pressed, expected) in cases {
            assert_eq!(
                input.update(
                    now,
                    HomeContact {
                        tapped,
                        long_pressed,
                        swiped,
                        pressed
                    },
                    BINDINGS
                ),
                expected,
                "at {now}"
            );
        }
        input.reset();
        assert_eq!(
            input.update(6500, HomeContact::default(), BINDINGS),
            A::Ignore
        );
        for (now, tapped, expected) in [
            (u32::MAX - 100, true, A::Ignore),
            (100, true, A::ToggleFrontlight),
            (u32::MAX - 100, true, A::Ignore),
            (251, false, A::Home),
            (7000, true, A::Ignore),
            (7400, true, A::Home),
            (7751, false, A::Home),
        ] {
            assert_eq!(
                input.update(
                    now,
                    HomeContact {
                        tapped,
                        ..HomeContact::default()
                    },
                    BINDINGS
                ),
                expected,
                "at {now}"
            );
        }
        for (now, contact, bindings, expected) in [
            (
                8000,
                HomeContact {
                    tapped: true,
                    ..HomeContact::default()
                },
                HomeBindings {
                    tap: A::Bookmark,
                    double_tap: A::Ignore,
                    long_press: A::ReaderMenu,
                },
                A::Bookmark,
            ),
            (
                9000,
                HomeContact {
                    tapped: true,
                    ..HomeContact::default()
                },
                HomeBindings {
                    tap: A::CreateClipping,
                    double_tap: A::Ignore,
                    long_press: A::Home,
                },
                A::CreateClipping,
            ),
            (
                10000,
                HomeContact {
                    tapped: true,
                    ..HomeContact::default()
                },
                HomeBindings {
                    tap: A::Home,
                    double_tap: A::CreateClipping,
                    long_press: A::Ignore,
                },
                A::Ignore,
            ),
            (
                10100,
                HomeContact {
                    tapped: true,
                    ..HomeContact::default()
                },
                HomeBindings {
                    tap: A::Home,
                    double_tap: A::CreateClipping,
                    long_press: A::Ignore,
                },
                A::CreateClipping,
            ),
            (
                11000,
                HomeContact {
                    long_pressed: true,
                    ..HomeContact::default()
                },
                HomeBindings {
                    tap: A::Home,
                    double_tap: A::Ignore,
                    long_press: A::CreateClipping,
                },
                A::CreateClipping,
            ),
        ] {
            assert_eq!(input.update(now, contact, bindings), expected, "at {now}");
        }
        assert_eq!(A::ToggleFrontlight as u8, 10);
        for value in 0..=11 {
            assert_eq!(A::try_from(value).unwrap() as u8, value);
        }
        assert!(A::try_from(12).is_err());
        assert!(A::try_from(255).is_err());
    }

    #[test]
    fn transfer_keeps_first_action_without_replaying_its_haptic() {
        let mut input = HomeInput::default();
        let bindings = HomeBindings {
            double_tap: A::Ignore,
            ..BINDINGS
        };
        assert_eq!(
            input.update(
                1,
                Some(HomeContact {
                    tapped: true,
                    ..HomeContact::default()
                }),
                bindings,
                A::Ignore,
                true
            ),
            Feedback::Tap
        );
        assert_eq!(input.action(), A::Home);
        assert_eq!(
            input.update(
                2,
                Some(HomeContact {
                    long_pressed: true,
                    ..HomeContact::default()
                }),
                bindings,
                A::Ignore,
                true
            ),
            Feedback::LongPress
        );
        assert_eq!(input.action(), A::ReaderMenu);
        assert_eq!(
            input.update(3, None, bindings, A::Bookmark, false),
            Feedback::None
        );
        assert_eq!(input.action(), A::Home);
        assert_eq!(
            input.update(4, None, bindings, A::Ignore, false),
            Feedback::None
        );
        assert_eq!(input.action(), A::Ignore);
    }

    #[test]
    fn navigation_clears_pending_actions_but_keeps_current_frame() {
        let mut input = HomeInput::default();
        input.update(1, None, BINDINGS, A::Bookmark, true);
        input.reset();
        assert_eq!(input.action(), A::Bookmark);
        input.update(2, None, BINDINGS, A::Ignore, false);
        assert_eq!(input.action(), A::Ignore);
    }
}

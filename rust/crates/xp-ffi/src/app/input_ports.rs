use super::{abi, App, Feedback, HomeAction, HomeBindings, HomeContact};

#[cfg(not(feature = "app-test"))]
#[repr(u8)]
// Send in order; only Poll returns a sample. Feedback and Finish return zero.
enum PollPhase {
    Poll,
    Feedback,
    Finish,
}

impl App {
    pub(super) fn poll_input(&self, shared_power: bool) -> abi::MappedSnapshot {
        #[cfg(feature = "app-test")]
        unsafe {
            (self.runtime.input_shared_power_mode)(u8::from(shared_power));
        }
        #[cfg(not(feature = "app-test"))]
        unsafe {
            let sample = (self.runtime.input_poll)(PollPhase::Poll as u8, u8::from(shared_power));
            crate::hal::gpio::xp_hal_input_update(sample.state, sample.buttons, sample.usb);
            (self.runtime.input_poll)(PollPhase::Feedback as u8, 0);
            self.input_polled(false);
            (self.runtime.input_poll)(PollPhase::Finish as u8, 0);
        }
        self.snapshot(true, false, false)
    }

    pub(super) fn snapshot(
        &self,
        poll: bool,
        full_buttons: bool,
        consume_release: bool,
    ) -> abi::MappedSnapshot {
        let mut frame = core::mem::MaybeUninit::uninit();
        unsafe {
            #[cfg(feature = "app-test")]
            (self.runtime.input_mapped_snapshot)(
                u8::from(poll),
                u8::from(full_buttons),
                u8::from(consume_release),
                frame.as_mut_ptr(),
            );
            #[cfg(not(feature = "app-test"))]
            {
                let home = self.input.borrow().action() as u8;
                (self.runtime.input_mapped_query)(
                    u8::from(poll),
                    u8::from(full_buttons),
                    u8::from(consume_release),
                    home,
                    frame.as_mut_ptr(),
                );
            }
            frame.assume_init()
        }
    }

    pub(super) fn input_polled(&self, defer: bool) {
        let frame = self.snapshot(false, false, false);
        let settings = self.settings();
        let action = |value| HomeAction::try_from(value).unwrap_or(HomeAction::Ignore);
        let bindings = HomeBindings {
            tap: action(settings.home_tap),
            double_tap: action(settings.home_double),
            long_press: action(settings.home_long),
        };
        let contact = (frame.has_home_key != 0).then_some(HomeContact {
            tapped: frame.home_key_tapped != 0,
            long_pressed: frame.home_key_long_pressed != 0,
            swiped: frame.input.swipe != 0,
            pressed: frame.home_key_pressed != 0,
        });
        let feedback =
            self.input
                .borrow_mut()
                .sample_contact(frame.input.now_ms, contact, bindings);
        match feedback {
            Feedback::None => {}
            Feedback::Tap => unsafe { (self.runtime.touch_feedback)(0) },
            Feedback::LongPress => unsafe { (self.runtime.touch_feedback)(1) },
        }
        let power = if frame.power_released != 0 {
            match settings.short_power {
                7 => HomeAction::Bookmark,
                8 => HomeAction::Sync,
                9 => HomeAction::ReaderMenu,
                _ => HomeAction::Ignore,
            }
        } else {
            HomeAction::Ignore
        };
        self.input.borrow_mut().finish_frame(power, defer);
    }
}

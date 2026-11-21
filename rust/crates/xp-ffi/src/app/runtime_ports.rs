use super::abi::Runtime;

impl Runtime {
    pub(super) unsafe fn idle_wait(&self, duration: u32, slice: u32) {
        #[cfg(feature = "app-test")]
        (self.idle_wait)(duration, slice);
        #[cfg(not(feature = "app-test"))]
        {
            let started = (self.now_ms)();
            while (self.now_ms)().wrapping_sub(started) < duration {
                (self.delay_ms)(slice);
                let mut sample = core::mem::MaybeUninit::uninit();
                (self.raw_input_sample)(sample.as_mut_ptr());
                let sample = sample.assume_init();
                if sample.power != 0
                    || sample.auxiliary != 0
                    || crate::hal::gpio::xp_hal_input_adc(sample.first, sample.second)
                {
                    break;
                }
            }
        }
    }

    pub(super) unsafe fn power_saving(&self, enabled: u8) {
        #[cfg(feature = "app-test")]
        (self.power_saving)(enabled);
        #[cfg(not(feature = "app-test"))]
        {
            let mut context = core::mem::MaybeUninit::uninit();
            (self.power_context)(context.as_mut_ptr());
            let context = context.assume_init();
            crate::hal::power::xp_hal_power_set(context.state, &context.services, enabled != 0);
        }
    }

    pub(super) unsafe fn tilt_update(&self, enabled: u8, orientation: u8, reader: u8) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.tilt_update)(enabled, orientation, reader)
        }
        #[cfg(not(feature = "app-test"))]
        {
            let mut context = core::mem::MaybeUninit::uninit();
            (self.tilt_context)(context.as_mut_ptr());
            let context = context.assume_init();
            crate::hal::tilt::xp_hal_tilt_update(
                context.state,
                &context.services,
                enabled,
                orientation,
                reader != 0,
            );
            u8::from(crate::hal::tilt::xp_hal_tilt_take(context.state, 2))
        }
    }

    pub(super) unsafe fn frontlight_state(&self) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.frontlight_state)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            u8::from((*((self.light_state)())).is_on())
        }
    }

    pub(super) unsafe fn frontlight_set(&self, enabled: u8) {
        #[cfg(feature = "app-test")]
        (self.frontlight_set)(enabled);
        #[cfg(not(feature = "app-test"))]
        {
            let state = (self.light_state)();
            if crate::hal::xp_hal_light_on(state, enabled != 0) {
                (self.light_output)(crate::hal::xp_hal_light_output(state));
            }
        }
    }
}

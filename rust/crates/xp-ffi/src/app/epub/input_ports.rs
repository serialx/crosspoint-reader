use super::{abi::XpEpubLeaves, EpubSession};
use xp_ui::reader::input::Services;

#[cfg(not(feature = "app-test"))]
#[repr(C)]
struct QueryContext {
    mapped: *const core::ffi::c_void,
    tilt: *mut crate::hal::tilt::Tilt,
    home: u8,
}

impl EpubSession {
    // Cache only the boot-lifetime table address. Every probe reads live input.
    pub(super) fn input_services(&self) -> Services {
        #[cfg(feature = "app-test")]
        return **self
            .input_services
            .get_or_init(|| unsafe { &*(self.leaves.input_services)() });
        #[cfg(not(feature = "app-test"))]
        {
            self.input_sources();
            Services {
                context: (self as *const Self).cast_mut().cast(),
                button: Some(button),
                read: Some(read),
                tap: Some(tap),
            }
        }
    }

    #[cfg(not(feature = "app-test"))]
    fn input_sources(&self) -> &'static Services {
        self.input_services
            .get_or_init(|| unsafe { &*(self.leaves.input_sources)() })
    }
}

#[cfg(not(feature = "app-test"))]
unsafe fn query(context: *mut core::ffi::c_void) -> (&'static Services, QueryContext) {
    let sources = (&*context.cast::<EpubSession>()).input_sources();
    (
        sources,
        QueryContext {
            mapped: sources.context,
            tilt: core::ptr::null_mut(),
            home: crate::app::home_action(),
        },
    )
}

#[cfg(not(feature = "app-test"))]
unsafe extern "C" fn button(
    context: *mut core::ffi::c_void,
    mask: u32,
    kind: u8,
    threshold: u32,
) -> u8 {
    let (sources, mut query) = query(context);
    (sources.button.unwrap())(
        (&mut query as *mut QueryContext).cast(),
        mask,
        kind,
        threshold,
    )
}

#[cfg(not(feature = "app-test"))]
unsafe extern "C" fn read(context: *mut core::ffi::c_void, kind: u8) -> u32 {
    use xp_ui::reader::input::Query;
    let (sources, mut query) = query(context);
    let value = (sources.read.unwrap())((&mut query as *mut QueryContext).cast(), kind);
    match kind {
        x if x == Query::TiltNext as u8 || x == Query::TiltPrev as u8 => u32::from(
            crate::hal::tilt::xp_hal_tilt_take(query.tilt, u8::from(x == Query::TiltPrev as u8)),
        ),
        _ => value,
    }
}

#[cfg(not(feature = "app-test"))]
unsafe extern "C" fn tap(context: *mut core::ffi::c_void, x: *mut i32, y: *mut i32) -> u8 {
    let (sources, mut query) = query(context);
    (sources.tap.unwrap())((&mut query as *mut QueryContext).cast(), x, y)
}

impl XpEpubLeaves {
    pub(crate) unsafe fn input_facts(&self, out: *mut super::abi::XpEpubInputFacts) {
        #[cfg(feature = "app-test")]
        (self.input_facts)(out);
        #[cfg(not(feature = "app-test"))]
        (self.input_facts)(crate::app::home_action(), out);
    }
    pub(crate) unsafe fn input_snapshot(&self, out: *mut xp_ui::model::Input) {
        #[cfg(feature = "app-test")]
        (self.input_snapshot)(out);
        #[cfg(not(feature = "app-test"))]
        (self.input_snapshot)(crate::app::home_action(), out);
    }
}

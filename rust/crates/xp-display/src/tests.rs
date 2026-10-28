use super::*;
use constants::*;

#[derive(Default)]
struct QuietBus(u32);
impl Bus for QuietBus {
    fn cmd(&mut self, _: u8) {
        self.0 += 1;
    }
    fn data8(&mut self, _: u8) {
        self.0 += 1;
    }
    fn data_span(&mut self, _: &[u8]) {
        self.0 += 1;
    }
    fn cmd_data(&mut self, _: u8, _: &[u8]) {
        self.0 += 1;
    }
    fn txn_begin(&mut self) {
        self.0 += 1;
    }
    fn txn_cmd(&mut self, _: u8) {
        self.0 += 1;
    }
    fn txn_data(&mut self, _: u8) {
        self.0 += 1;
    }
    fn txn_write(&mut self, _: &[u8]) {
        self.0 += 1;
    }
    fn txn_end(&mut self) {
        self.0 += 1;
    }
    fn reset(&mut self, _: u16) {
        self.0 += 1;
    }
    fn reset_level(&mut self, _: u8) {
        self.0 += 1;
    }
    fn busy_level(&mut self) -> u8 {
        0
    }
    fn busy_wait(&mut self, _: u32, _: u32) {
        self.0 += 1;
    }
    fn refresh_wait(&mut self, _: u32) {
        self.0 += 1;
    }
    fn delay_ms(&mut self, _: u32) {
        self.0 += 1;
    }
    fn delay_us(&mut self, _: u32) {
        self.0 += 1;
    }
    fn clock_ms(&mut self) -> u32 {
        0
    }
    fn probe(&mut self, _: u32, _: u32) -> u32 {
        0
    }
}

#[test]
fn short_frames_are_rejected_before_io() {
    let mut panel = unsafe { Uc8253::new(16, 2, core::ptr::null()).unwrap() };
    let mut bus = QuietBus::default();
    for operation in [
        OP_DISPLAY,
        OP_DISPLAY_START,
        OP_DISPLAY_WINDOW,
        OP_GRAY_BASE,
        OP_GRAY_LSB,
    ] {
        assert_eq!(
            panel.execute(
                &mut bus,
                Request {
                    operation,
                    data: Some(&[0; 3]),
                    ..Request::default()
                }
            ),
            Err(INVALID)
        );
    }
    assert_eq!(bus.0, 0);
}

#[test]
fn short_strip_is_rejected_before_window_selection() {
    let mut panel = unsafe { Uc8253::new(16, 2, core::ptr::null()).unwrap() };
    let mut bus = QuietBus::default();
    assert_eq!(
        panel.execute(
            &mut bus,
            Request {
                operation: OP_GRAY_STRIP,
                height: 2,
                data: Some(&[0; 3]),
                ..Request::default()
            }
        ),
        Err(INVALID)
    );
    assert_eq!(bus.0, 0);
}

#[test]
fn invalid_geometry_and_lut_pointers_are_rejected() {
    assert!(unsafe { Uc8253::new(15, 2, core::ptr::null()) }.is_err());
    assert!(unsafe { Uc8253::new(16, 0, core::ptr::null()) }.is_err());
    let mut config = default_config();
    config.banks[0][0] = core::ptr::null();
    assert!(unsafe { Uc8253::new(16, 2, &config) }.is_err());
}

#[test]
fn invalid_modes_do_not_start_a_waveform() {
    let mut panel = unsafe { Uc8253::new(16, 2, core::ptr::null()).unwrap() };
    let mut bus = QuietBus::default();
    for operation in [OP_DISPLAY, OP_GRAY_BEGIN, OP_GRAY_STRIP] {
        assert_eq!(
            panel.execute(
                &mut bus,
                Request {
                    operation,
                    mode: 99,
                    data: Some(&[0; 4]),
                    ..Request::default()
                }
            ),
            Err(INVALID)
        );
    }
    assert_eq!(bus.0, 0);
}

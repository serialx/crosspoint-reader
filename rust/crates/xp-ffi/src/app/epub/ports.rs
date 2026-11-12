// Shipping calls go straight to the Rust owners; host oracles retain their fakes.
use super::abi::{XpEpubLeaves, XpEpubPeer, XpEpubRadio, XpEpubRenderSpec};

impl XpEpubLeaves {
    pub(super) unsafe fn radio_status(&self, out: *mut XpEpubRadio, peer: *mut XpEpubPeer) {
        #[cfg(feature = "app-test")]
        (self.radio_status)(out, peer);
        #[cfg(not(feature = "app-test"))]
        {
            let mut status = core::mem::MaybeUninit::uninit();
            crate::ble_runtime::xp_ble_status(status.as_mut_ptr());
            let status = status.assume_init();
            out.write(XpEpubRadio {
                compiled_in: u8::from(status.compiled_in),
                running: u8::from(status.running),
                starting: u8::from(status.starting),
                stopping: u8::from(status.stopping),
                note: crate::ble_runtime::xp_ble_note(),
            });
            if !peer.is_null() {
                (self.radio_peer)(peer);
            }
        }
    }

    pub(super) unsafe fn radio_before_build(&self) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.radio_before_build)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            u8::from(crate::ble_runtime::xp_ble_before_build() != 0)
        }
    }

    pub(super) unsafe fn radio_settle_start(&self) {
        #[cfg(feature = "app-test")]
        {
            (self.radio_settle_start)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            crate::ble_runtime::xp_ble_settle_start()
        }
    }

    pub(super) unsafe fn radio_holds_heap(&self) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.radio_holds_heap)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            u8::from(crate::ble_runtime::xp_ble_holds_heap())
        }
    }

    pub(super) unsafe fn radio_after_paint(&self) {
        #[cfg(feature = "app-test")]
        {
            (self.radio_after_paint)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            crate::ble_runtime::xp_ble_after_paint()
        }
    }

    pub(super) unsafe fn radio_switch_off(&self) {
        #[cfg(feature = "app-test")]
        {
            (self.radio_switch_off)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            crate::ble_runtime::xp_ble_switch_off();
        }
    }

    pub(super) unsafe fn radio_request_connect(&self) {
        #[cfg(feature = "app-test")]
        {
            (self.radio_request_connect)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            crate::ble_runtime::xp_ble_request_connect()
        }
    }

    pub(super) unsafe fn has_exit_subscriber(&self) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.has_exit_subscriber)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            u8::from(crate::plugin::xp_plugin_any(
                (self.subscribers)().cast(),
                crate::plugin::Event::ReaderExit as u32,
            ))
        }
    }

    pub(super) unsafe fn has_session_subscriber(&self) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.has_session_subscriber)()
        }
        #[cfg(not(feature = "app-test"))]
        {
            u8::from(crate::plugin::xp_plugin_any(
                (self.subscribers)().cast(),
                crate::plugin::Event::ReaderSession as u32,
            ))
        }
    }

    pub(super) unsafe fn settings_render_spec(
        &self,
        width: u16,
        height: u16,
        out: *mut XpEpubRenderSpec,
    ) {
        #[cfg(not(feature = "app-test"))]
        let (font, compression) = {
            let mut settings = core::mem::MaybeUninit::uninit();
            (self.settings_snapshot)(1, settings.as_mut_ptr());
            let settings = settings.assume_init();
            (
                settings.font,
                crate::settings::xp_setting_line_compression(
                    settings.font_family.len != 0,
                    settings.line_spacing,
                ),
            )
        };
        (self.settings_render_spec)(width, height, out);
        #[cfg(not(feature = "app-test"))]
        {
            (*out).font = font;
            (*out).compression = compression;
        }
    }
}

// Shipping calls go straight to the Rust owners; host oracles retain their fakes.
use super::abi::{XpEpubLeaves, XpEpubPeer, XpEpubRadio, XpEpubRenderSpec};
use super::app_abi::FontOwner;

impl XpEpubLeaves {
    #[cfg(not(feature = "app-test"))]
    fn event_ports(&self) -> super::events::Ports {
        super::events::Ports {
            subscribers: self.subscribers,
            codec: self.event_codec,
            append: self.event_append,
        }
    }
    pub(super) unsafe fn emit_open(&self, path: super::app_abi::Text) {
        #[cfg(feature = "app-test")]
        (self.emit_open)(path);
        #[cfg(not(feature = "app-test"))]
        super::events::open(&self.event_ports(), path);
    }
    pub(super) unsafe fn emit_exit(
        &self,
        path: super::app_abi::Text,
        percent: i32,
        _end_percent: i32,
    ) {
        #[cfg(feature = "app-test")]
        (self.emit_exit)(path, percent);
        #[cfg(not(feature = "app-test"))]
        super::events::exit(&self.event_ports(), path, percent, _end_percent);
    }
    pub(super) unsafe fn emit_session(
        &self,
        path: super::app_abi::Text,
        document: super::app_abi::Text,
        event: *const super::abi::XpEpubSessionEvent,
    ) {
        #[cfg(feature = "app-test")]
        (self.emit_session)(path, document, event);
        #[cfg(not(feature = "app-test"))]
        super::events::session(&self.event_ports(), path, document, &*event);
    }

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
        _owner: FontOwner,
        width: u16,
        height: u16,
        out: *mut XpEpubRenderSpec,
    ) {
        #[cfg(not(feature = "app-test"))]
        let (font, compression) = {
            let settings = self.settings(_owner, true);
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

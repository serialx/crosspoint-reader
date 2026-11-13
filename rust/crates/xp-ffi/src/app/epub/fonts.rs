use super::{
    abi::{XpEpubLeaves, XpEpubSettings},
    app_abi::FontOwner,
    app_abi::Text,
};

#[cfg(all(not(feature = "app-test"), feature = "sd-font-catalog"))]
include!(concat!(env!("OUT_DIR"), "/builtin_font_ids.rs"));

impl XpEpubLeaves {
    pub(super) unsafe fn settings(&self, _owner: FontOwner, resolve: bool) -> XpEpubSettings {
        let mut out = core::mem::MaybeUninit::uninit();
        (self.settings_snapshot)(u8::from(resolve), out.as_mut_ptr());
        #[allow(unused_mut)]
        let mut settings = out.assume_init();
        #[cfg(all(not(feature = "app-test"), feature = "sd-font-catalog"))]
        if resolve {
            let family = settings.font_family.bytes().unwrap_or_default();
            let loaded = if family.is_empty() {
                0
            } else {
                crate::font_sd::catalog::loaded_id(_owner.get().cast(), family)
            };
            settings.font = xp_core::sdfont::control::reader_font(
                &BUILTIN_FONT_IDS,
                settings.font_size,
                loaded,
            );
        }
        settings
    }

    pub(super) unsafe fn font_variant(&self, _owner: FontOwner, font: i32, scale: f32) -> i32 {
        #[cfg(any(feature = "app-test", not(feature = "sd-font-catalog")))]
        {
            (self.font_variant)(font, scale)
        }
        #[cfg(all(not(feature = "app-test"), feature = "sd-font-catalog"))]
        {
            let id = xp_core::sdfont::control::builtin_variant(&BUILTIN_FONT_IDS, font, scale);
            let owner = _owner.get().cast::<crate::font_registry::Owner>();
            if id != 0 && (*owner).registry.contains(id) {
                id
            } else {
                0
            }
        }
    }

    pub(super) unsafe fn font_ready(&self, _owner: FontOwner, font: i32, fallback: i32) -> i32 {
        #[cfg(any(feature = "app-test", not(feature = "sd-font-catalog")))]
        {
            (self.font_ready)(font, fallback)
        }
        #[cfg(all(not(feature = "app-test"), feature = "sd-font-catalog"))]
        {
            let owner = _owner.get().cast::<crate::font_registry::Owner>();
            if font == fallback || (*owner).registry.contains(font) {
                font
            } else {
                fallback
            }
        }
    }

    pub(super) unsafe fn font_codepoint(
        &self,
        _owner: FontOwner,
        font: i32,
        codepoint: u32,
        style: u8,
    ) -> Option<(i32, i32)> {
        let (mut advance, mut top) = (0, 0);
        #[cfg(feature = "app-test")]
        let found = (self.font_codepoint)(font, codepoint, style, &mut advance, &mut top);
        #[cfg(not(feature = "app-test"))]
        let found = {
            let provider = crate::font_registry::xp_font_registry_provider(_owner.get());
            crate::textm::xp_native_codepoint(
                &provider,
                font,
                codepoint,
                style,
                &mut advance,
                &mut top,
            )
        };
        (found != 0).then_some((advance, top))
    }

    pub(super) unsafe fn font_text_advance(
        &self,
        _owner: FontOwner,
        text: Text,
        font: i32,
        style: u8,
        spacing: i8,
    ) -> i32 {
        #[cfg(feature = "app-test")]
        {
            (self.font_text_advance)(text, font, style, spacing)
        }
        #[cfg(not(feature = "app-test"))]
        {
            let provider = crate::font_registry::xp_font_registry_provider(_owner.get());
            let bytes = text.bytes().unwrap_or_default();
            let len = bytes
                .iter()
                .position(|&byte| byte == 0)
                .unwrap_or(bytes.len());
            crate::textm::xp_native_advance(&provider, font, text.data, len, style, spacing, -1, 1)
        }
    }

    pub(super) unsafe fn font_ascent(&self, _owner: FontOwner, font: i32) -> i32 {
        #[cfg(feature = "app-test")]
        {
            (self.font_ascent)(font)
        }
        #[cfg(not(feature = "app-test"))]
        {
            let owner = _owner.get().cast();
            let data = crate::font_registry::Owner::data(owner, font, 0);
            if data.is_null() {
                crate::font_registry::report(
                    owner,
                    xp_core::textm::native::DiagnosticEvent::MissingFont as u32,
                    font as u32,
                );
                return 0;
            }
            (*data).ascender
        }
    }

    pub(super) unsafe fn hyphen_language(&self, language: Text) -> i32 {
        #[cfg(feature = "app-test")]
        {
            (self.hyphen_language)(language)
        }
        #[cfg(not(feature = "app-test"))]
        {
            let id = xp_core::hyphen::language(language.bytes().unwrap_or_default());
            (self.hyphen_language)(id);
            id
        }
    }
}

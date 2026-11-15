pub mod calibre;
pub mod calibre_view;
pub mod ffi;
pub mod modes;
pub mod modes_owner;
pub mod usb;
pub mod usb_view;
pub mod web;
pub mod web_view;

#[cfg(any(test, feature = "legacy-network-oracles"))]
pub mod legacy_wifi;
mod view;
pub mod wifi;
pub mod wifi_view;

pub mod wifi_owner;
pub mod wifi_physical;
pub mod wifi_resources;

#[cfg(test)]
#[path = "../../../xp-ffi/src/app/stable_owner.rs"]
mod stable_owner;

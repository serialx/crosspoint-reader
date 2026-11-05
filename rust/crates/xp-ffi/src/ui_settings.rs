use xp_ui::settings::{
    hits::Layout as Hits,
    settings_ffi::{self, Services},
    state::{LayoutSelection, QuickResume, Remap},
    view::Layout,
};
use xp_ui::{
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest, ListState},
    scene::Interaction,
};
macro_rules! export {
 ($name:ident($($arg:ident:$ty:ty),*) -> $result:ty = $call:ident) => {
  /// # Safety
  /// Descriptor pointers follow the private settings-family ABI borrowing contract.
  #[no_mangle]
  #[inline(never)]
  pub unsafe extern "C" fn $name($($arg:$ty),*) -> $result { unsafe { settings_ffi::$call($($arg),*) } }
 };
}
export!(xp_ui_settings_list_init(s:*mut ListState,v:i32)->()=init);
export!(xp_ui_settings_list_select(s:*mut ListState,v:i32,n:u16)->()=select);
export!(xp_ui_settings_list_interest(s:*const ListState,t:u32)->Interest=interest);
export!(xp_ui_settings_list_update(s:*mut ListState,i:*const Input,n:u16)->Effect=update);
export!(xp_ui_settings_list_publish(s:*mut ListState,n:*const ListState,v:i32)->()=publish);
export!(xp_ui_settings_hit(h:*const Hits,x:i16,y:i16,o:*mut Interaction)->u8=hit);
export!(xp_ui_settings_view(s:*const ListState,l:*const Layout,r:*const Services,b:*const Buffer,n:*mut ListState,o:*mut SceneView,h:*mut Hits)->u8=view);
export!(xp_ui_settings_quick_resume(s:*mut QuickResume,t:u8,q:u8,c:u8,d:u8)->u8=quick_resume);
export!(xp_ui_settings_layout_locked(s:*const LayoutSelection,i:u8)->u8=layout_locked);
export!(xp_ui_settings_layout_toggle(s:*mut LayoutSelection,i:u8)->()=layout_toggle);
export!(xp_ui_settings_remap_init(s:*mut Remap)->()=remap_init);
export!(xp_ui_settings_remap_update(s:*mut Remap,i:*const Input,f:i32)->Effect=remap_update);
export!(xp_ui_settings_sync_update(s:u8,i:*const Input)->Effect=sync_update);
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_sync_result(w: u8, s: u8) -> u8 {
    settings_ffi::sync_result(w, s)
}

#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_menu_edit(k: u8, i: i32, v: u8) -> Effect {
    settings_ffi::menu_edit(k, i, v)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_status_value(i: u8, v: u8) -> u8 {
    settings_ffi::status_value(i, v)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_section(c: u8, f: u8) -> u8 {
    settings_ffi::section(c, f)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_visible(c: u8, f: u8, t: u8, b: u8, p: u8) -> u8 {
    settings_ffi::visible(c, f, t, b, p)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_extra(s: u8, c: u8, i: u8) -> u8 {
    settings_ffi::extra(s, c, i)
}

export!(xp_ui_settings_ble_show(s:*mut xp_ui::settings::ble::State,v:u8)->()=ble_show);
export!(xp_ui_settings_ble_poll(s:*mut xp_ui::settings::ble::State,n:u32)->u8=ble_poll);
export!(xp_ui_settings_ble_back(s:*mut xp_ui::settings::ble::State)->u8=ble_back);
export!(xp_ui_settings_ble_begin(s:*mut xp_ui::settings::ble::State,i:i32,c:u8,n:u32)->u8=ble_begin);
export!(xp_ui_settings_ble_step(s:*mut xp_ui::settings::ble::State,k:u8,c:u32,a:u32,n:u32)->u8=ble_step);
export!(xp_ui_settings_ble_clear(s:*mut xp_ui::settings::ble::State,i:i32)->u8=ble_clear);
export!(xp_ui_settings_ble_activate(s:*const xp_ui::settings::ble::State,c:i16,e:u8,a:u8)->Effect=ble_activate);
export!(xp_ui_settings_ble_row(s:*const xp_ui::settings::ble::State,f:u8,n:u8,b:u8,i:u16,o:*mut xp_ui::settings::ble::Row)->u8=ble_row);
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_ble_action(i: i32) -> u8 {
    xp_ui::settings::ble::action(i)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_ble_status(f: u8, e: u8) -> u8 {
    xp_ui::settings::ble::status(f, e != 0)
}

#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_settings_edit(k: u8, v: u8, n: u8, a: u8, b: u8, s: u8) -> Effect {
    xp_ui::settings::state::setting_edit(k, v, n, a, b, s)
}

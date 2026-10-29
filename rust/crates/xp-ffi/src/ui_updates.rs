use xp_ui::updates::{
    ffi,
    state::{Effect, State},
    Feedback, Layout, Services,
};
use xp_ui::{
    ffi::{Buffer, SceneView},
    model::{Input, Interest},
};
macro_rules! export {
 ($name:ident($($arg:ident:$ty:ty),*) -> $result:ty = $call:ident) => {
  /// # Safety
  /// Descriptors obey the private update-family ABI's lifetime and aliasing contract.
  #[no_mangle]
  #[inline(never)]
  pub unsafe extern "C" fn $name($($arg:$ty),*) -> $result { unsafe { ffi::$call($($arg),*) } }
 };
}
export!(xp_ui_updates_init(state:*mut State,kind:u8,recovery:u8)->()=init);
export!(xp_ui_updates_service(state:*mut State,event:u8,a:u32,b:u32)->Effect=service);
export!(xp_ui_updates_interest(state:*const State,now_ms:u32)->Interest=interest);
export!(xp_ui_updates_input(state:*mut State,input:*const Input,feedback:*const Feedback,published:u8,row_count:i32,family_count:i32)->Effect=input);
export!(xp_ui_updates_sleep(state:*const State)->u8=sleep);
export!(xp_ui_updates_delay(state:*const State)->u8=delay);
export!(xp_ui_updates_percent(state:*const State)->u32=percent);
export!(xp_ui_updates_view(state:*const State,layout:*const Layout,resources:*const Services,buffer:*const Buffer,feedback:*mut Feedback,scene:*mut SceneView)->u8=build);
export!(xp_ui_updates_publish(state:*mut State,snapshot:*const State,feedback:*const Feedback)->()=publish);

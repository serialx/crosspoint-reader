//! Thin exports for the private text-settings family.
use core::ffi::c_char;
use xp_core::para::paragraph::Paragraph;
use xp_ui::{
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest},
    reader_chrome::hits::Layout as Hits,
    reader_chrome_ffi::Rows,
    scene::Rect,
    text_settings::{
        self, ffi,
        preview::{Geometry, Key},
        view::Layout,
        Edit, State, Values,
    },
};
macro_rules! export {
    ($name:ident($($arg:ident:$ty:ty),*) $(->$ret:ty)? => $call:expr) => {
        /// # Safety
        /// Follows the private family header's borrowed, disjoint descriptor contract.
        #[no_mangle]
        #[inline(never)]
        pub unsafe extern "C" fn $name($($arg:$ty),*) $(->$ret)? { $call }
    };
}
export!(xp_ui_text_init(s:*mut State,t:u8,f:i32,z:i32,fc:u16,sc:u16,l:u8)=>unsafe{ffi::init(s,t,f,z,fc,sc,l)});
export!(xp_ui_text_interest(s:*const State,n:u32)->Interest=>unsafe{ffi::interest(s,n)});
export!(xp_ui_text_update(s:*mut State,i:*const Input)->Effect=>unsafe{ffi::update(s,i)});
export!(xp_ui_text_publish(s:*mut State,n:*const State,c:i32)=>unsafe{ffi::publish(s,n,c)});
export!(xp_ui_text_view(s:*const State,l:*const Layout,r:*const Rows,b:*const Buffer,n:*mut State,o:*mut SceneView,h:*mut Hits)->u8=>unsafe{ffi::view(s,l,r,b,n,o,h)});
export!(xp_ui_text_edit(s:*const State,r:i32,v:*const Values)->Edit=>unsafe{ffi::edit(s,r,v)});
export!(xp_ui_text_choice(f:u8,i:u8)->u8=>text_settings::choice(f,i));
export!(xp_ui_text_label(l:u8,k:u8,i:u8,v:u8,s:*mut c_char,c:u16)->*const c_char=>unsafe{ffi::label(l,k,i,v,s,c)});
export!(xp_ui_text_preview_geometry(b:Rect,p:i16,g:i16,lh:i32,line:i32,a:i32,k:*const Key,o:*mut Geometry)=>unsafe{ffi::geometry(b,p,g,lh,line,a,k,o)});
export!(xp_ui_text_preview_same(a:*const Key,b:*const Key)->u8=>unsafe{ffi::same(a,b)});
export!(xp_ui_text_preview_align(s:u8)->u8=>if s==4{0}else{s});
export!(xp_ui_text_preview_feed(p:*mut Paragraph,t:*const c_char)=>unsafe{ffi::feed(p,t)});
export!(xp_ui_text_preview_capacity(p:*const Paragraph,h:i32,l:i32,a:i32)->usize=>unsafe{ffi::capacity(p,h,l,a)});
export!(xp_ui_text_preview_line(r:i32,la:i32,h:i32,l:i32,a:i32,g:i32,y:*mut i16)->u8=>unsafe{ffi::line(r,la,h,l,a,g,y)});

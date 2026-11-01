use super::{page::Page, EpubSession, ResourceAccess};
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::render::{self, Kind, Op, Query, Services};

struct RenderCall<'a, 'b> {
    access: &'a ResourceAccess<'b>,
    native: &'a Services,
    page: NonNull<c_void>,
    font: i32,
    left: i32,
    top: i32,
}
impl RenderCall<'_, '_> {
    fn highlights(&self, page: NonNull<c_void>, font: i32, left: i32, top: i32) {
        let owner = self.access.owner;
        #[cfg(feature = "app-test")]
        if let Some(hook) = owner.render_hooks {
            unsafe { (hook.highlights)(hook.context, page.as_ptr(), font, top, left) };
            return;
        }
        owner.draw_clipping_highlights(self.access, page, font, top, left);
    }
}
impl Drop for RenderCall<'_, '_> {
    fn drop(&mut self) {
        let owner = self.access.owner;
        unsafe { (owner.leaves.render_finish_page)(owner.native(self.access)) };
    }
}

unsafe extern "C" fn read(context: *mut c_void, query: u8) -> u32 {
    let call = &*context.cast::<RenderCall<'_, '_>>();
    let owner = call.access.owner;
    if query == Query::TakeForced as u8 {
        u32::from(owner.take_forced_refresh(call.access))
    } else if query == Query::Counter as u8 {
        owner.render_refresh_counter(call.access) as u32
    } else {
        (call.native.read.expect("native render queries"))(call.native.context, query)
    }
}
unsafe extern "C" fn execute(context: *mut c_void, op: *const Op) -> u32 {
    let call = &*context.cast::<RenderCall<'_, '_>>();
    let owner = call.access.owner;
    match (*op).kind {
        kind if kind == Kind::Status as u8 => {
            #[cfg(feature = "app-test")]
            if let Some(hook) = owner.render_hooks {
                (hook.status)(hook.context);
                return 0;
            }
            owner.render_status_bar(call.access);
        }
        kind if kind == Kind::Highlights as u8 => {
            call.highlights(call.page, call.font, call.left, call.top);
        }
        kind if kind == Kind::SetCounter as u8 => {
            owner.set_render_refresh_counter(call.access, (*op).value as i32);
        }
        _ => {
            return (call.native.execute.expect("native render operations"))(
                call.native.context,
                op,
            )
        }
    }
    0
}
unsafe extern "C" fn highlights(
    context: *mut c_void,
    page: *mut c_void,
    font: i32,
    left: i32,
    top: i32,
) {
    let call = &*context.cast::<RenderCall<'_, '_>>();
    call.highlights(
        NonNull::new(page).expect("bound render page"),
        font,
        left,
        top,
    );
}

impl EpubSession {
    pub(super) fn render_contents(
        &self,
        access: &ResourceAccess<'_>,
        page: Page,
        font: i32,
        margins: [i32; 4],
        strip_enabled: bool,
    ) {
        let native = self.native(access);
        let services = unsafe { (self.leaves.render_services)(native).as_ref() }
            .expect("physical renderer services");
        let mut call = RenderCall {
            access,
            native: services,
            page: page.handle,
            font,
            left: margins[3],
            top: margins[0],
        };
        let context = (&mut call as *mut RenderCall<'_, '_>).cast();
        unsafe {
            (self.leaves.render_bind_page)(
                native,
                page.handle.as_ptr(),
                font,
                margins[3],
                margins[0],
                margins[2],
                highlights,
                context,
            );
            render::render(
                Services {
                    context,
                    read: Some(read),
                    execute: Some(execute),
                },
                strip_enabled,
            );
        }
        // Detach strip sources, prewarm and buffers before destroying their Page.
        drop(call);
        drop(page);
    }
}

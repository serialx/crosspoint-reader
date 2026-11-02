use super::{page::Page, EpubSession, ResourceAccess};
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::render::{self, Command, PhysicalRenderer, Query, Renderer};

struct RenderCall<'a, 'b> {
    access: &'a ResourceAccess<'b>,
    native: PhysicalRenderer<'a>,
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

impl Renderer for RenderCall<'_, '_> {
    fn query(&self, query: Query) -> u32 {
        let owner = self.access.owner;
        match query {
            Query::TakeForced => u32::from(owner.take_forced_refresh(self.access)),
            Query::Counter => owner.render_refresh_counter(self.access) as u32,
            _ => self.native.query(query),
        }
    }
    fn execute(&self, command: Command) -> u32 {
        let owner = self.access.owner;
        match command {
            Command::Status => {
                #[cfg(feature = "app-test")]
                if let Some(hook) = owner.render_hooks {
                    unsafe { (hook.status)(hook.context) };
                    return 0;
                }
                owner.render_status_bar(self.access);
            }
            Command::Highlights => {
                self.highlights(self.page, self.font, self.left, self.top);
            }
            Command::SetCounter(value) => {
                owner.set_render_refresh_counter(self.access, value as i32);
            }
            _ => return self.native.execute(command),
        }
        0
    }
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
        let call = RenderCall {
            access,
            native: unsafe { PhysicalRenderer::new(services) },
            page: page.handle,
            font,
            left: margins[3],
            top: margins[0],
        };
        let context = (&call as *const RenderCall<'_, '_>).cast_mut().cast();
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
        }
        render::render(&call, strip_enabled);
        // Detach strip sources, prewarm and buffers before destroying their Page.
        drop(call);
        drop(page);
    }
}

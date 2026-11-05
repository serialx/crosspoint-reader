use super::{
    app_abi,
    page::Page,
    page_view::View,
    render_resources::{
        physical::{Physical, Query as PhysicalQuery},
        renderer::{self, Context},
        PageGeometry,
    },
    EpubSession, ResourceAccess,
};

struct RenderContext<'a, 'b> {
    access: &'a ResourceAccess<'b>,
    page: View<'a>,
}
impl RenderContext<'_, '_> {
    fn geometry(&self) -> PageGeometry {
        self.access.owner.page_resources(self.access).geometry()
    }
}
impl Context for RenderContext<'_, '_> {
    fn now(&self) -> u32 {
        unsafe { (self.access.owner.runtime.now_ms)() }
    }
    fn has_images(&self) -> bool {
        self.page.has_images()
    }
    fn images_need_decode(&self) -> bool {
        self.page.info(self.access.owner.leaves).images_need_decode != 0
    }
    fn elements(&self) -> u16 {
        self.page.element_count()
    }
    fn take_forced(&self) -> bool {
        self.access.owner.take_forced_refresh(self.access)
    }
    fn counter(&self) -> u32 {
        self.access.owner.render_refresh_counter(self.access) as u32
    }
    fn set_counter(&self, value: u32) {
        self.access
            .owner
            .set_render_refresh_counter(self.access, value.cast_signed());
    }
    fn frequency(&self) -> u32 {
        self.access.owner.refresh_frequency(self.access) as u32
    }
    fn text_aa(&self) -> bool {
        self.access.owner.settings(false).text_aa != 0
    }
    fn is_a4(&self) -> bool {
        self.access.owner.overlay_geometry().a4 != 0
    }
    fn image_reset(&self) {
        unsafe { (self.access.owner.leaves.image_clear_failures)() };
    }
    fn paint(&self, mode: u8) {
        let geometry = self.geometry();
        let view = super::abi::XpEpubPageView {
            arena: geometry.arena,
            size: geometry.length,
            visible_offset: geometry.visible_offset,
        };
        unsafe {
            (self.access.owner.leaves.paint_arena)(
                &view,
                geometry.font,
                geometry.left,
                geometry.top,
                mode,
            )
        };
    }
    fn highlights(&self) {
        let geometry = self.geometry();
        self.access.owner.draw_clipping_highlights(
            self.access,
            self.page,
            geometry.font,
            geometry.top,
            geometry.left,
        );
    }
    fn status(&self) {
        self.access.owner.render_status_bar(self.access);
    }
    fn log_enabled(&self, severity: u8) -> bool {
        unsafe { (self.access.owner.leaves.log_enabled)(severity) != 0 }
    }
    fn log(&self, severity: u8, bytes: &[u8]) {
        unsafe {
            (self.access.owner.leaves.log)(
                severity,
                app_abi::Text {
                    data: b"ERS".as_ptr(),
                    len: 3,
                },
                app_abi::Text {
                    data: bytes.as_ptr(),
                    len: bytes.len(),
                },
            )
        };
    }
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
        #[cfg(feature = "app-test")]
        if matches!(page.borrow(), View::Legacy(..)) {
            self.render_legacy_contents(access, page, font, margins, strip_enabled);
            return;
        }
        #[cfg(feature = "app-test")]
        assert!(self.legacy_pages.get().is_none() && self.render_hooks.is_none());
        let view = page.borrow();
        let context = RenderContext { access, page: view };
        let geometry = PageGeometry {
            visible_offset: view.wire().visible_offset,
            font,
            left: margins[3],
            top: margins[0],
            status_top: self.overlay_geometry().height - margins[2] - 8,
            inverted: self.leaves.query(PhysicalQuery::Inverted) != 0,
            ..PageGeometry::EMPTY
        };
        renderer::run(
            self.page_resources(access),
            view.arena(),
            self.leaves,
            &context,
            self.resources,
            geometry,
            strip_enabled,
        );
        // The run's frame lease detaches all Page/source loans before Page release.
        drop(page);
    }
}

#[cfg(feature = "app-test")]
use core::ffi::c_void;
#[cfg(feature = "app-test")]
use xp_ui::reader::render::{self, Command, PhysicalRenderer, Query, Renderer};

#[cfg(feature = "app-test")]
struct RenderCall<'a, 'b> {
    access: &'a ResourceAccess<'b>,
    native: PhysicalRenderer<'a>,
    page: View<'a>,
    font: i32,
    left: i32,
    top: i32,
}
#[cfg(feature = "app-test")]
impl RenderCall<'_, '_> {
    fn highlights(&self, page: View<'_>, font: i32, left: i32, top: i32) {
        let owner = self.access.owner;
        #[cfg(feature = "app-test")]
        if let Some(hook) = owner.render_hooks {
            unsafe { (hook.highlights)(hook.context, page.fixture_pointer(), font, top, left) };
            return;
        }
        owner.draw_clipping_highlights(self.access, page, font, top, left);
    }
}
#[cfg(feature = "app-test")]
impl Drop for RenderCall<'_, '_> {
    fn drop(&mut self) {
        let owner = self.access.owner;
        unsafe { (owner.legacy_render().render_finish_page)(owner.native(self.access)) };
    }
}

#[cfg(feature = "app-test")]
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
#[cfg(feature = "app-test")]
impl EpubSession {
    pub(super) fn render_legacy_contents(
        &self,
        access: &ResourceAccess<'_>,
        page: Page,
        font: i32,
        margins: [i32; 4],
        strip_enabled: bool,
    ) {
        let native = self.native(access);
        let services = unsafe { (self.legacy_render().render_services)(native).as_ref() }
            .expect("physical renderer services");
        let call = RenderCall {
            access,
            native: unsafe { PhysicalRenderer::new(services) },
            page: page.borrow(),
            font,
            left: margins[3],
            top: margins[0],
        };
        let context = (&call as *const RenderCall<'_, '_>).cast_mut().cast();
        let View::Legacy(raw, services) = page.borrow() else {
            panic!("legacy Page required");
        };
        unsafe {
            (services.render_bind_page)(
                native,
                raw.as_ptr(),
                font,
                margins[3],
                margins[0],
                margins[2],
                legacy_highlights,
                context,
            )
        };
        render::render(&call, strip_enabled);
        // Detach strip sources, prewarm and buffers before destroying their Page.
        drop(call);
        drop(page);
    }
}

#[cfg(feature = "app-test")]
unsafe extern "C" fn legacy_highlights(
    context: *mut c_void,
    page: *mut c_void,
    font: i32,
    left: i32,
    top: i32,
) {
    let call = unsafe { &*context.cast::<RenderCall<'_, '_>>() };
    assert_eq!(call.page.fixture_pointer(), page);
    call.highlights(call.page, font, left, top);
}

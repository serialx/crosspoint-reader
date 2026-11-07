//! Host-only pixel capture through the production mode owner.
use super::StableOwner;
use core::{cell::Cell, ptr};
use xp_ui::{
    ffi::{Buffer, SceneView},
    model::{self, Input},
    network::modes_owner::{Host, Modes},
    scene::{Refresh, Scene},
    settings::{
        settings_ffi::Services,
        view::{Layout, Resources},
    },
};

struct Capture<'a> {
    layout: Layout,
    resources: Services,
    buffer: &'a Buffer,
    out: *mut SceneView,
    locked: Cell<bool>,
}

// One synchronous render, no retained loans or reentrant callbacks. The fixture
// checks model exclusion; C++ supplies only measurement and drawing resources.
unsafe impl Host for Capture<'_> {
    fn lock(&self) {
        assert!(!self.locked.replace(true));
    }
    fn unlock(&self) {
        assert!(self.locked.replace(false));
    }
    fn enter(&self) {}
    fn exit(&self) {}
    fn count(&self) -> u16 {
        self.resources.count
    }
    fn layout(&self) -> Layout {
        self.layout
    }
    fn with_resources<R>(&self, consume: impl FnOnce(&mut dyn Resources) -> R) -> R {
        assert!(!self.locked.get());
        let mut resources = self.resources;
        consume(&mut resources)
    }
    fn paint(&self, scene: &Scene<'_>) -> bool {
        assert!(!self.locked.get());
        assert!(scene.text.is_empty() && scene.interactions.is_empty());
        assert!(scene.commands.len() <= usize::from(self.buffer.command_capacity));
        unsafe {
            ptr::copy_nonoverlapping(
                scene.commands.as_ptr(),
                self.buffer.commands,
                scene.commands.len(),
            );
            self.out.write(SceneView {
                commands: self.buffer.commands,
                text: ptr::null(),
                interactions: ptr::null(),
                generation: scene.generation,
                command_count: scene.commands.len() as u16,
                text_len: 0,
                interaction_count: 0,
                refresh: scene.refresh as u8,
                reserved: 0,
            });
        }
        true
    }
    fn refresh(&self, _: Refresh) {}
    fn request_update(&self) {}
    fn haptic(&self) {
        panic!("capture input must only navigate");
    }
    fn complete(&self, _: bool, _: u8) {
        panic!("capture input must not complete");
    }
    fn scroll_diagnostic(&self, _: i32) {}
    fn scene_diagnostic(&self, _: bool) {
        panic!("mode capture scene rejected");
    }
}

/// # Safety
/// Descriptors and output storage are live and disjoint. Resources retain no
/// pointers or reenter. Commands remain owned by C++ after the visit is dropped.
#[no_mangle]
pub unsafe extern "C" fn network_modes_owner_render(
    selected: u16,
    layout: *const Layout,
    resources: *const Services,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let host = Capture {
        layout: unsafe { *layout },
        resources: unsafe { *resources },
        buffer: unsafe { &*buffer },
        out,
        locked: Cell::new(false),
    };
    assert!(selected < host.count());
    let owner = StableOwner::from(Modes::try_new(host).expect("mode capture owner"));
    owner.enter();
    for _ in 0..selected {
        owner.input(Input {
            pressed: model::NAV_NEXT,
            held: model::NAV_NEXT,
            now_ms: 100,
            ..Input::default()
        });
    }
    let painted = owner.render();
    owner.exit();
    u8::from(painted)
}

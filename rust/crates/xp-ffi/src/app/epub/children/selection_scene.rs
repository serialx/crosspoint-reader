use crate::app::abi;
use xp_ui::{
    ffi::{Paint, SceneView},
    scene::{Command, Error, Refresh},
};

pub(super) fn paint(
    services: &abi::Resources,
    commands: &[Command],
    resources: &Paint,
) -> Result<(), Error> {
    let scene = SceneView {
        commands: commands.as_ptr(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: commands.len() as u16,
        text_len: 0,
        interaction_count: 0,
        refresh: Refresh::None as u8,
        reserved: 0,
    };
    if unsafe { crate::app::scene::paint(services, &scene, resources) } == 0 {
        Err(Error::Invalid)
    } else {
        Ok(())
    }
}

pub(super) fn empty() -> Paint {
    Paint {
        context: core::ptr::null_mut(),
        clear: None,
        text: None,
        resource_valid: None,
        resource: None,
    }
}

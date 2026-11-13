use super::super::app_abi;
use super::*;
unsafe extern "C" {
    fn cssTestFailAllocation(index: usize);
    fn cssTestAllocationAttempts() -> usize;
}
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_document_id(
    leaves: &XpEpubLeaves,
    path: app_abi::Text,
    output: *mut u8,
) -> bool {
    let path = Text::copy(unsafe { path.bytes().unwrap() }).unwrap();
    unsafe { cssTestFailAllocation(0) };
    let digest = calculate(leaves, &path);
    let attempts = unsafe { cssTestAllocationAttempts() };
    unsafe { cssTestFailAllocation(usize::MAX) };
    assert_eq!(attempts, 0, "document sampling allocated Rust heap storage");
    let Some(digest) = digest else {
        return false;
    };
    assert_eq!(
        digest.0.get(32),
        Some(&0),
        "native event variables require a trailing NUL"
    );
    unsafe { ptr::copy_nonoverlapping(digest.wire().data, output, digest.wire().len) };
    true
}

#[no_mangle]
pub unsafe extern "C" fn xp_app_test_document_session(
    leaves: &'static XpEpubLeaves,
    runtime: &'static app_abi::Runtime,
    resources: &'static app_abi::Resources,
    path: app_abi::Text,
    worthy: bool,
) {
    let mut session = super::super::super::EpubSession::try_new(
        unsafe { path.bytes().unwrap() },
        false,
        15,
        leaves,
        runtime,
        resources,
    )
    .unwrap();
    session.state.get_mut().base.session = xp_ui::reader::SessionAccounting {
        start_epoch: 100,
        duration: if worthy { 10 } else { 9 },
        start_progress: 123,
        end_progress: 456,
        ..Default::default()
    };
    session.flush_session();
    let state = session.state.get_mut().base.session;
    assert_eq!(state.start_epoch, 0);
    assert_eq!(state.duration, 0);
    assert_eq!(state.start_progress, 0);
    assert_eq!(state.end_progress, 0);
}

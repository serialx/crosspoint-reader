use super::*;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_css_diagnostic(
    leaves: &'static XpEpubLeaves,
    event: u32,
    a: u32,
    b: u32,
) {
    diagnostics::parser(
        (leaves as *const XpEpubLeaves).cast_mut().cast(),
        event,
        a,
        b,
        0,
        0,
    );
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_css_transaction_diagnostic(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    event: u32,
    value: u32,
) {
    let mut owner = None;
    let mut handle = ptr::null_mut();
    let cache_path = Text::default();
    let mut context = Context {
        destination: &mut owner,
        handle: ptr::addr_of_mut!(handle),
        cache_path: &cache_path,
        resources,
        leaves,
        file: FileSlot::new(),
        source: None,
        paths: core::array::from_fn(|_| Text::default()),
        buffer: None,
        order: None,
        io: ptr::null_mut(),
    };
    unsafe { diagnostics::transaction((&mut context as *mut Context<'_>).cast(), event, value) };
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_css(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    cache_path: abi::Text,
    source_path: abi::Text,
    kind: u8,
    complete: u8,
) -> u64 {
    let (Some(cache_path), Some(source_path)) = (unsafe { cache_path.bytes() }, unsafe {
        source_path.bytes()
    }) else {
        return u64::MAX;
    };
    let (Ok(cache_path), Ok(source_path)) = (Text::copy(cache_path), Text::copy(source_path))
    else {
        return u64::MAX;
    };
    let mut owner = None;
    let result = if kind == 0 {
        let mut source = FileSlot::new();
        if !source.open(leaves, &source_path, false, c"EBP") {
            return u64::MAX;
        }
        let result = run(
            &mut owner,
            &cache_path,
            resources,
            leaves,
            Action::Stream,
            true,
            Some(&mut source),
        );
        if result != 0 {
            return u64::from(result) + 10;
        }
        run(
            &mut owner,
            &cache_path,
            resources,
            leaves,
            Action::Save,
            complete != 0,
            None,
        )
    } else {
        let action = match kind {
            1 => Action::Load,
            2 => Action::Inspect,
            3 => Action::Delete,
            4 => Action::Restore,
            _ => return u64::MAX,
        };
        run(
            &mut owner,
            &cache_path,
            resources,
            leaves,
            action,
            complete != 0,
            None,
        )
    };
    let count = owner.as_ref().map_or(0, |owner| owner.store.rule_count());
    u64::from(result) | ((count as u64) << 32)
}

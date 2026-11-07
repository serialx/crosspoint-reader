use super::{
    abi::*,
    text::{self, Pool, Text},
    url, xp_wifi_guard_enter_retire_n7_s1_s2_w3b as enter_guard,
    xp_wifi_guard_exit_retire_n7_s1_s2_w3b as exit_guard,
};
use core::{ffi::c_void, ptr};

struct Transfer<'a> {
    request: &'a Request,
    leaves: &'a Leaves,
    pool: &'a Pool<'a>,
    part: Option<Text<'a>>,
    bytes: usize,
    total: usize,
    status: i32,
    known: bool,
    complete: bool,
    stopped: bool,
    aborted: bool,
    same_origin: bool,
    first: bool,
    invalid: bool,
    end: usize,
    attempt_start: usize,
}
impl<'a> Transfer<'a> {
    #[inline(always)]
    fn new(
        request: &'a Request,
        leaves: &'a Leaves,
        pool: &'a Pool<'a>,
        part: Option<Text<'a>>,
    ) -> Self {
        Self {
            request,
            leaves,
            pool,
            part,
            bytes: 0,
            total: 0,
            status: 0,
            known: false,
            complete: false,
            stopped: false,
            aborted: false,
            same_origin: true,
            first: true,
            invalid: false,
            end: 0,
            attempt_start: 0,
        }
    }
}
fn success(status: i32) -> bool {
    (200..300).contains(&status)
}
fn redirect(status: i32) -> bool {
    matches!(status, 301 | 302 | 303 | 307 | 308)
}
unsafe fn event(t: *const Transfer<'_>, kind: Diagnostic, a: usize, b: usize) {
    let r = (*t).request;
    ((*(*t).leaves.diagnostics).emit)(kind, r.url.bytes, r.destination.bytes, a, b);
}
unsafe extern "C" fn abort(context: *mut c_void) -> bool {
    let t = context.cast::<Transfer<'_>>();
    let flag = (*t).request.cancel_flag;
    !flag.is_null() && *flag
}
unsafe fn notify(t: *const Transfer<'_>) {
    let c = &*(*t).request.consumer;
    if let Some(progress) = c.progress {
        progress(c.context, (*t).bytes, (*t).total);
    }
}
unsafe fn sink(t: *const Transfer<'_>, bytes: Bytes) -> bool {
    let c = &*(*t).request.consumer;
    c.sink.is_some_and(|f| f(c.context, bytes))
}
fn range(bytes: &[u8]) -> Option<(usize, usize, usize)> {
    let mut bytes = bytes;
    while matches!(bytes.last(), Some(b' ' | b'\t')) {
        bytes = &bytes[..bytes.len() - 1];
    }
    let mut rest = bytes.strip_prefix(b"bytes ")?;
    fn number(rest: &mut &[u8], separator: Option<u8>) -> Option<usize> {
        let count = rest.iter().take_while(|c| c.is_ascii_digit()).count();
        if count == 0 {
            return None;
        }
        let mut value = 0usize;
        for &c in &rest[..count] {
            value = value.checked_mul(10)?.checked_add(usize::from(c - b'0'))?;
        }
        *rest = &rest[count..];
        if let Some(separator) = separator {
            if rest.first() != Some(&separator) {
                return None;
            }
            *rest = &rest[1..];
        } else if !rest.is_empty() {
            return None;
        }
        Some(value)
    }
    let first = number(&mut rest, Some(b'-'))?;
    let last = number(&mut rest, Some(b'/'))?;
    let total = number(&mut rest, None)?;
    (first <= last && last < total).then_some((first, last, total))
}
#[inline(never)]
unsafe fn begin_response(t: *mut Transfer<'_>, facts: Facts) -> bool {
    (*t).first = false;
    if facts.status == 206 {
        let value = (*t).pool.header(
            &*(*t).leaves.sdk,
            (*t).request.client_storage,
            c"content-range".as_ptr(),
        );
        let parsed = range(value.slice());
        drop(value);
        let Some((first, last, total)) = parsed else {
            return false;
        };
        if first != (*t).attempt_start
            || ((*t).total != 0 && (*t).total != total)
            || (facts.known_length && facts.length != last - first + 1)
        {
            return false;
        }
        (*t).end = last + 1;
        (*t).total = total;
    } else {
        if (*t).attempt_start != 0 {
            if facts.status != 200 {
                return false;
            }
            if (*t).request.kind != Kind::Download {
                (*t).stopped = true;
                return false;
            }
            let file = &*(*t).leaves.file;
            (file.close)((*t).request.file_storage, false);
            let path = (*t).part.as_ref().unwrap().native();
            if !(file.open_write)((*t).request.file_storage, path) {
                (*t).stopped = true;
                return false;
            }
            (*t).bytes = 0;
            (*t).attempt_start = 0;
        }
        (*t).total = if facts.known_length { facts.length } else { 0 };
        (*t).end = (*t).total;
    }
    true
}
#[inline(never)]
unsafe extern "C" fn chunk(context: *mut c_void, bytes: Bytes, facts: *const Facts) -> bool {
    let t = context.cast::<Transfer<'_>>();
    let facts = *facts;
    match (*t).request.kind {
        Kind::Fetch | Kind::Download => {
            if !success(facts.status) {
                return true;
            }
            if ((*t).first && !begin_response(t, facts))
                || bytes.length > usize::MAX - (*t).bytes
                || ((*t).end != 0
                    && ((*t).bytes > (*t).end || bytes.length > (*t).end - (*t).bytes))
            {
                (*t).invalid = true;
                return false;
            }
            let written = if (*t).request.kind == Kind::Download {
                ((*(*t).leaves.file).write)((*t).request.file_storage, bytes) == bytes.length
            } else {
                sink(t, bytes)
            };
            if !written {
                (*t).stopped = true;
                return false;
            }
            (*t).bytes += bytes.length;
            if (*t).request.kind == Kind::Download {
                notify(t);
            }
            true
        }
        Kind::Catalog => {
            if !success(facts.status) && !((*t).request.capture_error_body && facts.status == 401) {
                return true;
            }
            if !sink(t, bytes) {
                (*t).stopped = true;
                return false;
            }
            true
        }
        Kind::Form => {
            let length = bytes.length.min(4096usize.saturating_sub((*t).bytes));
            text::output_append(
                &*(*t).leaves.text,
                (*t).request.form_response,
                Bytes {
                    data: bytes.data,
                    length,
                },
            );
            (*t).bytes += length;
            true
        }
        Kind::PostFile => {
            if !success(facts.status) {
                return true;
            }
            if ((*(*t).leaves.file).write)((*t).request.file_storage, bytes) != bytes.length {
                (*t).stopped = true;
                return false;
            }
            (*t).bytes = (*t).bytes.wrapping_add(bytes.length);
            (*t).known = facts.known_length;
            (*t).total = facts.length;
            if facts.known_length {
                notify(t);
            }
            true
        }
    }
}
unsafe fn add_header(
    pool: &Pool<'_>,
    sdk: &SdkLeaves,
    client: *mut c_void,
    name: &[u8],
    value: NativeText,
) {
    let slots = text::slots::<1>();
    let scratch = pool.scope(&slots);
    let name = scratch.copy(Bytes::from_slice(name));
    (sdk.add_header)(client, name.native(), value);
}
#[inline(never)]
unsafe fn configure(
    r: &Request,
    leaves: &Leaves,
    pool: &Pool<'_>,
    same_origin: bool,
    catalog: bool,
) {
    let slots = text::slots::<1>();
    let scratch = pool.scope(&slots);
    let sdk = &*leaves.sdk;
    let bearer = catalog && r.bearer.bytes.length != 0;
    let config = Configuration {
        timeout_ms: 60000,
        insecure: true,
        basic_auth: same_origin
            && !bearer
            && r.username.bytes.length != 0
            && r.password.bytes.length != 0,
        user_agent: Bytes::from_slice(b"CrossPoint-ESP32-"),
        username: r.username,
        password: r.password,
    };
    (sdk.configure)(r.client_storage, &config);
    if same_origin {
        if bearer {
            let token = scratch.concat(Bytes::from_slice(b"Bearer "), r.bearer.bytes);
            add_header(
                pool,
                sdk,
                r.client_storage,
                b"Authorization",
                token.native(),
            );
        }
        let c = &*r.consumer;
        for i in 0..c.header_count {
            let mut name = NativeText::EMPTY;
            let mut value = NativeText::EMPTY;
            (c.header_at)(c.context, i, &mut name, &mut value);
            (sdk.add_header)(r.client_storage, name, value);
        }
    }
    if catalog {
        if r.accept.bytes.length != 0 {
            add_header(pool, sdk, r.client_storage, b"Accept", r.accept);
        }
        if r.accept_language.bytes.length != 0 {
            add_header(
                pool,
                sdk,
                r.client_storage,
                b"Accept-Language",
                r.accept_language,
            );
        }
    }
}
#[inline(always)]
unsafe fn send(t: *mut Transfer<'_>, get: bool, catalog: bool) -> Facts {
    let callbacks = Callbacks {
        context: t.cast(),
        chunk,
        should_abort: if catalog { None } else { Some(abort) },
    };
    let mut facts = Facts::default();
    let r = (*t).request;
    // postForm clears the caller's response before reading its request body.
    let body = if r.kind == Kind::Form
        && r.body.native_string == (*r.form_response).native_string.cast_const()
    {
        (*r.form_response).view().bytes
    } else {
        r.body.bytes
    };
    let status = ((*(*t).leaves.sdk).send)(
        r.client_storage,
        if get { c"GET" } else { c"POST" }.as_ptr(),
        if get { Bytes::EMPTY } else { body },
        &callbacks,
        &mut facts,
    );
    facts.status = status;
    (*t).status = status;
    (*t).aborted = facts.aborted;
    facts
}
#[inline(always)]
unsafe fn resumable(t: *mut Transfer<'_>) {
    let pool = (*t).pool;
    let sdk = &*(*t).leaves.sdk;
    let r = (*t).request;
    let mut current = pool.copy(r.url.bytes);
    let start_origin = url::origin(pool, r.url.bytes);
    let mut redirects = 0;
    let mut stalled = 0;
    let mut attempts = 0;
    while attempts < 20 && stalled < 3 {
        attempts += 1;
        if !(sdk.acquire)(r.client_storage, current.native()) {
            (*t).status = -1;
            (sdk.release)(r.client_storage);
            return;
        }
        let hop_origin = url::origin(pool, current.bytes());
        (*t).same_origin = hop_origin.slice() == start_origin.slice();
        configure(r, (*t).leaves, pool, (*t).same_origin, false);
        event(t, Diagnostic::GetHeap, 0, 0);
        drop(hop_origin);
        (*t).attempt_start = (*t).bytes;
        (*t).first = true;
        (*t).invalid = false;
        (*t).end = 0;
        if (*t).attempt_start != 0 {
            let mut buffer = [0; 20];
            let digits = url::decimal((*t).attempt_start, &mut buffer);
            let mut value = pool.concat(Bytes::from_slice(b"bytes="), Bytes::from_slice(digits));
            value.append(Bytes::from_slice(b"-"));
            add_header(pool, sdk, r.client_storage, b"Range", value.native());
        }
        let facts = send(t, true, false);
        if (*t).aborted {
            (sdk.release)(r.client_storage);
            return;
        }
        if redirect(facts.status) {
            let location = pool.header(sdk, r.client_storage, c"location".as_ptr());
            let stop = redirects >= 5
                || location.value.length == 0
                || !url::resolve(pool, &mut current, location.bytes());
            redirects += 1;
            if !stop && r.downgrade_redirects && current.slice().starts_with(b"https://") {
                let length = current.value.length;
                ptr::copy(
                    current.value.data.add(8),
                    current.value.data.add(7),
                    length - 8,
                );
                ptr::copy_nonoverlapping(b"http://".as_ptr(), current.value.data, 7);
                current.resize(length - 1);
            }
            drop(location);
            (sdk.release)(r.client_storage);
            if stop {
                return;
            }
            attempts -= 1;
            continue;
        }
        let stop = (*t).invalid
            || (*t).stopped
            || !success(facts.status)
            || ((*t).first && !begin_response(t, facts));
        if !stop && facts.complete {
            if (*t).end != 0 && (*t).bytes != (*t).end {
                (sdk.release)(r.client_storage);
                return;
            }
            if facts.status != 206 || (*t).bytes == (*t).total {
                (*t).complete = true;
                (sdk.release)(r.client_storage);
                return;
            }
        }
        (sdk.release)(r.client_storage);
        if stop {
            return;
        }
        stalled = if (*t).bytes > (*t).attempt_start {
            0
        } else {
            stalled + 1
        };
    }
}
#[inline(always)]
unsafe fn direct<'a>(t: *mut Transfer<'a>, current: NativeText, catalog: bool) -> Option<Text<'a>> {
    let r = (*t).request;
    let sdk = &*(*t).leaves.sdk;
    if !(sdk.acquire)(r.client_storage, current) {
        ((*(*t).leaves.diagnostics).emit)(Diagnostic::BadUrl, current.bytes, Bytes::EMPTY, 0, 0);
        (*t).status = if r.kind == Kind::PostFile {
            i32::MIN
        } else {
            0
        };
        (sdk.release)(r.client_storage);
        return None;
    }
    configure(r, (*t).leaves, (*t).pool, (*t).same_origin, catalog);
    if catalog {
        ((*(*t).leaves.diagnostics).emit)(Diagnostic::Get, current.bytes, Bytes::EMPTY, 0, 0);
    } else if r.kind == Kind::Form {
        let value = (*t)
            .pool
            .copy(Bytes::from_slice(b"application/x-www-form-urlencoded"));
        add_header(
            (*t).pool,
            sdk,
            r.client_storage,
            b"Content-Type",
            value.native(),
        );
        drop(value);
        event(t, Diagnostic::Post, 0, 0);
    } else {
        add_header(
            (*t).pool,
            sdk,
            r.client_storage,
            b"Content-Type",
            r.content_type,
        );
    }
    let facts = send(t, catalog, catalog);
    let location = if catalog && redirect(facts.status) {
        Some(
            (*t).pool
                .header(sdk, r.client_storage, c"location".as_ptr()),
        )
    } else {
        None
    };
    (*t).known = facts.known_length;
    (*t).total = facts.length;
    (*t).complete = true;
    (sdk.release)(r.client_storage);
    location
}
unsafe fn publish(t: *const Transfer<'_>, same: bool) {
    let r = (*t).request;
    if !r.last_status.is_null() {
        *r.last_status = (*t).status.max(0);
    }
    if !r.last_redirected.is_null() {
        *r.last_redirected = !same;
    }
}
unsafe fn get_result(t: *const Transfer<'_>) -> u32 {
    if (*t).aborted {
        return 3;
    }
    if (*t).stopped {
        return 2;
    }
    if matches!((*t).status, 401 | 403) {
        event(t, Diagnostic::Unauthorized, (*t).status as usize, 0);
        return 4;
    }
    if !success((*t).status) {
        event(t, Diagnostic::Failed, (*t).status as usize, 0);
        return 1;
    }
    if !(*t).complete {
        event(t, Diagnostic::Incomplete, (*t).bytes, (*t).total);
        return 1;
    }
    0
}
#[inline(always)]
unsafe fn run(t: *mut Transfer<'_>) -> u32 {
    let r = (*t).request;
    let file = &*(*t).leaves.file;
    let is_file = matches!(r.kind, Kind::Download | Kind::PostFile);
    if is_file {
        let part = (*t).part.as_ref().unwrap().native();
        (file.remove)(part);
        if !(file.open_write)(r.file_storage, part) {
            event(t, Diagnostic::OpenFailed, 0, 0);
            return 2;
        }
    }
    let guard = matches!(r.kind, Kind::Fetch | Kind::Download)
        .then(|| enter_guard((*t).leaves.radio, (*t).leaves.diagnostics));
    match r.kind {
        Kind::Fetch | Kind::Download => {
            resumable(t);
            publish(t, (*t).same_origin);
        }
        Kind::Catalog => unreachable!("catalog has its own entry"),
        Kind::Form | Kind::PostFile => {
            direct(t, r.url, false);
        }
    }
    if !r.status_output.is_null() {
        *r.status_output = (*t).status.max(0);
    }
    if matches!(r.kind, Kind::Catalog | Kind::Form) {
        return u32::from(!success((*t).status) || (*t).stopped);
    }
    if r.kind == Kind::PostFile && !(*t).complete && (*t).status == i32::MIN {
        return 1;
    }
    let result = if r.kind == Kind::PostFile {
        (file.close)(r.file_storage, false);
        if (*t).aborted {
            3
        } else if (*t).stopped {
            2
        } else if !success((*t).status) {
            event(t, Diagnostic::PostFailed, (*t).status as usize, 0);
            1
        } else if (*t).bytes == 0 {
            event(t, Diagnostic::Empty, 0, 0);
            1
        } else if (*t).known && (*t).bytes != (*t).total {
            event(t, Diagnostic::PostIncomplete, (*t).bytes, (*t).total);
            1
        } else {
            0
        }
    } else {
        get_result(t)
    };
    if let Some(guard) = guard {
        exit_guard(guard, (*t).leaves.radio, (*t).leaves.diagnostics);
    }
    if r.kind == Kind::Download {
        (file.close)(r.file_storage, false);
    }
    if !is_file {
        return result;
    }
    let part = (*t).part.as_ref().unwrap().native();
    if result != 0 {
        (file.remove)(part);
        return result;
    }
    if (*t).bytes == 0 {
        event(t, Diagnostic::Empty, 0, 0);
        (file.remove)(part);
        return 1;
    }
    if !(file.replace)(part, r.destination) {
        event(t, Diagnostic::ReplaceFailed, 0, 0);
        (file.remove)(part);
        return 2;
    }
    event(t, Diagnostic::Downloaded, (*t).bytes, 0);
    0
}
struct CatalogTransfer<'a> {
    request: &'a Request,
    stopped: bool,
}
unsafe extern "C" fn catalog_chunk(
    context: *mut c_void,
    bytes: Bytes,
    facts: *const Facts,
) -> bool {
    let transfer = context.cast::<CatalogTransfer<'_>>();
    let r = (*transfer).request;
    if !success((*facts).status) && !(r.capture_error_body && (*facts).status == 401) {
        return true;
    }
    let consumer = &*r.consumer;
    if !consumer.sink.is_some_and(|f| f(consumer.context, bytes)) {
        (*transfer).stopped = true;
        return false;
    }
    true
}

#[inline(never)]
unsafe fn catalog_entry(r: &Request, leaves: &Leaves) -> u32 {
    let guard = enter_guard(leaves.radio, leaves.diagnostics);
    // Current URL, origin, location and replacement; no file staging slot.
    let slots = text::slots::<4>();
    let pool = Pool::new(&*leaves.text, &slots);
    let mut transfer = CatalogTransfer {
        request: r,
        stopped: false,
    };
    let mut status = 0;
    {
        let mut current = pool.empty();
        current.assign(r.url.bytes);
        let origin = url::origin(&pool, r.url.bytes);
        let sdk = &*leaves.sdk;
        for hop in 0..=5 {
            let same_origin = {
                let value = url::origin(&pool, current.bytes());
                value.slice() == origin.slice()
            };
            if !(sdk.acquire)(r.client_storage, current.native()) {
                ((*leaves.diagnostics).emit)(
                    Diagnostic::BadUrl,
                    current.bytes(),
                    Bytes::EMPTY,
                    0,
                    0,
                );
                status = 0;
                (sdk.release)(r.client_storage);
                break;
            }
            configure(r, leaves, &pool, same_origin, true);
            ((*leaves.diagnostics).emit)(Diagnostic::Get, current.bytes(), Bytes::EMPTY, 0, 0);
            let callbacks = Callbacks {
                context: ptr::addr_of_mut!(transfer).cast(),
                chunk: catalog_chunk,
                should_abort: None,
            };
            let mut facts = Facts::default();
            status = (sdk.send)(
                r.client_storage,
                c"GET".as_ptr(),
                Bytes::EMPTY,
                &callbacks,
                &mut facts,
            );
            let location =
                redirect(status).then(|| pool.header(sdk, r.client_storage, c"location".as_ptr()));
            (sdk.release)(r.client_storage);
            let resolved = location.as_ref().is_some_and(|value| {
                value.value.length != 0 && url::resolve(&pool, &mut current, value.bytes())
            });
            drop(location);
            if !resolved || hop == 5 {
                break;
            }
        }
        {
            let final_origin = url::origin(&pool, current.bytes());
            if !r.last_status.is_null() {
                *r.last_status = status.max(0);
            }
            if !r.last_redirected.is_null() {
                *r.last_redirected = final_origin.slice() != origin.slice();
            }
        }
        if !r.status_output.is_null() {
            *r.status_output = status.max(0);
        }
    }
    if !r.status_output.is_null() {
        *r.status_output = status.max(0);
    }
    let result = u32::from(!success(status) || transfer.stopped);
    drop(pool);
    exit_guard(guard, leaves.radio, leaves.diagnostics);
    result
}

#[inline(never)]
unsafe fn common_entry(r: &Request, leaves: &Leaves) -> u32 {
    let initial = match r.kind {
        Kind::Fetch => Some(Diagnostic::Fetching),
        Kind::Download => Some(Diagnostic::Downloading),
        Kind::PostFile => Some(Diagnostic::PostDownload),
        Kind::Catalog | Kind::Form => None,
    };
    if let Some(initial) = initial {
        ((*leaves.diagnostics).emit)(initial, r.url.bytes, r.destination.bytes, 0, 0);
    }
    let guard = matches!(r.kind, Kind::Catalog | Kind::Form | Kind::PostFile)
        .then(|| enter_guard(leaves.radio, leaves.diagnostics));
    if r.kind == Kind::Form {
        ((*leaves.text).resize)(r.form_response, 0);
    }
    // Redirect parsing and header construction use their own scoped slots.
    let slots = text::slots::<5>();
    let pool = Pool::new(&*leaves.text, &slots);
    let part = if matches!(r.kind, Kind::Download | Kind::PostFile) {
        Some(pool.concat(r.destination.bytes, Bytes::from_slice(b".part")))
    } else {
        None
    };
    let mut transfer = Transfer::new(r, leaves, &pool, part);
    // The native call can invoke chunk/abort. No Rust owner reference spans it.
    let result = run(ptr::addr_of_mut!(transfer));
    if matches!(r.kind, Kind::Download | Kind::PostFile) {
        ((*leaves.file).close)(r.file_storage, true);
    }
    drop(transfer);
    drop(pool);
    if let Some(guard) = guard {
        exit_guard(guard, leaves.radio, leaves.diagnostics);
    }
    result
}

#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_http_transfer_retire_n5_n6_c2_c4_b2(
    request: *const Request,
    leaves: *const Leaves,
) -> u32 {
    let r = &*request;
    let leaves = &*leaves;
    if r.kind == Kind::Catalog {
        catalog_entry(r, leaves)
    } else {
        common_entry(r, leaves)
    }
}

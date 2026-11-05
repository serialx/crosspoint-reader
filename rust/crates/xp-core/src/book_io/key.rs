//! Device-wrapped book key format and transaction ordering. Crypto is a platform leaf.
use core::ffi::c_void;

pub const MAX_RECORD: usize = 74;
#[repr(C)]
pub struct Cipher {
    pub output: *mut u8,
    pub input: *const u8,
    pub kek: *const u8,
    pub iv: *const u8,
    pub tag: *mut u8,
    pub aad: *const u8,
    pub key_len: usize,
    pub aad_len: usize,
    pub encrypt: bool,
}
#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub secret: unsafe extern "C" fn(*mut c_void, *mut u8) -> bool,
    pub hash: unsafe extern "C" fn(*mut c_void, *const u8, usize, *mut u8) -> bool,
    pub random: unsafe extern "C" fn(*mut c_void, *mut u8, usize),
    pub cipher: unsafe extern "C" fn(*mut c_void, *const Cipher) -> bool,
    pub read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    pub write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> bool,
    pub replace: unsafe extern "C" fn(*mut c_void) -> bool,
}

unsafe fn wrap_key(services: &Services, kek: &mut [u8; 32]) -> bool {
    let mut secret = [0u8; 32];
    if !(services.secret)(services.context, secret.as_mut_ptr()) {
        return false;
    }
    let mut input = [0u8; 41];
    input[..9].copy_from_slice(b"book-key:");
    input[9..].copy_from_slice(&secret);
    (services.hash)(
        services.context,
        input.as_ptr(),
        input.len(),
        kek.as_mut_ptr(),
    )
}

/// # Safety
/// Services and their context remain valid throughout this call. Callbacks do not
/// retain pointers or reenter, and observe the fixed record/crypto buffer bounds.
pub unsafe fn write(key: &[u8], expires: i64, services: &Services) -> i32 {
    if !matches!(key.len(), 16 | 32) {
        return 1;
    }
    let mut kek = [0u8; 32];
    if !wrap_key(services, &mut kek) {
        return 1;
    }
    let mut file = [0u8; MAX_RECORD];
    file[..4].copy_from_slice(b"FIBK");
    file[4] = 2;
    file[5] = key.len() as u8;
    file[6..14].copy_from_slice(&expires.to_le_bytes());
    (services.random)(services.context, file.as_mut_ptr().add(14), 12);
    let cipher = Cipher {
        output: file.as_mut_ptr().add(26),
        input: key.as_ptr(),
        kek: kek.as_ptr(),
        iv: file.as_ptr().add(14),
        tag: file.as_mut_ptr().add(26 + key.len()),
        aad: file.as_ptr(),
        key_len: key.len(),
        aad_len: 14,
        encrypt: true,
    };
    if !(services.cipher)(services.context, &cipher) {
        return 2;
    }
    if !(services.write)(services.context, file.as_ptr(), 42 + key.len()) {
        return 3;
    }
    if (services.replace)(services.context) {
        0
    } else {
        4
    }
}

/// # Safety
/// Services obey write's contract. Key is disjoint writable storage. Expiry is
/// changed only on successful authenticated opening, matching the old facade.
pub unsafe fn read(key: *mut u8, expires: *mut i64, services: &Services) -> i32 {
    let mut file = [0u8; MAX_RECORD];
    let count = (services.read)(services.context, file.as_mut_ptr(), file.len());
    if count < 13 || count as usize > file.len() || &file[..4] != b"FIBK" {
        return 0;
    }
    let (header, key_len, expires_offset) = match file[4] {
        1 => (13, 16, 5),
        2 if matches!(file[5], 16 | 32) => (14, usize::from(file[5]), 6),
        _ => return 0,
    };
    if count as usize != header + 12 + key_len + 16 {
        return 0;
    }
    let mut kek = [0u8; 32];
    if !wrap_key(services, &mut kek) {
        return 0;
    }
    let cipher = Cipher {
        output: key,
        input: file.as_ptr().add(header + 12),
        kek: kek.as_ptr(),
        iv: file.as_ptr().add(header),
        tag: file.as_mut_ptr().add(header + 12 + key_len),
        aad: file.as_ptr(),
        key_len,
        aad_len: header,
        encrypt: false,
    };
    if !(services.cipher)(services.context, &cipher) {
        return -1;
    }
    let mut value = 0u64;
    for i in 0..8 {
        value |= u64::from(file[expires_offset + i]) << (i * 8);
    }
    expires.write(value as i64);
    key_len as i32
}

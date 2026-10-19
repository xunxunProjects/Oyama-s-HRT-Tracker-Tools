use std::os::raw::c_char;

static VERSION: &[u8] = b"0.1.0\0";

/// Version of the Rust printing library, as a NUL-terminated C string.
#[no_mangle]
pub extern "C" fn hrtprint_version() -> *const c_char {
    VERSION.as_ptr().cast()
}

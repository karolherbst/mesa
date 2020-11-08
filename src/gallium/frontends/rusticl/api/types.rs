pub type CreateContextCB = ::std::option::Option<
    unsafe extern "C" fn(
        errinfo: *const ::std::os::raw::c_char,
        private_info: *const ::std::ffi::c_void,
        cb: usize,
        user_data: *mut ::std::ffi::c_void,
    ),
>;

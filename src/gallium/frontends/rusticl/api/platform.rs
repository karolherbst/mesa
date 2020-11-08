extern crate mesa_rust_util;

use crate::api::bindings::*;
use crate::api::icd::DISPATCH;
use crate::api::util::*;
use crate::core::version::*;

use self::mesa_rust_util::ptr::*;

#[repr(C)]
#[allow(non_camel_case_types)]
pub struct _cl_platform_id {
    dispatch: *const cl_icd_dispatch,
    extensions: [cl_name_version; 1],
}

static mut PLATFORM: _cl_platform_id = _cl_platform_id {
    dispatch: &DISPATCH,
    extensions: [mk_cl_version_ext(1, 0, 0, b"cl_khr_icd")],
};

pub fn check_platform(platform: cl_platform_id) -> Result<(), cl_int> {
    if !platform.is_null() && unsafe { platform != &PLATFORM } {
        return Err(CL_INVALID_PLATFORM);
    }
    Ok(())
}

pub fn get_platform() -> cl_platform_id {
    unsafe { &PLATFORM }
}

pub fn get_platform_ids(
    num_entries: cl_uint,
    platforms: *mut cl_platform_id,
    num_platforms: *mut cl_uint,
) -> Result<(), cl_int> {
    // CL_INVALID_VALUE if num_entries is equal to zero and platforms is not NULL
    if num_entries == 0 && !platforms.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // or if both num_platforms and platforms are NULL."
    if num_platforms.is_null() && platforms.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // platforms returns a list of OpenCL platforms available for access through the Khronos ICD Loader.
    // The cl_platform_id values returned in platforms are ICD compatible and can be used to identify a
    // specific OpenCL platform. If the platforms argument is NULL, then this argument is ignored. The
    // number of OpenCL platforms returned is the minimum of the value specified by num_entries or the
    // number of OpenCL platforms available.
    unsafe {
        platforms.write_checked(&PLATFORM);
    }

    // num_platforms returns the number of OpenCL platforms available. If num_platforms is NULL, then
    // this argument is ignored.
    num_platforms.write_checked(1);

    Ok(())
}

pub fn get_platform_info(
    platform: cl_platform_id,
    param_name: cl_platform_info,
    param_value_size: usize,
    param_value: *mut ::std::ffi::c_void,
    param_value_size_ret: *mut usize,
) -> Result<(), cl_int> {
    // CL_INVALID_PLATFORM if platform is not a valid platform.
    check_platform(platform)?;

    let p = unsafe { &*platform };

    // according to spec it returns an error if the data contains a 0, but let's be safe.
    let d: Vec<u8> = match param_name {
        CL_PLATFORM_EXTENSIONS => cl_prop("cl_khr_icd"),
        CL_PLATFORM_EXTENSIONS_WITH_VERSION => {
            cl_prop::<Vec<cl_name_version>>(p.extensions.to_vec())
        }
        CL_PLATFORM_HOST_TIMER_RESOLUTION => cl_prop::<cl_ulong>(0),
        CL_PLATFORM_ICD_SUFFIX_KHR => cl_prop("MESA"),
        CL_PLATFORM_NAME => cl_prop("rusticl"),
        CL_PLATFORM_NUMERIC_VERSION => cl_prop::<cl_version>(CLVersion::Cl3_0 as u32),
        CL_PLATFORM_PROFILE => cl_prop("FULL_PROFILE"),
        CL_PLATFORM_VENDOR => cl_prop("Mesa/X.org"),
        CL_PLATFORM_VERSION => cl_prop("OpenCL 3.0"),
        // CL_INVALID_VALUE if param_name is not one of the supported values
        _ => return Err(CL_INVALID_VALUE),
    };

    let size: usize = d.len();

    // CL_INVALID_VALUE [...] if size in bytes specified
    // by param_value_size is < size of return type as specified in the OpenCL Platform Queries table,
    // and param_value is not a NULL value.
    if param_value_size < size && !param_value.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // param_value_size_ret returns the actual size in bytes of data being queried by param_name. If
    // param_value_size_ret is NULL, it is ignored.
    param_value_size_ret.write_checked(size);

    // param_value is a pointer to memory location where appropriate values for a given
    // param_name, as specified in the Platform Queries table, will be returned. If param_value is NULL,
    // it is ignored.
    unsafe {
        param_value.copy_checked(d.as_ptr() as *const ::std::ffi::c_void, size);
    }

    Ok(())
}

#[test]
fn test_get_platform_info() {
    let mut s: usize = 0;
    let mut r = get_platform_info(
        ptr::null(),
        CL_PLATFORM_EXTENSIONS,
        0,
        ptr::null_mut(),
        &mut s,
    );
    assert!(r.is_ok());
    assert!(s > 0);

    let mut v: Vec<u8> = vec![0; s];
    r = get_platform_info(
        ptr::null(),
        CL_PLATFORM_EXTENSIONS,
        s,
        v.as_mut_ptr() as *mut ::std::ffi::c_void,
        &mut s,
    );

    assert!(r.is_ok());
    assert_eq!(s, v.len());
    assert!(!v[0..s - 2].contains(&0));
    assert_eq!(v[s - 1], 0);
}

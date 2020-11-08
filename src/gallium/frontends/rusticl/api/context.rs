extern crate mesa_rust_util;

use crate::api::bindings::*;
use crate::api::device::check_device;
use crate::api::device::get_devs_for_type;
use crate::api::icd::DISPATCH;
use crate::api::platform::check_platform;
use crate::api::types::*;
use crate::api::util::*;

use self::mesa_rust_util::properties::Properties;
use self::mesa_rust_util::ptr::CheckedPtr;

use std::os::raw::c_void;
use std::ptr;
use std::sync::atomic::AtomicU32;
use std::sync::atomic::Ordering;

#[repr(C)]
#[allow(non_camel_case_types)]
pub struct _cl_context {
    dispatch: *const cl_icd_dispatch,
    devs: Vec<cl_device_id>,
    properties: Vec<cl_context_properties>,
    refs: AtomicU32,
}

impl _cl_context {
    fn new(devs: Vec<cl_device_id>, properties: Vec<cl_context_properties>) -> Box<Self> {
        Box::new(Self {
            dispatch: &DISPATCH,
            devs,
            properties,
            refs: AtomicU32::new(1),
        })
    }
}

fn check_context(context: cl_context) -> Result<(), cl_int> {
    if context.is_null() {
        return Err(CL_INVALID_CONTEXT);
    }
    Ok(())
}

pub fn create_context(
    properties: *const cl_context_properties,
    num_devices: cl_uint,
    devices: *const cl_device_id,
    pfn_notify: CreateContextCB,
    user_data: *mut ::std::os::raw::c_void,
) -> Result<cl_context, cl_int> {
    // CL_INVALID_VALUE if devices is NULL.
    if devices.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_VALUE if num_devices is equal to zero.
    if num_devices == 0 {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_VALUE if pfn_notify is NULL but user_data is not NULL.
    if pfn_notify.is_none() && !user_data.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_PROPERTY [...] if the same property name is specified more than once.
    let props = Properties::from_ptr(properties).ok_or(CL_INVALID_PROPERTY)?;
    for p in props.props {
        match p.0 as u32 {
            // CL_INVALID_PLATFORM [...] if platform value specified in properties is not a valid platform.
            CL_CONTEXT_PLATFORM => {
                check_platform(p.1 as cl_platform_id)?;
            }
            CL_CONTEXT_INTEROP_USER_SYNC => check_cl_bool(p.1).ok_or(CL_INVALID_PROPERTY)?,
            // CL_INVALID_PROPERTY if context property name in properties is not a supported property name
            _ => return Err(CL_INVALID_PROPERTY),
        }
    }

    let mut devs: Vec<cl_device_id> = vec![ptr::null(); num_devices as usize];
    for i in 0..num_devices as usize {
        devs[i] = unsafe { *devices.add(i) };
    }

    // Duplicate devices specified in devices are ignored.
    devs.dedup();

    for d in &devs {
        // CL_INVALID_DEVICE if any device in devices is not a valid device.
        check_device(*d)?;
    }

    Ok(Box::into_raw(_cl_context::new(
        devs,
        Properties::from_ptr_raw(properties),
    )))
}

pub fn create_context_from_type(
    properties: *const cl_context_properties,
    device_type: cl_device_type,
    pfn_notify: CreateContextCB,
    user_data: *mut ::std::os::raw::c_void,
) -> Result<cl_context, cl_int> {
    // CL_INVALID_DEVICE_TYPE if device_type is not a valid value.
    check_cl_device_type(device_type)?;

    // need to convert the Vec a little from & to *const
    let devs: Vec<cl_device_id> = get_devs_for_type(device_type)
        .into_iter()
        .map(|d| -> cl_device_id { d })
        .collect();
    // CL_DEVICE_NOT_FOUND if no devices that match device_type and property values specified in properties were found.
    if devs.is_empty() {
        return Err(CL_DEVICE_NOT_FOUND);
    }

    // errors are essentially the same and we will always pass in a valide
    // device list, so that's fine as well.
    create_context(
        properties,
        devs.len() as u32,
        devs.as_ptr(),
        pfn_notify,
        user_data,
    )
}

pub fn retain_context(context: cl_context) -> Result<(), cl_int> {
    // CL_INVALID_CONTEXT if context is not a valid OpenCL context.
    check_context(context)?;
    unsafe { &*context }.refs.fetch_add(1, Ordering::AcqRel);

    Ok(())
}

pub fn release_context(context: cl_context) -> Result<(), cl_int> {
    // CL_INVALID_CONTEXT if context is not a valid OpenCL context.
    check_context(context)?;

    unsafe {
        if (*context).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
            Box::from_raw(context);
        }
    }

    Ok(())
}

pub fn get_context_info(
    context: cl_context,
    param_name: cl_context_info,
    param_value_size: usize,
    param_value: *mut ::std::os::raw::c_void,
    param_value_size_ret: *mut usize,
) -> Result<(), cl_int> {
    // CL_INVALID_CONTEXT if context is not a valid context.
    check_context(context)?;

    let context = unsafe { &*context };

    let d: Vec<u8> = match param_name {
        CL_CONTEXT_DEVICES => cl_prop::<&Vec<cl_device_id>>(&context.devs),
        CL_CONTEXT_NUM_DEVICES => cl_prop::<cl_uint>(context.devs.len() as u32),
        CL_CONTEXT_PROPERTIES => cl_prop::<Vec<cl_context_properties>>(Vec::new()),
        CL_CONTEXT_REFERENCE_COUNT => cl_prop::<cl_uint>(context.refs.load(Ordering::Relaxed)),
        // CL_INVALID_VALUE if param_name is not one of the supported values
        _ => return Err(CL_INVALID_VALUE),
    };

    let size: usize = d.len();

    // CL_INVALID_VALUE [...] if size in bytes specified by param_value_size is < size of return
    // type as specified in the Context Attributes table and param_value is not a NULL value.
    if param_value_size < size && !param_value.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // param_value_size_ret returns the actual size in bytes of data being queried by param_name.
    // If param_value_size_ret is NULL, it is ignored.
    param_value_size_ret.write_checked(size);

    // param_value is a pointer to memory where the appropriate result being queried is returned.
    // If param_value is NULL, it is ignored.
    unsafe {
        param_value.copy_checked(d.as_ptr() as *const c_void, size);
    }

    Ok(())
}

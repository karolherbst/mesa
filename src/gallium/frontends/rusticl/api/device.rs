extern crate mesa_rust_util;

use crate::api::bindings::*;
use crate::api::icd::DISPATCH;
use crate::api::platform::check_platform;
use crate::api::platform::get_platform;
use crate::api::util::*;
use crate::core::device::*;

use self::mesa_rust_util::ptr::*;

use std::cmp::min;
use std::mem::size_of;
use std::ptr;
use std::sync::Once;

#[repr(C)]
#[allow(non_camel_case_types)]
pub struct _cl_device_id {
    dispatch: *const cl_icd_dispatch,
    dev: Device,
}

impl _cl_device_id {
    fn new(dev: Device) -> Self {
        Self {
            dispatch: &DISPATCH,
            dev,
        }
    }
}

static mut DEVICES: Vec<_cl_device_id> = Vec::new();
static INIT: Once = Once::new();

fn load_devices() {
    Device::all().into_iter().for_each(|d| unsafe {
        DEVICES.push(_cl_device_id::new(d));
    });
}

fn devs() -> &'static Vec<_cl_device_id> {
    INIT.call_once(load_devices);
    unsafe { &DEVICES }
}

pub fn get_devs_for_type(device_type: cl_device_type) -> Vec<&'static _cl_device_id> {
    devs().iter()
        .filter(|d| match device_type as u32 {
            CL_DEVICE_TYPE_ACCELERATOR => device_type & d.dev.device_type(),
            CL_DEVICE_TYPE_CPU => device_type & d.dev.device_type(),
            CL_DEVICE_TYPE_CUSTOM => device_type & d.dev.device_type(),
            CL_DEVICE_TYPE_DEFAULT => device_type & d.dev.device_type(),
            CL_DEVICE_TYPE_GPU => device_type & d.dev.device_type(),
            CL_DEVICE_TYPE_ALL => CL_DEVICE_TYPE_CUSTOM as u64 & !d.dev.device_type(),
            _ => 0,
        } != 0)
        .collect()
}

pub fn check_device(device: cl_device_id) -> Result<(), cl_int> {
    if device.is_null() {
        return Err(CL_INVALID_DEVICE);
    }

    for dev in unsafe { &DEVICES } {
        if dev as *const _ == device {
            return Ok(());
        }
    }

    Err(CL_INVALID_DEVICE)
}

pub fn get_device_ids(
    platform: cl_platform_id,
    device_type: cl_device_type,
    num_entries: cl_uint,
    devices: *mut cl_device_id,
    num_devices: *mut cl_uint,
) -> Result<(), cl_int> {
    // CL_INVALID_PLATFORM if platform is not a valid platform.
    check_platform(platform)?;

    // CL_INVALID_DEVICE_TYPE if device_type is not a valid value.
    check_cl_device_type(device_type)?;

    // CL_INVALID_VALUE if num_entries is equal to zero and devices is not NULL
    if num_entries == 0 && !devices.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_VALUE [...] if both num_devices and devices are NULL.
    if num_devices.is_null() && devices.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    let devs = get_devs_for_type(device_type);
    // CL_DEVICE_NOT_FOUND if no OpenCL devices that matched device_type were found
    if devs.is_empty() {
        return Err(CL_DEVICE_NOT_FOUND);
    }

    // num_devices returns the number of OpenCL devices available that match device_type. If
    // num_devices is NULL, this argument is ignored.
    num_devices.write_checked(devs.len() as cl_uint);

    if !devices.is_null() {
        let n = min(num_entries as usize, devs.len());

        #[allow(clippy::needless_range_loop)]
        for i in 0..n {
            unsafe {
                *devices.add(i) = devs[i];
            }
        }
    }

    Ok(())
}

pub fn get_device_info(
    device: cl_device_id,
    param_name: cl_device_info,
    param_value_size: usize,
    param_value: *mut ::std::os::raw::c_void,
    param_value_size_ret: *mut usize,
) -> Result<(), cl_int> {
    // CL_INVALID_DEVICE if device is not a valid device.
    check_device(device)?;

    let dev = &unsafe { &*device }.dev;
    let d: Vec<u8> = match param_name {
        CL_DEVICE_ADDRESS_BITS => cl_prop::<cl_uint>(dev.address_bits()),
        CL_DEVICE_ATOMIC_FENCE_CAPABILITIES => cl_prop::<cl_device_atomic_capabilities>(0),
        CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES => cl_prop::<cl_device_atomic_capabilities>(0),
        CL_DEVICE_AVAILABLE => cl_prop::<bool>(true),
        CL_DEVICE_BUILT_IN_KERNELS => cl_prop::<&str>(""),
        CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION => cl_prop::<Vec<cl_name_version>>(Vec::new()),
        CL_DEVICE_COMPILER_AVAILABLE => cl_prop::<bool>(true),
        CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES => {
            cl_prop::<cl_device_device_enqueue_capabilities>(0)
        }
        CL_DEVICE_DOUBLE_FP_CONFIG => cl_prop::<cl_device_fp_config>(0),
        CL_DEVICE_ENDIAN_LITTLE => cl_prop::<bool>(dev.little_endian()),
        CL_DEVICE_ERROR_CORRECTION_SUPPORT => cl_prop::<bool>(false),
        CL_DEVICE_EXECUTION_CAPABILITIES => {
            cl_prop::<cl_device_exec_capabilities>(CL_EXEC_KERNEL.into())
        }
        CL_DEVICE_EXTENSIONS => cl_prop::<&str>(&dev.extension_string),
        CL_DEVICE_EXTENSIONS_WITH_VERSION => cl_prop::<&Vec<cl_name_version>>(&dev.extensions),
        CL_DEVICE_GENERIC_ADDRESS_SPACE_SUPPORT => cl_prop::<bool>(false),
        CL_DEVICE_GLOBAL_MEM_CACHE_TYPE => cl_prop::<cl_device_mem_cache_type>(CL_NONE),
        CL_DEVICE_GLOBAL_MEM_CACHE_SIZE => cl_prop::<cl_ulong>(0),
        CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE => cl_prop::<cl_uint>(0),
        CL_DEVICE_GLOBAL_MEM_SIZE => cl_prop::<cl_ulong>(dev.global_mem_size()),
        CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE => cl_prop::<usize>(0),
        CL_DEVICE_HOST_UNIFIED_MEMORY => cl_prop::<bool>(dev.unified_memory()),
        CL_DEVICE_IL_VERSION => cl_prop::<&str>(""),
        CL_DEVICE_ILS_WITH_VERSION => cl_prop::<Vec<cl_name_version>>(Vec::new()),
        CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT => cl_prop::<cl_uint>(0),
        CL_DEVICE_IMAGE_MAX_ARRAY_SIZE => cl_prop::<usize>(dev.image_array_size()),
        CL_DEVICE_IMAGE_MAX_BUFFER_SIZE => cl_prop::<usize>(dev.image_buffer_size()),
        CL_DEVICE_IMAGE_PITCH_ALIGNMENT => cl_prop::<cl_uint>(0),
        CL_DEVICE_IMAGE_SUPPORT => cl_prop::<bool>(dev.image_supported()),
        CL_DEVICE_IMAGE2D_MAX_HEIGHT => cl_prop::<usize>(dev.image_2d_size()),
        CL_DEVICE_IMAGE2D_MAX_WIDTH => cl_prop::<usize>(dev.image_2d_size()),
        CL_DEVICE_IMAGE3D_MAX_HEIGHT => cl_prop::<usize>(dev.image_3d_size()),
        CL_DEVICE_IMAGE3D_MAX_WIDTH => cl_prop::<usize>(dev.image_3d_size()),
        CL_DEVICE_IMAGE3D_MAX_DEPTH => cl_prop::<usize>(dev.image_3d_size()),
        CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS => cl_prop::<bool>(false),
        CL_DEVICE_LATEST_CONFORMANCE_VERSION_PASSED => cl_prop::<&str>(""),
        CL_DEVICE_LINKER_AVAILABLE => cl_prop::<bool>(true),
        CL_DEVICE_LOCAL_MEM_SIZE => cl_prop::<cl_ulong>(dev.local_mem_size()),
        // TODO add query for CL_LOCAL vs CL_GLOBAL
        CL_DEVICE_LOCAL_MEM_TYPE => cl_prop::<cl_device_local_mem_type>(CL_GLOBAL),
        CL_DEVICE_MAX_CLOCK_FREQUENCY => cl_prop::<cl_uint>(dev.max_clock_freq()),
        CL_DEVICE_MAX_COMPUTE_UNITS => cl_prop::<cl_uint>(dev.max_compute_units()),
        // TODO atm implemented as mem_const
        CL_DEVICE_MAX_CONSTANT_ARGS => cl_prop::<cl_uint>(1024),
        CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE => cl_prop::<cl_ulong>(dev.const_max_size()),
        CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE => cl_prop::<usize>(0),
        CL_DEVICE_MAX_MEM_ALLOC_SIZE => cl_prop::<cl_ulong>(dev.max_mem_alloc()),
        CL_DEVICE_MAX_NUM_SUB_GROUPS => cl_prop::<cl_uint>(0),
        CL_DEVICE_MAX_ON_DEVICE_EVENTS => cl_prop::<cl_uint>(0),
        CL_DEVICE_MAX_ON_DEVICE_QUEUES => cl_prop::<cl_uint>(0),
        CL_DEVICE_MAX_PARAMETER_SIZE => cl_prop::<usize>(dev.param_max_size()),
        CL_DEVICE_MAX_PIPE_ARGS => cl_prop::<cl_uint>(0),
        CL_DEVICE_MAX_READ_IMAGE_ARGS => cl_prop::<cl_uint>(dev.image_read_count()),
        CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS => cl_prop::<cl_uint>(0),
        CL_DEVICE_MAX_SAMPLERS => cl_prop::<cl_uint>(dev.max_samplers()),
        CL_DEVICE_MAX_WORK_GROUP_SIZE => cl_prop::<usize>(dev.max_threads_per_block()),
        CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS => cl_prop::<cl_uint>(dev.max_grid_dimensions()),
        CL_DEVICE_MAX_WORK_ITEM_SIZES => cl_prop::<Vec<usize>>(dev.max_block_sizes()),
        CL_DEVICE_MAX_WRITE_IMAGE_ARGS => cl_prop::<cl_uint>(dev.image_write_count()),
        // TODO proper retrival from devices
        CL_DEVICE_MEM_BASE_ADDR_ALIGN => cl_prop::<cl_uint>(0x1000),
        CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE => {
            cl_prop::<cl_uint>(size_of::<cl_ulong16>() as cl_uint)
        }
        CL_DEVICE_NAME => cl_prop(dev.screen().name()),
        CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR => cl_prop::<cl_uint>(1),
        CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE => cl_prop::<cl_uint>(0),
        CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT => cl_prop::<cl_uint>(1),
        CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF => cl_prop::<cl_uint>(0),
        CL_DEVICE_NATIVE_VECTOR_WIDTH_INT => cl_prop::<cl_uint>(1),
        CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG => cl_prop::<cl_uint>(1),
        CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT => cl_prop::<cl_uint>(1),
        CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT => cl_prop::<bool>(false),
        CL_DEVICE_NUMERIC_VERSION => cl_prop::<cl_version>(dev.cl_version as cl_version),
        // TODO subdevice support
        CL_DEVICE_PARENT_DEVICE => cl_prop::<cl_device_id>(ptr::null()),
        CL_DEVICE_PARTITION_AFFINITY_DOMAIN => cl_prop::<cl_device_affinity_domain>(0),
        CL_DEVICE_PARTITION_MAX_SUB_DEVICES => cl_prop::<cl_uint>(0),
        CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS => cl_prop::<cl_uint>(0),
        CL_DEVICE_PIPE_MAX_PACKET_SIZE => cl_prop::<cl_uint>(0),
        CL_DEVICE_PIPE_SUPPORT => cl_prop::<bool>(false),
        CL_DEVICE_PLATFORM => cl_prop::<cl_platform_id>(get_platform()),
        CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT => cl_prop::<cl_uint>(0),
        CL_DEVICE_PREFERRED_INTEROP_USER_SYNC => cl_prop::<bool>(true),
        CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT => cl_prop::<cl_uint>(0),
        CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT => cl_prop::<cl_uint>(0),
        CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR => cl_prop::<cl_uint>(1),
        CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE => cl_prop::<cl_uint>(0),
        CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT => cl_prop::<cl_uint>(1),
        CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF => cl_prop::<cl_uint>(0),
        CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT => cl_prop::<cl_uint>(1),
        CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG => cl_prop::<cl_uint>(1),
        CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT => cl_prop::<cl_uint>(1),
        CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE => cl_prop::<usize>(1),
        // TODO
        CL_DEVICE_PRINTF_BUFFER_SIZE => cl_prop::<usize>(0),
        // TODO
        CL_DEVICE_PROFILING_TIMER_RESOLUTION => cl_prop::<usize>(0),
        CL_DEVICE_OPENCL_C_FEATURES => cl_prop::<Vec<cl_name_version>>(Vec::new()),
        CL_DEVICE_OPENCL_C_VERSION => cl_prop::<String>(dev.clc_version.into()),
        CL_DEVICE_OPENCL_C_ALL_VERSIONS => cl_prop::<&Vec<cl_name_version>>(&dev.clc_versions),
        CL_DEVICE_PROFILE => cl_prop(if dev.embedded {
            "EMBEDDED_PROFILE"
        } else {
            "FULL_PROFILE"
        }),
        CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE => cl_prop::<cl_uint>(0),
        CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE => cl_prop::<cl_uint>(0),
        CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES => cl_prop::<cl_command_queue_properties>(0),
        CL_DEVICE_QUEUE_PROPERTIES => {
            cl_prop::<cl_command_queue_properties>(CL_QUEUE_PROFILING_ENABLE.into())
        }
        // TODO sub devices
        CL_DEVICE_REFERENCE_COUNT => cl_prop::<cl_uint>(1),
        CL_DEVICE_SINGLE_FP_CONFIG => cl_prop::<cl_device_fp_config>(
            (CL_FP_ROUND_TO_ZERO | CL_FP_ROUND_TO_NEAREST) as cl_device_fp_config,
        ),
        CL_DEVICE_SVM_CAPABILITIES => cl_prop::<cl_device_svm_capabilities>(0),
        CL_DEVICE_TYPE => cl_prop::<cl_device_type>(dev.device_type()),
        CL_DEVICE_VENDOR => cl_prop(dev.screen().device_vendor()),
        CL_DEVICE_VENDOR_ID => cl_prop::<cl_uint>(dev.vendor_id()),
        CL_DEVICE_VERSION => cl_prop::<String>({
            let r: Vec<String> = [String::from("OpenCL "), dev.cl_version.into()].to_vec();
            r.concat()
        }),
        CL_DRIVER_VERSION => cl_prop("0.1"),
        CL_DEVICE_WORK_GROUP_COLLECTIVE_FUNCTIONS_SUPPORT => cl_prop::<bool>(false),
        // CL_INVALID_VALUE if param_name is not one of the supported values
        // CL_INVALID_VALUE [...] if param_name is a value that is available as an extension and the corresponding extension is not supported by the device.
        _ => return Err(CL_INVALID_VALUE),
    };

    let size: usize = d.len();

    // CL_INVALID_VALUE [...] if size in bytes specified by param_value_size is < size of return type as specified in the Device Queries table and param_value is not a NULL value
    if param_value_size < size && !param_value.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // param_value_size_ret returns the actual size in bytes of data being queried by param_name. If
    // param_value_size_ret is NULL, it is ignored.
    param_value_size_ret.write_checked(size);

    // param_value is a pointer to memory location where appropriate values for a given param_name, as specified in the Device Queries table, will be returned. If param_value is NULL, it is ignored.
    unsafe {
        param_value.copy_checked(d.as_ptr() as *const ::std::ffi::c_void, size);
    }

    Ok(())
}

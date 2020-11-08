#![allow(non_snake_case)]

extern crate mesa_rust_util;

use crate::api::bindings::*;
use crate::api::context::*;
use crate::api::device::*;
use crate::api::platform::*;
use crate::api::types::*;

use self::mesa_rust_util::ptr::*;

use std::ffi::CStr;
use std::ptr;

pub const DISPATCH: cl_icd_dispatch = cl_icd_dispatch {
    clGetPlatformIDs: Some(cl_get_platform_ids),
    clGetPlatformInfo: Some(cl_get_platform_info),
    clGetDeviceIDs: Some(cl_get_device_ids),
    clGetDeviceInfo: Some(cl_get_device_info),
    clCreateContext: Some(cl_create_context),
    clCreateContextFromType: Some(cl_create_context_from_type),
    clRetainContext: Some(cl_retain_context),
    clReleaseContext: Some(cl_release_context),
    clGetContextInfo: Some(cl_get_context_info),
    clCreateCommandQueue: None,
    clRetainCommandQueue: None,
    clReleaseCommandQueue: None,
    clGetCommandQueueInfo: None,
    clSetCommandQueueProperty: None,
    clCreateBuffer: None,
    clCreateImage2D: None,
    clCreateImage3D: None,
    clRetainMemObject: None,
    clReleaseMemObject: None,
    clGetSupportedImageFormats: Some(cl_get_supported_image_formats),
    clGetMemObjectInfo: None,
    clGetImageInfo: None,
    clCreateSampler: None,
    clRetainSampler: None,
    clReleaseSampler: None,
    clGetSamplerInfo: None,
    clCreateProgramWithSource: Some(cl_create_program_with_source),
    clCreateProgramWithBinary: None,
    clRetainProgram: None,
    clReleaseProgram: None,
    clBuildProgram: None,
    clUnloadCompiler: None,
    clGetProgramInfo: None,
    clGetProgramBuildInfo: None,
    clCreateKernel: None,
    clCreateKernelsInProgram: None,
    clRetainKernel: None,
    clReleaseKernel: None,
    clSetKernelArg: None,
    clGetKernelInfo: None,
    clGetKernelWorkGroupInfo: None,
    clWaitForEvents: None,
    clGetEventInfo: None,
    clRetainEvent: None,
    clReleaseEvent: None,
    clGetEventProfilingInfo: None,
    clFlush: None,
    clFinish: None,
    clEnqueueReadBuffer: None,
    clEnqueueWriteBuffer: None,
    clEnqueueCopyBuffer: None,
    clEnqueueReadImage: None,
    clEnqueueWriteImage: None,
    clEnqueueCopyImage: None,
    clEnqueueCopyImageToBuffer: None,
    clEnqueueCopyBufferToImage: None,
    clEnqueueMapBuffer: None,
    clEnqueueMapImage: None,
    clEnqueueUnmapMemObject: None,
    clEnqueueNDRangeKernel: None,
    clEnqueueTask: None,
    clEnqueueNativeKernel: None,
    clEnqueueMarker: None,
    clEnqueueWaitForEvents: None,
    clEnqueueBarrier: None,
    clGetExtensionFunctionAddress: Some(cl_get_extension_function_address),
    clCreateFromGLBuffer: None,
    clCreateFromGLTexture2D: None,
    clCreateFromGLTexture3D: None,
    clCreateFromGLRenderbuffer: None,
    clGetGLObjectInfo: None,
    clGetGLTextureInfo: None,
    clEnqueueAcquireGLObjects: None,
    clEnqueueReleaseGLObjects: None,
    clGetGLContextInfoKHR: None,
    clGetDeviceIDsFromD3D10KHR: ptr::null_mut(),
    clCreateFromD3D10BufferKHR: ptr::null_mut(),
    clCreateFromD3D10Texture2DKHR: ptr::null_mut(),
    clCreateFromD3D10Texture3DKHR: ptr::null_mut(),
    clEnqueueAcquireD3D10ObjectsKHR: ptr::null_mut(),
    clEnqueueReleaseD3D10ObjectsKHR: ptr::null_mut(),
    clSetEventCallback: None,
    clCreateSubBuffer: None,
    clSetMemObjectDestructorCallback: None,
    clCreateUserEvent: None,
    clSetUserEventStatus: None,
    clEnqueueReadBufferRect: None,
    clEnqueueWriteBufferRect: None,
    clEnqueueCopyBufferRect: None,
    clCreateSubDevicesEXT: None,
    clRetainDeviceEXT: None,
    clReleaseDeviceEXT: None,
    clCreateEventFromGLsyncKHR: None,
    clCreateSubDevices: None,
    clRetainDevice: None,
    clReleaseDevice: None,
    clCreateImage: None,
    clCreateProgramWithBuiltInKernels: None,
    clCompileProgram: None,
    clLinkProgram: None,
    clUnloadPlatformCompiler: None,
    clGetKernelArgInfo: None,
    clEnqueueFillBuffer: None,
    clEnqueueFillImage: None,
    clEnqueueMigrateMemObjects: None,
    clEnqueueMarkerWithWaitList: None,
    clEnqueueBarrierWithWaitList: None,
    clGetExtensionFunctionAddressForPlatform: None,
    clCreateFromGLTexture: None,
    clGetDeviceIDsFromD3D11KHR: ptr::null_mut(),
    clCreateFromD3D11BufferKHR: ptr::null_mut(),
    clCreateFromD3D11Texture2DKHR: ptr::null_mut(),
    clCreateFromD3D11Texture3DKHR: ptr::null_mut(),
    clCreateFromDX9MediaSurfaceKHR: ptr::null_mut(),
    clEnqueueAcquireD3D11ObjectsKHR: ptr::null_mut(),
    clEnqueueReleaseD3D11ObjectsKHR: ptr::null_mut(),
    clGetDeviceIDsFromDX9MediaAdapterKHR: ptr::null_mut(),
    clEnqueueAcquireDX9MediaSurfacesKHR: ptr::null_mut(),
    clEnqueueReleaseDX9MediaSurfacesKHR: ptr::null_mut(),
    clCreateFromEGLImageKHR: None,
    clEnqueueAcquireEGLObjectsKHR: None,
    clEnqueueReleaseEGLObjectsKHR: None,
    clCreateEventFromEGLSyncKHR: None,
    clCreateCommandQueueWithProperties: None,
    clCreatePipe: None,
    clGetPipeInfo: None,
    clSVMAlloc: None,
    clSVMFree: None,
    clEnqueueSVMFree: None,
    clEnqueueSVMMemcpy: None,
    clEnqueueSVMMemFill: None,
    clEnqueueSVMMap: None,
    clEnqueueSVMUnmap: None,
    clCreateSamplerWithProperties: None,
    clSetKernelArgSVMPointer: None,
    clSetKernelExecInfo: None,
    clGetKernelSubGroupInfoKHR: None,
    clCloneKernel: None,
    clCreateProgramWithIL: None,
    clEnqueueSVMMigrateMem: None,
    clGetDeviceAndHostTimer: None,
    clGetHostTimer: None,
    clGetKernelSubGroupInfo: None,
    clSetDefaultDeviceCommandQueue: None,
    clSetProgramReleaseCallback: None,
    clSetProgramSpecializationConstant: None,
    clCreateBufferWithProperties: None,
    clCreateImageWithProperties: None,
    clSetContextDestructorCallback: None,
};

// We need those functions exported

#[no_mangle]
extern "C" fn clGetPlatformInfo(
    platform: cl_platform_id,
    param_name: cl_platform_info,
    param_value_size: usize,
    param_value: *mut ::std::ffi::c_void,
    param_value_size_ret: *mut usize,
) -> cl_int {
    cl_get_platform_info(
        platform,
        param_name,
        param_value_size,
        param_value,
        param_value_size_ret,
    )
}

#[no_mangle]
extern "C" fn clGetExtensionFunctionAddress(
    function_name: *const ::std::os::raw::c_char,
) -> *mut ::std::ffi::c_void {
    cl_get_extension_function_address(function_name)
}

#[no_mangle]
extern "C" fn clIcdGetPlatformIDsKHR(
    num_entries: cl_uint,
    platforms: *mut cl_platform_id,
    num_platforms: *mut cl_uint,
) -> cl_int {
    cl_icd_get_platform_ids_khr(num_entries, platforms, num_platforms)
}

// extern "C" function stubs in ICD and extension order

extern "C" fn cl_get_platform_ids(
    num_entries: cl_uint,
    platforms: *mut cl_platform_id,
    num_platforms: *mut cl_uint,
) -> cl_int {
    match get_platform_ids(num_entries, platforms, num_platforms) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

extern "C" fn cl_get_platform_info(
    platform: cl_platform_id,
    param_name: cl_platform_info,
    param_value_size: usize,
    param_value: *mut ::std::ffi::c_void,
    param_value_size_ret: *mut usize,
) -> cl_int {
    match get_platform_info(
        platform,
        param_name,
        param_value_size,
        param_value,
        param_value_size_ret,
    ) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

extern "C" fn cl_get_device_ids(
    platform: cl_platform_id,
    device_type: cl_device_type,
    num_entries: cl_uint,
    devices: *mut cl_device_id,
    num_devices: *mut cl_uint,
) -> cl_int {
    match get_device_ids(platform, device_type, num_entries, devices, num_devices) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

extern "C" fn cl_get_device_info(
    device: cl_device_id,
    param_name: cl_device_info,
    param_value_size: usize,
    param_value: *mut ::std::os::raw::c_void,
    param_value_size_ret: *mut usize,
) -> cl_int {
    match get_device_info(
        device,
        param_name,
        param_value_size,
        param_value,
        param_value_size_ret,
    ) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

extern "C" fn cl_create_context(
    properties: *const cl_context_properties,
    num_devices: cl_uint,
    devices: *const cl_device_id,
    pfn_notify: CreateContextCB,
    user_data: *mut ::std::os::raw::c_void,
    errcode_ret: *mut cl_int,
) -> cl_context {
    match create_context(properties, num_devices, devices, pfn_notify, user_data) {
        Ok(c) => {
            errcode_ret.write_checked(CL_SUCCESS as cl_int);
            c
        }
        Err(e) => {
            errcode_ret.write_checked(e);
            ptr::null_mut()
        }
    }
}

extern "C" fn cl_create_context_from_type(
    properties: *const cl_context_properties,
    device_type: cl_device_type,
    pfn_notify: CreateContextCB,
    user_data: *mut ::std::ffi::c_void,
    errcode_ret: *mut cl_int,
) -> cl_context {
    match create_context_from_type(properties, device_type, pfn_notify, user_data) {
        Ok(c) => {
            errcode_ret.write_checked(CL_SUCCESS as cl_int);
            c
        }
        Err(e) => {
            errcode_ret.write_checked(e);
            ptr::null_mut()
        }
    }
}

extern "C" fn cl_retain_context(context: cl_context) -> cl_int {
    match retain_context(context) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

extern "C" fn cl_release_context(context: cl_context) -> cl_int {
    match release_context(context) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

extern "C" fn cl_get_context_info(
    context: cl_context,
    param_name: cl_context_info,
    param_value_size: usize,
    param_value: *mut ::std::os::raw::c_void,
    param_value_size_ret: *mut usize,
) -> cl_int {
    match get_context_info(
        context,
        param_name,
        param_value_size,
        param_value,
        param_value_size_ret,
    ) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

extern "C" fn cl_get_supported_image_formats(
    _context: cl_context,
    _flags: cl_mem_flags,
    _image_type: cl_mem_object_type,
    _num_entries: cl_uint,
    _image_formats: *mut cl_image_format,
    _num_image_formats: *mut cl_uint,
) -> cl_int {
    CL_INVALID_VALUE as cl_int
}

extern "C" fn cl_create_program_with_source(
    _context: cl_context,
    _count: cl_uint,
    _strings: *mut *const ::std::os::raw::c_char,
    _lengths: *const usize,
    errcode_ret: *mut cl_int,
) -> cl_program {
    errcode_ret.write_checked(CL_OUT_OF_HOST_MEMORY);
    ptr::null_mut()
}

extern "C" fn cl_get_extension_function_address(
    function_name: *const ::std::os::raw::c_char,
) -> *mut ::std::ffi::c_void {
    if function_name.is_null() {
        return ptr::null_mut();
    }
    match unsafe { CStr::from_ptr(function_name) }.to_str().unwrap() {
        "clGetPlatformInfo" => cl_get_platform_info as *mut std::ffi::c_void,
        "clIcdGetPlatformIDsKHR" => cl_icd_get_platform_ids_khr as *mut std::ffi::c_void,
        _ => ptr::null_mut(),
    }
}

// cl_khr_icd
extern "C" fn cl_icd_get_platform_ids_khr(
    num_entries: cl_uint,
    platforms: *mut cl_platform_id,
    num_platforms: *mut cl_uint,
) -> cl_int {
    match get_platform_ids(num_entries, platforms, num_platforms) {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}

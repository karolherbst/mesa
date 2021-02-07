extern crate mesa_rust_gen;
extern crate mesa_rust_util;

use crate::pipe::device::*;

use self::mesa_rust_gen::*;
use self::mesa_rust_util::string::*;

use std::convert::TryInto;
use std::os::raw::c_void;
use std::ptr;

pub struct PipeScreen {
    ldev: PipeLoaderDevice,
    screen: *mut pipe_screen,
}

// until we have a better solution
pub trait ComputeParam<T> {
    fn compute_param(&self, cap: pipe_compute_cap) -> T;
}

impl ComputeParam<u32> for PipeScreen {
    fn compute_param(&self, cap: pipe_compute_cap) -> u32 {
        let size = self.compute_param_wrapped(cap, ptr::null_mut());
        assert_eq!(size, 4);
        let mut d: [u8; 4] = [0; 4];
        self.compute_param_wrapped(cap, d.as_mut_ptr() as *mut c_void);
        u32::from_ne_bytes(d)
    }
}

impl ComputeParam<u64> for PipeScreen {
    fn compute_param(&self, cap: pipe_compute_cap) -> u64 {
        let size = self.compute_param_wrapped(cap, ptr::null_mut());
        assert_eq!(size, 8);
        let mut d: [u8; 8] = [0; 8];
        self.compute_param_wrapped(cap, d.as_mut_ptr() as *mut c_void);
        u64::from_ne_bytes(d)
    }
}

impl ComputeParam<Vec<u64>> for PipeScreen {
    fn compute_param(&self, cap: pipe_compute_cap) -> Vec<u64> {
        let size = self.compute_param_wrapped(cap, ptr::null_mut());
        let elems = (size / 8) as usize;

        let mut res: Vec<u64> = Vec::new();
        let mut d: Vec<u8> = vec![0; size as usize];

        self.compute_param_wrapped(cap, d.as_mut_ptr() as *mut c_void);
        for i in 0..elems {
            let offset = i * 8;
            let slice = &d[offset..offset + 8];
            res.push(u64::from_ne_bytes(slice.try_into().expect("")));
        }
        res
    }
}

impl PipeScreen {
    pub(super) fn new(ldev: PipeLoaderDevice, screen: *mut pipe_screen) -> Option<Self> {
        if screen.is_null() || !has_required_cbs(screen) {
            return None;
        }

        Some(Self { ldev, screen })
    }

    pub fn param(&self, cap: pipe_cap) -> i32 {
        unsafe { (*self.screen).get_param.unwrap()(self.screen, cap) }
    }

    pub fn shader_param(&self, t: pipe_shader_type, cap: pipe_shader_cap) -> i32 {
        unsafe { (*self.screen).get_shader_param.unwrap()(self.screen, t, cap) }
    }

    fn compute_param_wrapped(&self, cap: pipe_compute_cap, ptr: *mut c_void) -> i32 {
        let s = &mut unsafe { *self.screen };
        unsafe {
            s.get_compute_param.unwrap()(self.screen, pipe_shader_ir::PIPE_SHADER_IR_NIR, cap, ptr)
        }
    }

    pub fn name(&self) -> String {
        unsafe {
            let s = *self.screen;
            c_string_to_string(s.get_name.unwrap()(self.screen))
        }
    }

    pub fn device_vendor(&self) -> String {
        unsafe {
            let s = *self.screen;
            c_string_to_string(s.get_device_vendor.unwrap()(self.screen))
        }
    }

    pub fn device_type(&self) -> pipe_loader_device_type {
        unsafe { *self.ldev.ldev }.type_
    }
}

impl Drop for PipeScreen {
    fn drop(&mut self) {
        unsafe {
            (*self.screen).destroy.unwrap()(self.screen);
        }
    }
}

fn has_required_cbs(screen: *mut pipe_screen) -> bool {
    let s = unsafe { *screen };
    s.destroy.is_some()
        && s.get_compute_param.is_some()
        && s.get_name.is_some()
        && s.get_param.is_some()
        && s.get_shader_param.is_some()
}

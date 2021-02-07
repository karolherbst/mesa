extern crate mesa_rust_gen;

use crate::pipe::screen::*;

use self::mesa_rust_gen::*;

use std::ptr;

pub(super) struct PipeLoaderDevice {
    pub(super) ldev: *mut pipe_loader_device,
}

impl PipeLoaderDevice {
    fn new(ldev: *mut pipe_loader_device) -> Self {
        Self { ldev }
    }

    fn load_screen(self) -> Option<PipeScreen> {
        let s = unsafe { pipe_loader_create_screen(self.ldev) };
        PipeScreen::new(self, s)
    }
}

impl Drop for PipeLoaderDevice {
    fn drop(&mut self) {
        unsafe {
            pipe_loader_release(&mut self.ldev, 1);
        }
    }
}

fn load_devs() -> Vec<PipeLoaderDevice> {
    let n = unsafe { pipe_loader_probe(ptr::null_mut(), 0) };
    let mut devices: Vec<*mut pipe_loader_device> = vec![ptr::null_mut(); n as usize];
    unsafe {
        pipe_loader_probe(devices.as_mut_ptr(), n);
    }

    devices.into_iter().map(PipeLoaderDevice::new).collect()
}

pub fn load_screens() -> Vec<PipeScreen> {
    load_devs()
        .into_iter()
        .filter_map(PipeLoaderDevice::load_screen)
        .collect()
}

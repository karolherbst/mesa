use crate::api::bindings::*;

use std::convert::TryInto;
use std::ffi::CString;
use std::mem::size_of;

pub trait CLProp {
    fn cl_vec(&self) -> Vec<u8>;
}

impl CLProp for bool {
    fn cl_vec(&self) -> Vec<u8> {
        cl_prop::<cl_bool>(if *self { CL_TRUE } else { CL_FALSE })
    }
}

impl CLProp for cl_char {
    fn cl_vec(&self) -> Vec<u8> {
        self.to_ne_bytes().to_vec()
    }
}

impl CLProp for cl_uint {
    fn cl_vec(&self) -> Vec<u8> {
        self.to_ne_bytes().to_vec()
    }
}

impl CLProp for cl_ulong {
    fn cl_vec(&self) -> Vec<u8> {
        self.to_ne_bytes().to_vec()
    }
}

impl CLProp for isize {
    fn cl_vec(&self) -> Vec<u8> {
        self.to_ne_bytes().to_vec()
    }
}

impl CLProp for usize {
    fn cl_vec(&self) -> Vec<u8> {
        self.to_ne_bytes().to_vec()
    }
}

impl CLProp for String {
    fn cl_vec(&self) -> Vec<u8> {
        let mut c = self.clone();
        c.push('\0');
        c.into_bytes()
    }
}

impl CLProp for &str {
    fn cl_vec(&self) -> Vec<u8> {
        CString::new(*self)
            .or_else(|_| CString::new(b"\0".to_vec()))
            .unwrap()
            .into_bytes_with_nul()
    }
}

impl CLProp for cl_name_version {
    fn cl_vec(&self) -> Vec<u8> {
        let mut v = Vec::new();
        v.append(&mut self.version.cl_vec());
        v.append(&mut self.name.to_vec().cl_vec());
        v.resize(size_of::<cl_name_version>(), 0);
        v
    }
}

impl<T> CLProp for Vec<T>
where
    T: CLProp,
{
    fn cl_vec(&self) -> Vec<u8> {
        let mut res: Vec<u8> = Vec::new();
        for i in self {
            res.append(&mut i.cl_vec())
        }
        res
    }
}

impl<T> CLProp for &Vec<T>
where
    T: CLProp,
{
    fn cl_vec(&self) -> Vec<u8> {
        let mut res: Vec<u8> = Vec::new();
        for i in *self {
            res.append(&mut i.cl_vec())
        }
        res
    }
}

impl<T> CLProp for *const T {
    fn cl_vec(&self) -> Vec<u8> {
        (*self as usize).cl_vec()
    }
}

pub fn cl_prop<T: CLProp>(v: T) -> Vec<u8> {
    v.cl_vec()
}

const CL_DEVICE_TYPES: [u32; 6] = [
    CL_DEVICE_TYPE_ACCELERATOR,
    CL_DEVICE_TYPE_ALL,
    CL_DEVICE_TYPE_CPU,
    CL_DEVICE_TYPE_CUSTOM,
    CL_DEVICE_TYPE_DEFAULT,
    CL_DEVICE_TYPE_GPU,
];

pub fn check_cl_device_type(val: cl_device_type) -> Result<(), cl_int> {
    let v: u32 = val.try_into().or(Err(CL_INVALID_DEVICE_TYPE))?;
    if CL_DEVICE_TYPES.contains(&v) {
        return Ok(());
    }
    Err(CL_INVALID_DEVICE_TYPE)
}

pub fn check_cl_bool<T: PartialEq + TryInto<cl_uint>>(val: T) -> Option<()> {
    let c: u32 = val.try_into().ok()?;
    if c != CL_TRUE && c != CL_FALSE {
        return None;
    }
    Some(())
}

#[test]
fn test_nul_string() {
    let r = str_as_nul_vec("d");
    assert!(r.is_ok());
    let s = r.unwrap();
    assert_eq!(s[0], 'd' as u8);
    assert_eq!(s[1], '\0' as u8);
}

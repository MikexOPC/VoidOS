use std::ffi::c_void;
use std::io;
use std::mem::size_of;
use std::marker::PhantomData;
use std::ptr::null;

const JOB_OBJECT_EXTENDED_LIMIT_INFORMATION: i32 = 9;
const JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: u32 = 0x2000;
const PROCESS_SYNCHRONIZE: u32 = 0x0010_0000;
const WAIT_TIMEOUT: u32 = 258;
const ES_CONTINUOUS: u32 = 0x8000_0000;
const ES_SYSTEM_REQUIRED: u32 = 1;

pub struct SystemAwake {
    previous: u32,
    thread: PhantomData<*mut ()>,
}

#[repr(C)]
#[derive(Default)]
struct BasicLimits {
    per_process_time: i64,
    per_job_time: i64,
    flags: u32,
    minimum_working_set: usize,
    maximum_working_set: usize,
    active_processes: u32,
    affinity: usize,
    priority: u32,
    scheduling: u32,
}

#[repr(C)]
#[derive(Default)]
struct IoCounters {
    values: [u64; 6],
}

#[repr(C)]
#[derive(Default)]
struct ExtendedLimits {
    basic: BasicLimits,
    io: IoCounters,
    process_memory: usize,
    job_memory: usize,
    peak_process_memory: usize,
    peak_job_memory: usize,
}

#[link(name = "kernel32")]
unsafe extern "system" {
    fn CreateJobObjectW(attributes: *const c_void, name: *const u16) -> *mut c_void;
    fn SetInformationJobObject(job: *mut c_void, class: i32, info: *const c_void, size: u32) -> i32;
    fn AssignProcessToJobObject(job: *mut c_void, process: *mut c_void) -> i32;
    fn GetCurrentProcess() -> *mut c_void;
    fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
    fn WaitForSingleObject(handle: *mut c_void, milliseconds: u32) -> u32;
    fn CloseHandle(handle: *mut c_void) -> i32;
    fn SetThreadExecutionState(flags: u32) -> u32;
}

// ====== SystemAwake ======

impl SystemAwake {
    pub fn new() -> io::Result<Self> {
        let previous = unsafe { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) };
        if previous == 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(Self { previous, thread: PhantomData })
    }
}

// ====== Drop ======

impl Drop for SystemAwake {
    fn drop(&mut self) {
        unsafe { SetThreadExecutionState(self.previous | ES_CONTINUOUS) };
    }
}

// ====== bind_children_to_runner ======

pub fn bind_children_to_runner() -> io::Result<()> {
    unsafe {
        let job = CreateJobObjectW(null(), null());
        if job.is_null() {
            return Err(io::Error::last_os_error());
        }
        let limits = ExtendedLimits {
            basic: BasicLimits { flags: JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, ..BasicLimits::default() },
            ..ExtendedLimits::default()
        };
        let configured = SetInformationJobObject(
            job,
            JOB_OBJECT_EXTENDED_LIMIT_INFORMATION,
            (&raw const limits).cast(),
            size_of::<ExtendedLimits>() as u32,
        );
        if configured == 0 || AssignProcessToJobObject(job, GetCurrentProcess()) == 0 {
            let error = io::Error::last_os_error();
            CloseHandle(job);
            return Err(error);
        }
    }
    Ok(())
}

// ====== process_alive ======

pub fn process_alive(pid: u32) -> bool {
    unsafe {
        let handle = OpenProcess(PROCESS_SYNCHRONIZE, 0, pid);
        if handle.is_null() {
            return false;
        }
        let alive = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
        CloseHandle(handle);
        alive
    }
}

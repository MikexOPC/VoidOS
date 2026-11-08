use std::fs::{File, OpenOptions};
use std::io::{BufRead, Write, stdin, stdout};
use std::path::PathBuf;
use std::time::Duration;

const FIRST: u64 = 0xffff_8000_0000_1000;
const LAST: u64 = 0xffff_8000_0000_1040;
const INSTRUCTION: u64 = 0xffff_8000_0000_2000;
const OUTSIDE: u64 = 0xffff_8000_0000_3000;

struct Files {
    serial: File,
    trace: File,
}

// ====== field ======

fn field(name: &str, value: u64) -> String {
    format!("{name}: 0x{value:016x}\n")
}

// ====== kernel_pf ======

fn kernel_pf(error: u64, rip: u64, cr2: u64) -> String {
    format!("     0: v=0e e={error:04x} i=0 cpl=0 IP=0038:{rip:016x} pc={rip:016x} SP=0030:0000000000409000 CR2={cr2:016x}\n")
}

// ====== emit ======

fn emit(file: &mut File, text: &str) {
    file.write_all(text.as_bytes()).expect("fake evidence");
    file.flush().expect("fake evidence");
}

// ====== registers ======

fn registers(rip: u64, halted: bool, rax: u64) {
    let mut out = stdout();
    let hlt = u8::from(halted);
    write!(
        out,
        "RAX={rax:016x} RBX=0000000000000000 RCX=0000000000000000 RDX=0000000000000000\r\n\
         RSI=0000000000000000 RDI=0000000000000000 RBP=0000000000000000 RSP=0000000000409000\r\n\
         R8 =0000000000000000 R9 =0000000000000000 R10=0000000000000000 R11=0000000000000000\r\n\
         R12=0000000000000000 R13=0000000000000000 R14=0000000000000000 R15=0000000000000000\r\n\
         RIP={rip:016x} RFL=00000002 [-------] CPL=0 II=0 A20=1 SMM=0 HLT={hlt}\r\n"
    ).expect("fake monitor");
    out.flush().expect("fake monitor");
}

// ====== arm ======

fn arm(files: &mut Files) {
    emit(&mut files.serial, "[ OK ] KERNEL_K5_SMEP_SMAP_SUPPORTED\n");
    emit(&mut files.serial, &field("K5_COPY_INSTRUCTION", INSTRUCTION));
    emit(&mut files.serial, &field("K5_COPY_OUTSIDE", OUTSIDE));
    emit(&mut files.serial, &field("K5_NMI_FIRST", FIRST));
    emit(&mut files.serial, &field("K5_NMI_LAST", LAST));
    emit(&mut files.serial, "\u{1b}[32m[ OK ]\u{1b}[0m KERNEL_K5_NMI_ARMED\n");
}

// ====== continue_copy ======

fn continue_copy(files: &mut Files, cr2: u64) {
    for (error, address) in [(0, 0x401000), (3, 0x406000), (0, 0x401000), (3, cr2)] {
        emit(&mut files.trace, &kernel_pf(error, INSTRUCTION, address));
    }
    emit(&mut files.trace, "     1: v=06 e=0000 i=0 cpl=3 IP=0033:0000000000401000 pc=0000000000401000\n");
    emit(&mut files.serial, &field("K5_COPY_FAULTS", 4));
    emit(&mut files.serial, "[ OK ] KERNEL_K5_COPY_OK\n[ OK ] KERNEL_READY\n");
}

// ====== run_positive ======

fn run_positive(files: &mut Files, cr2: u64, miss_first: bool) {
    arm(files);
    let mut misses = u32::from(miss_first);
    for line in stdin().lock().lines() {
        match line.expect("fake stdin").trim() {
            "info registers" if misses > 0 => {
                misses -= 1;
                registers(FIRST - 8, false, 0);
            }
            "info registers" => registers(FIRST, false, 0),
            "nmi" => continue_copy(files, cr2),
            _ => {}
        }
    }
    park();
}

// ====== run_negative ======

fn run_negative(files: &mut Files, error: Option<u64>, rip: u64) {
    arm(files);
    if let Some(error) = error {
        emit(&mut files.trace, &kernel_pf(error, rip, 0x401000));
    }
    let mut answered = false;
    for line in stdin().lock().lines() {
        if line.expect("fake stdin").trim() == "info registers" {
            registers(FIRST, answered, 0x4b35_4641);
            answered = true;
        }
    }
    park();
}

// ====== park ======

fn park() -> ! {
    loop {
        std::thread::sleep(Duration::from_secs(3600));
    }
}

// ====== main ======

fn main() {
    let arguments: Vec<String> = std::env::args().collect();
    let value_after = |name: &str| {
        let at = arguments.iter().position(|item| item == name).expect("fake argument");
        arguments[at + 1].clone()
    };
    let serial = PathBuf::from(value_after("-serial").trim_start_matches("file:"));
    let trace = PathBuf::from(value_after("-D"));
    let open = |path: &PathBuf| OpenOptions::new().create(true).append(true).open(path).expect("fake file");
    let mut files = Files { serial: open(&serial), trace: open(&trace) };
    let case = std::env::var("VOIDOS_FAKE_CASE").unwrap_or_default();
    match case.as_str() {
        "copy-ok" => run_positive(&mut files, 0x406000, false),
        "copy-late-window" => run_positive(&mut files, 0x406000, true),
        "copy-wrong-fault" => run_positive(&mut files, 0x407000, false),
        "negative-ok" => run_negative(&mut files, Some(3), INSTRUCTION),
        "negative-rip" => run_negative(&mut files, Some(0), OUTSIDE),
        "negative-wrong-rip" => run_negative(&mut files, Some(0), INSTRUCTION),
        "negative-clean" => run_negative(&mut files, None, 0),
        "negative-accepted" => {
            emit(&mut files.serial, "[ OK ] KERNEL_READY\n");
            run_negative(&mut files, Some(3), INSTRUCTION);
        }
        "panic" => {
            emit(&mut files.serial, "PANIC\n");
            park();
        }
        "triple" => {
            arm(&mut files);
            emit(&mut files.trace, "Triple fault\n");
            run_positive(&mut files, 0x406000, false);
        }
        "silent" => {
            for line in stdin().lock().lines() {
                let _ = line;
            }
            park();
        }
        "deaf" => park(),
        "exit-early" => {
            arm(&mut files);
            std::process::exit(3);
        }
        "exit-immediately" => std::process::exit(7),
        other => panic!("Unknown fake case: {other}"),
    }
}

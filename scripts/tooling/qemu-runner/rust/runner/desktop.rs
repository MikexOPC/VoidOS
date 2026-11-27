use crate::cli;
use crate::evidence::serial::SerialLog;
use crate::vm::job::{SystemAwake, bind_children_to_runner};
use crate::vm::machine::{Inject, Paths, Plan, arguments, prepare_esp};
use std::fs;
use std::io::Write;
use std::os::windows::process::CommandExt;
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

const CREATE_NO_WINDOW: u32 = 0x0800_0000;

// ====== wait_marker ======

fn wait_marker(child: &mut Child, serial: &Path, marker: &str) -> Result<(), String> {
    let deadline = Instant::now() + Duration::from_secs(120);
    while Instant::now() < deadline {
        let bytes = fs::read(serial).unwrap_or_default();
        let log = SerialLog::parse(&bytes);
        if log.terminal() || !log.failures().is_empty() {
            return Err(format!("Errore kernel, controllare {}", serial.display()));
        }
        if log.text.lines().any(|line| line.ends_with(marker)) {
            return Ok(());
        }
        if let Some(status) = child.try_wait().map_err(|error| error.to_string())? {
            return Err(format!("QEMU terminato ({status}), controllare stderr.log"));
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    Err(format!("Timeout in attesa di {marker}, controllare {}", serial.display()))
}

// ====== run ======

pub fn run(tcg: bool) -> Result<bool, String> {
    bind_children_to_runner().map_err(|error| error.to_string())?;
    let _awake = SystemAwake::new().map_err(|error| error.to_string())?;
    let executable = std::env::current_exe().map_err(|error| error.to_string())?;
    let root = cli::find_root(&executable).ok_or("Repository non trovato")?;
    let build = root.join("kernel/build/meson-kernel");
    for name in ["kernel.elf", "user-ui.elf", "user-init.elf", "user-terminal.elf", "user-sh.elf", "user-worker.elf", "user-bridge.elf", "user-sysmon.elf", "user-files.elf"] {
        if !build.join(name).is_file() {
            return Err(format!("Build incompleta: manca {name}"));
        }
    }
    let paths = Paths {
        qemu_dir: std::env::var_os("QEMU_DIR").map(Into::into)
            .unwrap_or_else(|| "C:/Program Files/qemu".into()),
        loader: root.join("arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI"),
    };
    let stamp = SystemTime::now().duration_since(UNIX_EPOCH).map_err(|error| error.to_string())?.as_nanos();
    let directory = root.join("kernel/build").join(format!("desktop-{stamp:x}"));
    prepare_esp(&directory, &build.join("kernel.elf"), &paths)?;
    fs::copy(paths.qemu_dir.join("share/edk2-i386-vars.fd"), directory.join("vars.fd"))
        .map_err(|error| format!("Firmware OVMF: {error}"))?;
    let plan = Plan { memory: 256, smp: 4, inject: Inject::None, cpu: "max", machine: "q35" };
    let original = arguments(&directory, &paths, &plan);
    let mut args = Vec::new();
    let mut index = 0;
    while index < original.len() {
        let option = &original[index];
        if option == "-d" || option == "-D" {
            index += 2;
            continue;
        }
        if option == "-vga" {
            args.push("-vga".into());
            args.push("none".into());
            args.push("-device".into());
            args.push("virtio-vga".into());
            index += 2;
            continue;
        }
        args.push(option.clone());
        if option == "-accel" || option == "-display" {
            args.push(if option == "-display" { "gtk,zoom-to-fit=on,show-menubar=off,show-tabs=off" }
                else if tcg { "tcg" } else { "whpx,kernel-irqchip=off" }.into());
            index += 2;
        } else {
            index += 1;
        }
    }
    println!("Avvio desktop VoidOS con {}. Log in {}", if tcg { "TCG" } else { "WHPX" }, directory.display());
    let mut child = Command::new(paths.qemu_dir.join("qemu-system-x86_64.exe"))
        .args(args).stdin(Stdio::piped())
        .stdout(fs::File::create(directory.join("monitor.log")).map_err(|error| error.to_string())?)
        .stderr(fs::File::create(directory.join("stderr.log")).map_err(|error| error.to_string())?)
        .creation_flags(CREATE_NO_WINDOW).spawn().map_err(|error| error.to_string())?;
    let serial = directory.join("serial.log");
    let startup = (|| {
        wait_marker(&mut child, &serial, "SHELL_TASK2_READY")?;
        let input = child.stdin.as_mut().ok_or("Monitor QEMU non disponibile")?;
        for key in ["g", "u", "i", "ret"] {
            writeln!(input, "sendkey {key} 20").and_then(|()| input.flush()).map_err(|error| error.to_string())?;
            std::thread::sleep(Duration::from_millis(100));
        }
        wait_marker(&mut child, &serial, "USER: VoidOS desktop ready")
    })();
    if let Err(error) = startup {
        let _ = child.kill();
        let _ = child.wait();
        return Err(error);
    }
    println!("Desktop pronto. Chiudere la finestra QEMU per terminare.");
    child.wait().map(|status| status.success()).map_err(|error| error.to_string())
}

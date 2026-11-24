use crate::evidence::results::{Value, object};
use crate::vm::machine::Vm;
use std::time::{Duration, Instant};

// ====== wait_text ======

fn wait_text(vm: &mut Vm, marker: &str, count: usize) -> Result<(), String> {
    let deadline = Instant::now() + vm.limits.wait;
    while Instant::now() < deadline {
        let log = vm.log();
        if log.terminal() || !log.failures().is_empty() {
            return Err(format!("GUI campaign kernel failure: {:?}", log.failures()));
        }
        if log.text.matches(marker).count() >= count {
            return Ok(());
        }
        if !vm.alive() {
            return Err(format!("VM exited before {marker}"));
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    Err(format!("Missing GUI evidence {marker} ({count})"))
}

// ====== click ======

fn click(vm: &mut Vm, dx: i32, dy: i32) {
    vm.command(&format!("mouse_move {dx} {dy}"));
    std::thread::sleep(Duration::from_millis(150));
    vm.command("mouse_button 1");
    std::thread::sleep(Duration::from_millis(100));
    vm.command("mouse_button 0");
    std::thread::sleep(Duration::from_millis(150));
}

// ====== screenshot ======

fn screenshot(vm: &mut Vm, name: &str) -> Result<(), String> {
    let path = vm.directory.join(format!("{name}.ppm"));
    let path_text = path.to_string_lossy().replace('\\', "/");
    vm.command(&format!("screendump \"{path_text}\""));
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        if let Ok(bytes) = std::fs::read(&path) {
            if bytes.starts_with(b"P6\n1024 768\n255\n") && bytes.len() == 1024 * 768 * 3 + 16 {
                return Ok(());
            }
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    Err(format!("Incomplete screenshot {}", path.display()))
}

// ====== run ======

pub fn run(vm: &mut Vm, minutes: u32) -> Result<Value, String> {
    vm.wait("SHELL_TASK2_READY", 1, Some("INFO"))?;
    wait_text(vm, "VoidOS gui surface read-only ok", 1)?;
    let started = Instant::now();
    let duration = Duration::from_secs(u64::from(minutes) * 60);
    let mut cycles = 0;
    loop {
        let ready = vm.log().text.matches("VoidOS desktop ready").count();
        let terminal = vm.log().text.matches("VoidOS desktop terminal ready").count();
        let commands = vm.log().text.matches("VoidOS terminal command processed").count();
        let reads = vm.log().text.matches("VoidOS terminal VFS read ok").count();
        super::ordinary::shell_type(vm, "gui")?;
        wait_text(vm, "VoidOS desktop ready", ready + 1)?;
        std::thread::sleep(Duration::from_millis(500));
        if cycles == 0 {
            screenshot(vm, "desktop")?;
        }
        click(vm, -109, 326);
        wait_text(vm, "VoidOS desktop terminal ready", terminal + 1)?;
        for key in ["r", "p", "spc", "shift-7", "h", "e", "l", "l", "o", "dot", "t", "x", "t", "ret"] {
            vm.command(&format!("sendkey {key} 20"));
            std::thread::sleep(Duration::from_millis(100));
        }
        wait_text(vm, "VoidOS terminal command processed", commands + 1)?;
        wait_text(vm, "VoidOS terminal VFS read ok", reads + 1)?;
        std::thread::sleep(Duration::from_millis(500));
        if cycles == 0 {
            screenshot(vm, "terminal-command")?;
        }
        click(vm, 159, -585);
        std::thread::sleep(Duration::from_millis(400));
        click(vm, -159, 585);
        wait_text(vm, "VoidOS desktop terminal ready", terminal + 2)?;
        click(vm, 571, -676);
        vm.wait("SHELL_GUI_OK", cycles + 1, Some("OK"))?;
        cycles += 1;
        if started.elapsed() >= duration {
            break;
        }
    }
    super::ordinary::shell_type(vm, "bridge status")?;
    vm.wait("SHELL_BRIDGE_STATUS_OK", 1, Some("OK"))?;
    Ok(object(vec![
        ("GuiCycles", Value::Number(cycles as u64)),
        ("ElapsedSeconds", Value::Number(started.elapsed().as_secs())),
        ("ChildLaunches", Value::Number((cycles * 2) as u64)),
        ("Commands", Value::Number(cycles as u64)),
    ]))
}

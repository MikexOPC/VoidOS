#[path = "../requests/notify.rs"]
mod state;
static NOTIFY: state::Notify = state::Notify::new();

// ====== kernel_console_ap_notify ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_ap_notify(operation: u32, cpu: u32, token: u64,
    producer: u32, value: u64) -> u64 {
    match operation {
        0 => u64::from(NOTIFY.publish(cpu, token)),
        1 => NOTIFY.begin(cpu, token, producer),
        2 => u64::from(NOTIFY.finish(cpu, token, producer, value)),
        3 => NOTIFY.take(cpu, token),
        4 => u64::from(NOTIFY.acknowledge(cpu, token, value)),
        5 => u64::from(NOTIFY.seal(cpu, token)),
        6 if value <= 3 => u64::from(NOTIFY.close(cpu, token, value & 1 != 0, value & 2 != 0)),
        7 => NOTIFY.query(token, producer),
        _ => 0,
    }
}

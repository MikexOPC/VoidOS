#[path = "../requests/channel.rs"]
mod state;
static CHANNEL: state::Channel = state::Channel::new();

// ====== kernel_console_ap_channel ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_ap_channel(operation: u32, cpu: u32, token: u64, producer: u32) -> u64 {
    match operation {
        0 => u64::from(CHANNEL.publish(cpu, token)),
        1 => u64::from(CHANNEL.enter(cpu, token, producer)),
        2 => u64::from(CHANNEL.complete(cpu, token, producer)),
        3 => CHANNEL.mask(token),
        4 => u64::from(CHANNEL.close(cpu, token)),
        _ => 0,
    }
}

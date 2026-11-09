#![no_std]

macro_rules! program {
    ($symbol:ident, $length:ident, $file:literal) => {
        #[unsafe(no_mangle)]
        pub static $symbol: [u8; include_bytes!(concat!(env!("K6_PROGRAM_DIR"), "/", $file)).len()] =
            *include_bytes!(concat!(env!("K6_PROGRAM_DIR"), "/", $file));
        #[unsafe(no_mangle)]
        pub static $length: u64 = $symbol.len() as u64;
    };
}

program!(k6_program_hello, k6_program_hello_length, "hello.elf");
program!(k6_program_null, k6_program_null_length, "null.elf");
program!(k6_program_readonly, k6_program_readonly_length, "readonly.elf");
program!(k6_program_privileged, k6_program_privileged_length, "privileged.elf");
program!(k6_program_kernel_jump, k6_program_kernel_jump_length, "kernel_jump.elf");
program!(k6_program_bad_pointer, k6_program_bad_pointer_length, "bad_pointer.elf");
program!(k6_program_bss, k6_program_bss_length, "bss.elf");
program!(k6_program_irq, k6_program_irq_length, "irq.elf");

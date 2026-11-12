#include "internal.h"

// ====== copy_checked ======

static BOOT_U64 copy_checked(void *target, const void *source, BOOT_U64 pointer, BOOT_U64 length, BOOT_U64 write)
{
    process_rt.CopyPointer = pointer;
    process_rt.CopyLength = length;
    process_rt.CopyWrite = write;
    BOOT_U64 failed = x86_process_copy(target, source, length);
    process_rt.CopyLength = 0;
    if (__readeflags() & FLAG_AC) process_die("PROCESS_AC_RETAINED");
    return failed;
}

// ====== sys_write ======

static BOOT_U64 sys_write(BOOT_U64 pointer, BOOT_U64 length)
{
    BOOT_U32 range = x86_process_copy_range(pointer, length);
    if (range == 3) return RESULT_LENGTH;
    if (range != 1) return RESULT_RANGE;
    if (!length) return 0;
    if (!x86_process_write_allowed(process_rt.WrittenBytes, length)) return RESULT_QUOTA;
    if (copy_checked(process_rt.Buffer, (const void *)pointer, pointer, length, 0)) {
        for (BOOT_U32 index = 0; index <= MAX_COPY; ++index) ((volatile unsigned char *)process_rt.Buffer)[index] = 0;
        return RESULT_RANGE;
    }
    x86_process_console_filter(process_rt.Buffer, length);
    if (!kernel_text("USER: ")) process_die("PROCESS_CONSOLE_FAILED");
    for (BOOT_U64 offset = 0; offset < length; offset += TEXT_BLOCK) {
        BOOT_U64 end = offset + TEXT_BLOCK < length ? offset + TEXT_BLOCK : length;
        unsigned char saved = process_rt.Buffer[end];
        process_rt.Buffer[end] = 0;
        if (!kernel_text((const char *)process_rt.Buffer + offset)) process_die("PROCESS_CONSOLE_FAILED");
        process_rt.Buffer[end] = saved;
    }
    if (process_rt.Buffer[length - 1] != '\n') {
        if (!kernel_text("\n")) process_die("PROCESS_CONSOLE_FAILED");
    }
    process_rt.WrittenBytes += length;
    ++process_rt.Current->Writes;
    return length;
}

// ====== sys_send ======

static BOOT_U64 sys_send(BOOT_U64 handle, BOOT_U64 pointer, BOOT_U64 len)
{
    BOOT_U64 status = x86_ipc_send_check(process_rt.IpcChannel, process_rt.IpcEndpoint, handle, len);
    if (status) return status;
    unsigned char data[IPC_MESSAGE_BYTES] = {0};
    if (x86_process_copy_range(pointer, len) != 1 ||
        copy_checked(data, (const void *)pointer, pointer, len, 0)) return RESULT_RANGE;
    return x86_ipc_send(process_rt.IpcChannel, process_rt.IpcEndpoint, handle, data, len);
}

// ====== sys_recv ======

static BOOT_U64 sys_recv(BOOT_U64 handle, BOOT_U64 pointer, BOOT_U64 capacity)
{
    unsigned char data[IPC_MESSAGE_BYTES] = {0};
    BOOT_U64 len = x86_ipc_peek(process_rt.IpcChannel, process_rt.IpcEndpoint, handle, data, capacity);
    if (len > sizeof(data)) return len;
    if (!len || x86_process_copy_range(pointer, len) != 1 ||
        copy_checked((void *)pointer, data, pointer, len, 1)) return RESULT_RANGE;
    if (x86_ipc_consume(process_rt.IpcChannel, process_rt.IpcEndpoint, handle)) process_die("PROCESS_IPC_STATE");
    return len;
}

// ====== process_budget_expired ======

int process_budget_expired(BOOT_U64 rip)
{
    if (process_rt.BudgetLeft && !kernel_deadline_expired(process_rt.BudgetClock, &process_rt.BudgetDeadline)) return 0;
    process_rt.Current->Kind = X86_PROCESS_BUDGET;
    process_rt.Current->Rip = rip;
    return 1;
}

// ====== x86_process_syscall_dispatch ======

int x86_process_syscall_dispatch(BOOT_U64 *frame)
{
#ifdef KERNEL_TEST_K6_PROCESS
    if (k6_negative == 31) (void)*(const volatile unsigned char *)0x10ULL;
    if (k6_negative == 32) x86_process_probe_read(X86_LAYOUT_SLOT_ADDRESS(X86_LAYOUT_HEAP_SLOT));
#endif
    if (!x86_process_return_valid(IRET(frame, 0), IRET(frame, 1), IRET(frame, 2), IRET(frame, 3), IRET(frame, 4))) {
        process_rt.Current->Kind = X86_PROCESS_BAD_FRAME;
        process_rt.Current->Rip = IRET(frame, 0);
        return 2;
    }
    if (process_budget_expired(IRET(frame, 0))) return 2;
    BOOT_U64 number = GPR(frame, GPR_RAX);
    if (number == SYS_EXIT) {
        process_rt.Current->Kind = X86_PROCESS_EXIT;
        process_rt.Current->Code = GPR(frame, GPR_RDI);
        return 2;
    }
    if (process_rt.YieldFrame && (number == SYS_YIELD || (process_rt.IpcEndpoint && number == SYS_WAIT_READABLE))) {
        BOOT_U64 result = number == SYS_YIELD ? 0 :
            x86_ipc_receive_ready(process_rt.IpcChannel, process_rt.IpcEndpoint, GPR(frame, GPR_RDI));
        GPR(frame, GPR_RAX) = result;
        if (number == SYS_WAIT_READABLE && result != IPC_WOULD_BLOCK)
            return process_budget_expired(IRET(frame, 0)) ? 2 : 1;
        process_rt.Waiting = number == SYS_WAIT_READABLE;
        for (BOOT_U32 index = 0; index < USER_FRAME_WORDS; ++index) process_rt.YieldFrame[index] = frame[index];
        process_rt.Yielded = 1;
        return 2;
    }
    if (process_rt.IpcEndpoint && (number == SYS_SEND || number == SYS_RECV)) {
        BOOT_U64 handle = GPR(frame, GPR_RDI), pointer = GPR(frame, GPR_RSI), length = GPR(frame, GPR_RDX);
        GPR(frame, GPR_RAX) = number == SYS_SEND ? sys_send(handle, pointer, length) : sys_recv(handle, pointer, length);
        return process_budget_expired(IRET(frame, 0)) ? 2 : 1;
    }
    GPR(frame, GPR_RAX) = number == SYS_WRITE ? sys_write(GPR(frame, GPR_RDI), GPR(frame, GPR_RSI)) : RESULT_UNKNOWN;
#ifdef KERNEL_TEST_K6_PROCESS
    if (process_delay_next_syscall) {
        process_delay_next_syscall = 0;
        BOOT_U32 attempt = 0;
        while (!kernel_deadline_expired(process_rt.BudgetClock, &process_rt.BudgetDeadline)) {
            if (++attempt == 0x1000000U) process_die("PROCESS_CLOCK_FAILED");
            _mm_pause();
        }
        if (!kernel_log(0, "KERNEL_K6_SYSCALL_DEADLINE_OK")) process_die("PROCESS_CONSOLE_FAILED");
    }
#endif
    return process_budget_expired(IRET(frame, 0)) ? 2 : 1;
}

// ====== x86_process_copy_recover ======

int x86_process_copy_recover(BOOT_U64 *frame, BOOT_U64 cr2)
{
    enum { VECTOR, ERROR_CODE, RIP, CS, FLAGS };
    if (!x86_process_fault_recoverable(frame[RIP], frame[CS], frame[FLAGS], frame[ERROR_CODE], cr2,
        (BOOT_U64)x86_process_copy_first, (BOOT_U64)x86_process_copy_last,
        process_rt.CopyPointer, process_rt.CopyLength, process_rt.CopyWrite)) return 0;
    frame[RIP] = (BOOT_U64)x86_process_copy_fixup;
    frame[FLAGS] &= ~FLAG_AC;
    return 1;
}

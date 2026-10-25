#include "timer.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "lib/memory/address_ranges.h"
#include "kernel/diagnostics/log.h"

// ====== number ======

static BOOT_U64 number(const unsigned char *data, BOOT_U32 bytes)
{
    BOOT_U64 result = 0;
    BOOT_U32 byte;
    for (byte = 0; byte < bytes; ++byte) result |= (BOOT_U64)data[byte]<<(byte*8);
    return result;
}

// ====== output_valid ======

static int output_valid(const KERNEL_ACPI_DIRECTORY *directory, const X86_PM_CLOCK *output)
{
    BOOT_U64 address = (BOOT_U64)output;
    return output && !(address & 7) && address <= ~0ULL-sizeof(*output) &&
        !os_address_ranges_overlap(address,sizeof(*output),(BOOT_U64)directory,sizeof(*directory)) &&
        !os_address_ranges_overlap(address,sizeof(*output),X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(address,sizeof(*output),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(address,sizeof(*output),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES);
}

// ====== select_fadt ======

static int select_fadt(const KERNEL_ACPI_DIRECTORY *directory, KERNEL_ACPI_READ read,
    void *context, BOOT_U64 *physical, unsigned char *selected)
{
    unsigned char header[36];
    BOOT_U32 entry, count = 0, byte;
    for (entry = 0; entry < directory->Count; ++entry) {
        if (!read(context,directory->Entries[entry],36,header)) {
            return 0;
        }
        if (number(header,4) != 0x50434146) continue;
        if (++count != 1) {
            return 0;
        }
        *physical = directory->Entries[entry];
        for (byte = 0; byte < 36; ++byte) selected[byte] = header[byte];
    }
    return count == 1;
}

// ====== table ======

static int table(const unsigned char *selected, BOOT_U64 physical, BOOT_U32 bits,
    KERNEL_ACPI_READ read, void *context, unsigned char *prefix, BOOT_U32 *length)
{
    unsigned char chunk[128];
    BOOT_U32 byte, offset, bytes, sum = 0;
    *length = (BOOT_U32)number(selected+4,4);
    if (*length < 116 || *length > 4096 || selected[8] < 1 || selected[8] > 6 ||
        (selected[8] >= 3 && *length < 220) ||
        !kernel_acpi_physical_span(physical,*length,bits)) return 0;
    bytes = *length < 220 ? *length : 220;
    if (!read(context,physical,bytes,prefix)) {
        return 0;
    }
    for (byte = 0; byte < 36; ++byte) if (selected[byte] != prefix[byte]) return 0;
    for (byte = 0; byte < bytes; ++byte) sum += prefix[byte];
    for (offset = bytes; offset < *length; offset += bytes) {
        bytes = *length-offset;
        if (bytes > sizeof(chunk)) bytes = sizeof(chunk);
        if (!read(context,physical+offset,bytes,chunk)) {
            return 0;
        }
        for (byte = 0; byte < bytes; ++byte) sum += chunk[byte];
    }
    return !(sum & 255);
}

// ====== fields ======

static int fields(const unsigned char *prefix, BOOT_U32 length, X86_PM_CLOCK *draft)
{
    BOOT_U64 port = number(prefix+76,4);
    BOOT_U32 flags = (BOOT_U32)number(prefix+112,4);
    if (length >= 220 && number(prefix+212,8)) {
        if (prefix[208] != 1 || prefix[209] != 32 || prefix[210] ||
            (prefix[211] != 0 && prefix[211] != 3)) {
            return 0;
        }
        port = number(prefix+212,8);
    }
    if (!x86_pm_clock_port(port,flags,prefix[91])) {
        return 0;
    }
    draft->Port = (BOOT_U32)port;
    draft->Mask = flags & 256 ? ~0U : 0xffffff;
    draft->Ready = 1;
    return 1;
}

// ====== x86_pm_clock_capture ======

int x86_pm_clock_capture(const KERNEL_ACPI_DIRECTORY *directory, BOOT_U32 bits,
    KERNEL_ACPI_READ read, void *context, X86_PM_CLOCK *output)
{
    unsigned char selected[36], prefix[220];
    X86_PM_CLOCK draft = {0};
    BOOT_U32 byte;
    if (!directory || !read || !output_valid(directory,output) ||
        !kernel_acpi_directory_valid(directory,bits) ||
        !select_fadt(directory,read,context,&draft.Physical,selected) ||
        !table(selected,draft.Physical,bits,read,context,prefix,&draft.Length) ||
        os_address_ranges_overlap(draft.Physical,draft.Length,directory->Physical,directory->Length) ||
        !fields(prefix,draft.Length,&draft)) {
        return 0;
    }
    for (byte = 0; byte < sizeof(draft); ++byte)
        ((volatile unsigned char *)output)[byte] = ((const unsigned char *)&draft)[byte];
    return 1;
}

// ====== clock_valid ======

static int clock_valid(const X86_PM_CLOCK *clock)
{
    return clock->Ready == 1 && clock->Length >= 116 && clock->Length <= 4096 &&
        clock->Physical >= 4096 &&
        x86_pm_clock_port(clock->Port,0,4) && (clock->Mask == ~0U || clock->Mask == 0xffffff);
}

// ====== x86_timer_owner_valid ======

int x86_timer_owner_valid(const X86_LAPIC_TIMER *timer)
{
    BOOT_U64 address = (BOOT_U64)timer;
    if (!timer || (address & 7) || address > ~0ULL-sizeof(*timer) ||
        os_address_ranges_overlap(address,sizeof(*timer),X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(address,sizeof(*timer),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap(address,sizeof(*timer),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES)) {
        return 0;
    }
    return !timer->Reserved && x86_timer_event_fields(timer->Count,timer->Consumed,timer->Phase,timer->Active) &&
        timer->Mapping && clock_valid(&timer->Clock) &&
        !os_address_ranges_overlap(address,sizeof(*timer),(BOOT_U64)timer->Mapping,sizeof(*timer->Mapping)) &&
        x86_lapic_validate(timer->Mapping);
}

// ====== sample ======

static int sample(X86_LAPIC_TIMER *timer, X86_TIMER_SAMPLE *output)
{
    BOOT_U32 start, start_after, now, end_after, attempt;
    X86_TIMER_SAMPLE draft = {0};
    int result = 0, stopped;
    if (!x86_timer_write(timer->Mapping,0x380,~0U)) {
        return 0;
    }
    /* Intel SDM Vol.3A section 12.5.4, Dec.2024: initial count at 0x380,
     * current count at 0x390, zero initial count stops the one-shot timer.
     * ACPI 6.5 section 4.8.3.3: PM_TMR is read-only, dword, 3.579545MHz.
     * Bracket MMIO reads so scheduling latency is measured and bounded. */
    start = x86_pm_read((unsigned short)timer->Clock.Port) & timer->Clock.Mask;
    if (!x86_timer_read(timer->Mapping,0x390,&draft.Start) || !draft.Start) goto cleanup;
    start_after = x86_pm_read((unsigned short)timer->Clock.Port) & timer->Clock.Mask;
    for (attempt = 0; attempt < 10000000; ++attempt) {
        now = x86_pm_read((unsigned short)timer->Clock.Port) & timer->Clock.Mask;
        draft.PmTicks = x86_pm_elapsed(start_after,now,timer->Clock.Mask);
        if (draft.PmTicks >= X86_PM_SAMPLE_TICKS) break;
    }
    if (attempt == 10000000 || !x86_timer_read(timer->Mapping,0x390,&draft.End)) goto cleanup;
    end_after = x86_pm_read((unsigned short)timer->Clock.Port) & timer->Clock.Mask;
    draft.PmTicks = x86_timer_sample_elapsed(start,start_after,now,end_after,timer->Clock.Mask);
    draft.Reload = x86_timer_reload(draft.Start,draft.End,draft.PmTicks);
    result = draft.Reload ? 1 : 2;
cleanup:
    stopped = x86_timer_write(timer->Mapping,0x380,0) &&
        x86_timer_read(timer->Mapping,0x380,&now) && !now &&
        x86_timer_read(timer->Mapping,0x390,&now) && !now;
    if (!result || !stopped) {
        return 0;
    }
    if (result == 2) { return 2; }
    output->Start = draft.Start; output->End = draft.End;
    output->PmTicks = draft.PmTicks; output->Reload = draft.Reload;
    return 1;
}

// ====== x86_timer_measure ======

int x86_timer_measure(X86_LAPIC_TIMER *timer)
{
    BOOT_U32 value, reload, attempt;
    int first, second;
    X86_TIMER_SAMPLE samples[2];
    if (!x86_timer_owner_valid(timer) || !x86_exception_environment() || timer->Active || timer->Reload ||
        timer->Count || timer->Error || timer->Token.Id || timer->Token.Vector || timer->Token.Reserved ||
        !x86_lapic_idle(timer->Mapping) ||
        !x86_timer_read(timer->Mapping,0x320,&value) || value != 0x10041 ||
        !x86_timer_read(timer->Mapping,0x3e0,&value) || value != 3 ||
        !x86_timer_read(timer->Mapping,0x380,&value) || value ||
        !x86_timer_read(timer->Mapping,0x390,&value) || value) {
        return 0;
    }
    /* Three bounded pairs, no retry on I/O failure or incomplete stop.
     * Samples remain private until both quality and consensus succeed. */
    for (attempt = 1; attempt <= 3; ++attempt) {
        first = sample(timer,&samples[0]);
        if (!first) { return 0; }
        second = first == 1 ? sample(timer,&samples[1]) : 2;
        if (!second) { return 0; }
        reload = first == 1 && second == 1 ?
            x86_timer_sample_consensus(samples[0].Reload,samples[1].Reload) : 0;
        if (reload) {
            if (!kernel_text("KERNEL_TIMER_CALIBRATION_ATTEMPT: ") ||
                !kernel_number(attempt) || !kernel_text("\n")) { return 0; }
            timer->Samples[0] = samples[0]; timer->Samples[1] = samples[1];
            timer->Reload = reload;
            return 1;
        }
        if (attempt < 3 && !kernel_log(2,"TIMER_CALIBRATION_RETRY")) { return 0; }
    }
    return 0;
}

#include "arch/x86/kernel/timer/lapic/timer.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
static unsigned char fadt[300];
static KERNEL_ACPI_DIRECTORY directory;
static X86_LAPIC_MAPPING mapping;
static X86_LAPIC_TIMER timer;
static BOOT_U32 pm, lvt, initial, current, divide, stalled, eoi = 1, fail_write;
static BOOT_U32 measurement_mode, measurement_starts, measurement_reads;
extern BOOT_U32 test_timer_token_live;

// ====== ambiente senza istruzioni privilegiate e byte firmware in memoria host ===== //

BOOT_U64 x86_exception_environment(void) { return 1; }
int kernel_acpi_directory_valid(const KERNEL_ACPI_DIRECTORY *input, BOOT_U32 bits)
{
    return input && input->Count <= 256 && bits == 48;
}
BOOT_U32 kernel_acpi_physical_span(BOOT_U64 physical, BOOT_U32 bytes, BOOT_U32 bits)
{
    return bits == 48 && physical >= 4096 && bytes && physical <= (1ULL<<48)-bytes;
}
static int read_table(void *context, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output)
{
    BOOT_U32 index;
    (void)context;
    if (physical < 0x2000 || physical-0x2000 > sizeof(fadt) || bytes > sizeof(fadt)-(physical-0x2000)) return 0;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)output)[index] = fadt[physical-0x2000+index];
    return 1;
}

// ====== registri simulati con decremento esplicito e reference clock indipendente ===== //

int x86_timer_read(const X86_LAPIC_MAPPING *owner, BOOT_U32 offset, BOOT_U32 *output)
{
    if (owner != &mapping || !output) return 0;
    if (offset == 0x320) *output = lvt;
    else if (offset == 0x380) *output = initial;
    else if (offset == 0x390) {
        BOOT_U32 decrement = measurement_mode == 3 && measurement_starts == 1 ? 100000 : 1000;
        if (!stalled && current > decrement) { current -= decrement; }
        *output = current;
    }
    else if (offset == 0x3e0) *output = divide;
    else if (offset == 0x30) *output = 0x50014;
    else return 0;
    return 1;
}
int x86_timer_write(const X86_LAPIC_MAPPING *owner, BOOT_U32 offset, BOOT_U32 value)
{
    if (owner != &mapping) return 0;
    if (fail_write) {
        if (fail_write == 2) fail_write = 0;
        return 0;
    }
    if (offset == 0x320) lvt = value;
    else if (offset == 0x380) {
        if (value == ~0U) { ++measurement_starts; measurement_reads = 0; }
        if (measurement_mode == 4 && !value && measurement_starts) { return 0; }
        current = initial = value;
    }
    else if (offset == 0x3e0) divide = value;
    else return 0;
    return 1;
}
BOOT_U32 x86_pm_read(unsigned short port)
{
    if (port != 0x408) return 0;
    if (!stalled) {
        ++measurement_reads;
        pm += ((measurement_mode == 1 && measurement_starts == 1) || measurement_mode == 2) &&
            measurement_reads == 2 ? 10000u : 1000u;
    }
    return pm;
}
int x86_lapic_validate(const X86_LAPIC_MAPPING *owner) { return owner == &mapping; }
int x86_lapic_idle(const X86_LAPIC_MAPPING *owner) { return owner == &mapping && eoi; }
int x86_lapic_eoi_vector(const X86_LAPIC_MAPPING *owner, BOOT_U32 vector)
{
    return owner == &mapping && vector == 0x41 && eoi;
}

// ====== fixture FADT con checksum corretto e profilo PM a 24 bit ===== //

static void checksum(void)
{
    BOOT_U32 index, sum = 0, length = fadt[4] | ((BOOT_U32)fadt[5]<<8);
    fadt[9] = 0;
    for (index = 0; index < length; ++index) sum += fadt[index];
    fadt[9] = (unsigned char)(0U-sum);
}
static void reset(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(fadt); ++byte) ((volatile unsigned char *)fadt)[byte] = 0;
    for (byte = 0; byte < sizeof(directory); ++byte) ((volatile unsigned char *)&directory)[byte] = 0;
    for (byte = 0; byte < sizeof(timer); ++byte) ((volatile unsigned char *)&timer)[byte] = 0;
    directory.Physical = 0x1000; directory.Length = 44; directory.Count = 1; directory.Entries[0] = 0x2000;
    fadt[0] = 'F'; fadt[1] = 'A'; fadt[2] = 'C'; fadt[3] = 'P'; fadt[4] = 244; fadt[8] = 3;
    fadt[76] = 8; fadt[77] = 4; fadt[91] = 4;
    timer.Mapping = &mapping;
    timer.Clock.Port = 0x408; timer.Clock.Mask = 0xffffff; timer.Clock.Ready = 1;
    timer.Clock.Length = 244; timer.Clock.Physical = 0x2000;
    pm = 0xffff00; initial = current = divide = stalled = fail_write = 0; eoi = 1; lvt = 0x10040;
    measurement_mode = measurement_starts = measurement_reads = 0;
    test_timer_token_live = 1;
    checksum();
}

// ====== sorgente legacy estesa e intero checksum prima di pubblicare ===== //

static int capture(void)
{
    X86_PM_CLOCK output = {0};
    BOOT_U32 byte;
    reset();
    if (!x86_pm_clock_capture(&directory,48,read_table,0,&output) || output.Port != 0x408 || output.Mask != 0xffffff) return 1;
    fadt[208] = 1; fadt[209] = 32; fadt[211] = 3; fadt[212] = 0x10; fadt[213] = 4;
    fadt[113] = 1; checksum();
    if (!x86_pm_clock_capture(&directory,48,read_table,0,&output) || output.Port != 0x410 || output.Mask != ~0U) return 2;
    fadt[240] ^= 1;
    output.Ready = 77;
    if (x86_pm_clock_capture(&directory,48,read_table,0,&output) || output.Ready != 77) return 3;
    reset();
    for (byte = 0; byte < 244; ++byte) {
        fadt[byte] ^= 1;
        if (x86_pm_clock_capture(&directory,48,read_table,0,&output)) return 4;
        fadt[byte] ^= 1;
    }
    return 0;
}

// ====== profili incompleti ambigu o non supportati conservano l'output ===== //

static int rejected(void)
{
    X86_PM_CLOCK output = {0};
    const BOOT_U32 positions[6] = {91,208,209,210,211,114};
    const unsigned char values[6] = {3,0,16,1,2,16};
    BOOT_U32 index;
    for (index = 0; index < 6; ++index) {
        reset();
        if (index > 0 && index < 5) { fadt[208] = 1; fadt[209] = 32; fadt[211] = 3; fadt[212] = 8; fadt[213] = 4; }
        fadt[positions[index]] = values[index]; checksum();
        output.Ready = 77;
        if (x86_pm_clock_capture(&directory,48,read_table,0,&output) || output.Ready != 77) return 1;
    }
    reset(); directory.Count = 2; directory.Entries[1] = 0x2000;
    if (x86_pm_clock_capture(&directory,48,read_table,0,&output)) return 2;
    reset(); fadt[4] = 115; checksum();
    if (x86_pm_clock_capture(&directory,48,read_table,0,&output)) return 3;
    reset();
    if (x86_pm_clock_capture(&directory,48,read_table,0,(X86_PM_CLOCK *)&directory) ||
        x86_pm_clock_capture(&directory,48,read_table,0,(X86_PM_CLOCK *)X86_LAPIC_VIRTUAL) ||
        x86_pm_clock_capture(&directory,48,read_table,0,(X86_PM_CLOCK *)((unsigned char *)&output+1))) {
        return 4;
    }
    return 0;
}

// ====== campioni indipendenti arrestati anche su clock fermo o scrittura fallita ===== //

static int measurement(void)
{
    reset(); lvt = 0x10041; divide = 3;
    if (!x86_timer_measure(&timer) || !timer.Reload || initial || current) return 1;
    if (!timer.Samples[0].Reload || !timer.Samples[1].Reload) return 2;
    reset(); lvt = 0x10041; divide = 3; stalled = 1;
    if (x86_timer_measure(&timer) || timer.Reload || initial || current) return 3;
    reset(); lvt = 0x10041; divide = 3; fail_write = 1;
    if (x86_timer_measure(&timer) || timer.Reload) return 4;
    reset(); lvt = 0x10041; divide = 3; measurement_mode = 1;
    if (!x86_timer_measure(&timer) || measurement_starts != 3 || !timer.Reload || initial || current) {
        return 5;
    }
    reset(); lvt = 0x10041; divide = 3; measurement_mode = 2;
    timer.Samples[0].Reload = 77; timer.Samples[1].Reload = 88;
    if (x86_timer_measure(&timer) || measurement_starts != 3 || timer.Reload || initial || current ||
        timer.Samples[0].Reload != 77 || timer.Samples[1].Reload != 88) { return 6; }
    reset(); lvt = 0x10041; divide = 3; measurement_mode = 3;
    if (!x86_timer_measure(&timer) || measurement_starts != 4 || initial || current) { return 7; }
    reset(); lvt = 0x10041; divide = 3; measurement_mode = 4;
    if (x86_timer_measure(&timer) || measurement_starts != 1 || timer.Reload || !initial) { return 8; }
    return 0;
}

// ====== un solo EOI per scadenza reale prima di incrementare Count ===== //

static int service(void)
{
    reset(); timer.Active = 1; timer.Phase = X86_TIMER_ARMED; timer.Reload = initial = 100; divide = 3; lvt = 0x41;
    if (!x86_timer_service(&timer) || timer.Count != 1 || timer.Error || lvt != 0x10041) return 1;
    timer.Phase = X86_TIMER_ARMED; timer.Consumed = 1; lvt = 0x41; initial = 100; eoi = 0;
    if (x86_timer_service(&timer) || timer.Count != 1 || !timer.Error) return 2;
    reset(); timer.Active = 1; timer.Phase = X86_TIMER_ARMED; timer.Reload = initial = 100; lvt = 0x41; divide = 3; current = 1;
    if (x86_timer_service(&timer) || timer.Count) return 3;
    reset(); timer.Active = 1; timer.Phase = X86_TIMER_ARMED; timer.Reload = initial = 100; lvt = 0x20041; divide = 3;
    if (x86_timer_service(&timer) || timer.Count) return 4;
    return 0;
}

// ====== mille cicli senza perdere eventi sovrascrivere output o riarmare pending ===== //

static int event_cycles(void)
{
    BOOT_U32 index, event = 77;
    reset(); timer.Reload = 100; lvt = 0x10041; divide = 3;
    if (!x86_timer_events_initialize(&timer) || x86_timer_events_initialize(&timer)) return 1;
    if (x86_timer_arm(&timer) || x86_timer_take(&timer,&event) || event != 77) return 2;
    timer.Token.Id = 2; timer.Token.Vector = 0x41;
    test_timer_token_live = 0;
    if (x86_timer_arm(&timer) || timer.Active || initial || current) return 12;
    test_timer_token_live = 1;
    if (x86_timer_events_close(&timer)) return 3;
    for (index = 1; index <= 1000; ++index) {
        if (!x86_timer_arm(&timer) || x86_timer_arm(&timer) || x86_timer_take(&timer,&event)) return 4;
        current = 0;
        if (!x86_timer_service(&timer) || x86_timer_arm(&timer) ||
            x86_timer_events_close(&timer) || timer.Consumed != index-1) return 5;
        if (x86_timer_take(&timer,(BOOT_U32 *)&timer) ||
            x86_timer_take(&timer,(BOOT_U32 *)X86_LAPIC_VIRTUAL) ||
            x86_timer_take(&timer,(BOOT_U32 *)((unsigned char *)&event+1))) return 6;
        if (!x86_timer_take(&timer,&event) || event != index ||
            x86_timer_take(&timer,&event) || timer.Consumed != index || timer.Active) return 7;
    }
    timer.Token.Id = 0; timer.Token.Vector = 0;
    if (!x86_timer_events_close(&timer) || x86_timer_events_close(&timer) ||
        x86_timer_arm(&timer) || x86_timer_take(&timer,&event)) return 8;
    reset(); timer.Reload = 100; lvt = 0x10041; divide = 3;
    if (!x86_timer_events_initialize(&timer)) return 9;
    timer.Token.Id = 2; timer.Token.Vector = 0x41; fail_write = 1;
    if (x86_timer_arm(&timer) || timer.Error != 3 || x86_timer_take(&timer,&event)) return 10;
    if (timer.Phase != X86_TIMER_ARMED || !timer.ArmedPm) return 15;
    reset(); timer.Reload = 100; lvt = 0x10041; divide = 3;
    if (!x86_timer_events_initialize(&timer)) return 16;
    timer.Token.Id = 2; timer.Token.Vector = 0x41; fail_write = 2;
    if (x86_timer_arm(&timer) || timer.Error != 3 || timer.ArmedPm || timer.Active ||
        timer.Phase != X86_TIMER_READY || initial || current) return 17;
    reset(); timer.Phase = X86_TIMER_READY; timer.Count = timer.Consumed = ~0U;
    lvt = 0x10041; divide = 3;
    timer.Reload = 100; timer.Token.Id = 2; timer.Token.Vector = 0x41;
    if (x86_timer_arm(&timer) || timer.Count != ~0U) return 11;
    timer.Count = timer.Consumed = ~0U-1;
    if (!x86_timer_arm(&timer)) return 13;
    current = 0;
    if (!x86_timer_service(&timer) || timer.Count != ~0U ||
        !x86_timer_take(&timer,&event) || event != ~0U || x86_timer_arm(&timer)) return 14;
    return 0;
}

// ====== owner alias e calibrazione su contatore vivo vengono rifiutati ===== //

static int timing(void) {
    const BOOT_U32 masks[2] = {0xffffff, ~0U};
    const BOOT_U32 delays[2] = {X86_PM_SAMPLE_TICKS, X86_PM_SAMPLE_TICKS * 20 + 1};
    for (BOOT_U32 width = 0; width < 2; ++width) {
        for (BOOT_U32 late = 0; late < 2; ++late) {
            BOOT_U32 event, start, irq_elapsed, wait_elapsed;
            reset(); timer.Clock.Mask = masks[width];
            timer.Reload = 100; lvt = 0x10041; divide = 3;
            if (!x86_timer_events_initialize(&timer)) return 1;
            timer.Token.Id = 2; timer.Token.Vector = 0x41;
            pm = masks[width] - 2000;
            if (!x86_timer_arm(&timer)) return 2;
            start = timer.ArmedPm;
            pm = start + delays[late] - 1000;
            current = 0;
            if (!x86_timer_service(&timer) || timer.ArmedPm || timer.Count != 1) return 3;
            irq_elapsed = timer.DeadlinePm[0];
            if (irq_elapsed != delays[late] || x86_timer_deadline_valid(irq_elapsed) != (late ? 0U : 1U)) return 4;
            pm += X86_PM_CLOCK_HZ;
            wait_elapsed = x86_pm_elapsed(start,x86_pm_read(0x408) & masks[width],masks[width]);
            if (x86_timer_deadline_valid(wait_elapsed) || timer.DeadlinePm[0] != irq_elapsed) return 5;
            if (x86_timer_service(&timer) || timer.DeadlinePm[0] != irq_elapsed) return 6;
            if (!x86_timer_take(&timer,&event) || event != 1 || timer.ArmedPm) return 7;
            pm = masks[width] - 999;
            if (!x86_timer_arm(&timer) || timer.ArmedPm) return 8;
            pm = delays[late] - 1000;
            current = 0;
            if (!x86_timer_service(&timer) || timer.DeadlinePm[1] != delays[late]) return 9;
            if (!x86_timer_take(&timer,&event) || event != 2) return 10;
            timer.Token.Id = timer.Token.Vector = 0;
            if (!x86_timer_events_close(&timer)) return 11;
        }
    }
    return 0;
}

// ====== lifetimes ======

static int lifetimes(void)
{
    reset(); lvt = 0x10041; divide = 3; initial = current = 100000;
    if (x86_timer_measure(&timer) || timer.Reload || initial != 100000) return 1;
    reset(); timer.Reserved = 1;
    if (x86_timer_owner_valid(&timer)) return 2;
    reset(); timer.Active = 2;
    if (x86_timer_owner_valid(&timer)) return 3;
    reset(); timer.Mapping = (const X86_LAPIC_MAPPING *)&timer;
    if (x86_timer_owner_valid(&timer)) return 4;
    reset(); timer.Count = 4;
    if (x86_timer_owner_valid(&timer)) return 5;
    reset(); timer.Clock.Port = 0x3f8;
    if (x86_timer_owner_valid(&timer)) return 6;
    reset(); timer.Clock.Mask = 0xffff;
    if (x86_timer_owner_valid(&timer)) return 7;
    reset(); timer.ArmedPm = 1;
    if (x86_timer_owner_valid(&timer)) return 8;
    reset(); timer.Active = 1; timer.Phase = X86_TIMER_ARMED; timer.ArmedPm = 0x1000000;
    if (x86_timer_owner_valid(&timer)) return 9;
    return 0;
}

// ====== gruppi separati per discovery calibrazione e IRQ ===== //

__declspec(dllexport) int run_lapic_timer_tests(void)
{
    int result = capture(); if (result) return 100+result;
    result = rejected(); if (result) return 200+result;
    result = measurement(); if (result) return 300+result;
    result = service(); if (result) return 400+result;
    result = lifetimes();
    if (result) return 500+result;
    result = event_cycles();
    if (result) return 600+result;
    result = timing();
    if (result) return 800+result;
    if (x86_timer_loop_request_stop() || x86_timer_loop_closed(0) ||
        x86_timer_loop_run(0,0,0)) return 701;
    reset();
    if (x86_timer_loop_closed(&timer) || x86_timer_loop_run(&timer,0,0) ||
        x86_timer_loop_request_stop() || timer.Count || timer.Consumed || timer.Phase) return 702;
    return 0;
}

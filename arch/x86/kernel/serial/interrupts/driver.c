#include "route.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "kernel/diagnostics/log.h"
typedef struct {
    const X86_UART_ROUTE *Route;
    const X86_IO_APIC_CAPACITY *Io;
    BOOT_U32 Masked;
} ROUTE_REQUEST;

// ====== owner output distinto da tutte le tabelle lette ===== //

static int output_valid(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *caps,
    X86_UART_ROUTE *output)
{
    BOOT_U64 target = (BOOT_U64)output;
    return topology && caps && output && !(target & 3) && target <= ~0ULL-sizeof(*output) &&
        !os_address_ranges_overlap(target,sizeof(*output),(BOOT_U64)topology,sizeof(*topology)) &&
        !os_address_ranges_overlap(target,sizeof(*output),(BOOT_U64)caps,sizeof(*caps));
}

// ====== COM1 IRQ quattro con override ISA e NMI globali disgiunti ===== //

int x86_uart_route_plan(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *caps,
    X86_UART_ROUTE *output)
{
    X86_UART_ROUTE draft = {0,0,4,0x40,0,0};
    BOOT_U32 index, found = 0;
    if (!output_valid(topology,caps,output) || !kernel_madt_validate(topology,52) ||
        caps->Ready != 1 || caps->LapicId > 255 || caps->IoCount != topology->IoCount) return 0;
    for (index = 0; index < topology->IsoCount; ++index) {
        if (topology->Iso[index].Source == 4) {
            draft.Gsi = topology->Iso[index].Gsi;
            draft.Bits = x86_uart_route_bits(topology->Iso[index].Flags);
            if (!draft.Bits) return 0;
        }
    }
    for (index = 0; index < topology->NmiCount; ++index)
        if (topology->Nmi[index].Kind == 3 && topology->Nmi[index].Value == draft.Gsi) return 0;
    // Nessuna linea condivisa in questo primo profilo, anche con IRQ ISA impliciti.
    for (index = 0; index < 16; ++index) {
        BOOT_U32 other, gsi = index;
        if (index == 4) continue;
        for (other = 0; other < topology->IsoCount; ++other)
            if (topology->Iso[other].Source == index) gsi = topology->Iso[other].Gsi;
        if (gsi == draft.Gsi) return 0;
    }
    for (index = 0; index < caps->IoCount; ++index) {
        const X86_IO_APIC_CAPACITY *io = &caps->Io[index];
        if (io->Address != topology->Io[index].Address || io->Base != topology->Io[index].GsiBase ||
            io->Id != topology->Io[index].Id || io->Inputs != x86_apic_io_inputs(io->Version) ||
            !io->Inputs || io->End != x86_apic_gsi_end(io->Base,io->Inputs) || !io->End) return 0;
        if (draft.Gsi >= io->Base && draft.Gsi < io->End) {
            draft.Io = index; draft.Pin = draft.Gsi-io->Base; ++found;
        }
    }
    if (found != 1) return 0;
    draft.Destination = caps->LapicId;
    *output = draft;
    return 1;
}

// ====== selettore riletto prima di ogni accesso IOWIN ===== //

static int read_register(BOOT_U32 reg, BOOT_U32 *value)
{
    BOOT_U32 actual;
    return x86_apic_control_write(0,reg) && x86_apic_control_read(0,&actual) &&
        (actual & 255) == reg && x86_apic_control_read(0x10,value);
}
// ====== scrittura IOWIN con selettore verificato ===== //

static int write_register(BOOT_U32 reg, BOOT_U32 value)
{
    BOOT_U32 actual;
    return x86_apic_control_write(0,reg) && x86_apic_control_read(0,&actual) &&
        (actual & 255) == reg && x86_apic_control_write(0x10,value);
}

// ====== maschera prima del cambio rotta e readback dopo apertura ===== //

static int configure(void *state)
{
    ROUTE_REQUEST *request = state;
    BOOT_U32 saved, restored, id, version, low = 0, high;
    BOOT_U32 reg = 0x10 + request->Route->Pin * 2;
    BOOT_U32 expected = request->Route->Bits | (request->Masked ? 0x10000 : 0);
    int result, restore;
    if (!x86_apic_control_read(0,&saved)) return 0;
    saved &= 255;
    result = read_register(0,&id) && ((id >> 24) & 15) == request->Io->Id &&
        read_register(1,&version) && version == request->Io->Version && read_register(reg,&low);
    if (result) {
        // Una sola sorgente IRQ assegnata; disabilitare UART prima di richiudere.
        result = !(low & 0x5000) && write_register(reg,low | 0x10000) &&
            read_register(reg,&low) && (low & 0x10000) &&
            write_register(reg+1,request->Route->Destination << 24) && write_register(reg,expected) &&
            read_register(reg,&low) && (low & ~0x5000U) == expected &&
            read_register(reg+1,&high) && high == request->Route->Destination << 24;
    }
    restore = x86_apic_control_write(0,saved) && x86_apic_control_read(0,&restored) &&
        (restored & 255) == saved;
    return result && restore;
}

// ====== rotta bounded prima del prestito UC in sola CPU BSP ===== //

int x86_uart_route_apply(X86_PAGE_WINDOW *window, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps, const X86_UART_ROUTE *route, BOOT_U32 masked)
{
    ROUTE_REQUEST request;
    if (!x86_exception_environment() || !window || !profile || !caps || !route || masked > 1 || caps->Ready != 1 ||
        route->Io >= caps->IoCount || caps->IoCount > 16 || route->Reserved ||
        route->Destination != profile->InitialId || route->Destination > 255 ||
        route->Pin >= caps->Io[route->Io].Inputs || route->Pin >= 120 ||
        (BOOT_U64)route->Gsi != (BOOT_U64)caps->Io[route->Io].Base + route->Pin ||
        (route->Bits & ~0xa000U) != 0x40 || caps->Io[route->Io].Id > 15) return 0;
    request.Route = route; request.Io = &caps->Io[route->Io]; request.Masked = masked;
    return x86_apic_borrow(window,request.Io->Address,profile->CacheBits,1,configure,&request);
}

// ====== ISR contiene soltanto il vettore reale assegnato alla seriale ===== //

static int service_bitmap(BOOT_U32 expected)
{
    BOOT_U32 index, value;
    for (index = 0; index < 8; ++index) {
        if (!x86_apic_control_read(0x100+index*16,&value) ||
            value != (index == 2 ? expected : 0)) return 0;
    }
    return 1;
}

// ====== ACK UART precede EOI zero e ISR vuoto dopo la scrittura ===== //

static int complete(void *state)
{
    (void)state;
    return service_bitmap(1) && x86_uart_lapic_eoi_zero() && service_bitmap(0);
}
// ====== prestito LAPIC revocato anche se il completamento fallisce ===== //

int x86_uart_eoi(X86_PAGE_WINDOW *window, const X86_APIC_PROFILE *profile)
{
    return x86_exception_environment() && profile &&
        x86_apic_borrow(window,profile->Lapic,profile->CacheBits,1,complete,(void *)profile);
}

#define COM1 0x3f8U
static X86_UART_IRQ_OWNER owner;
static X86_UART_ROUTE route;
static BOOT_U32 finished;

// ====== IER zero con DLAB gia azzerato nel profilo UART diagnostico ===== //

static int uart_disable(void)
{
    if (x86_read_port(COM1+3) != 3) return 0;
    x86_write_port(COM1+1,0);
    return x86_read_port(COM1+1) == 0;
}

// ====== attesa bounded del trasmettitore prima di abilitare THRE ===== //

static int uart_idle(void)
{
    BOOT_U32 attempt;
    for (attempt = 0; attempt < 100000; ++attempt) {
        unsigned char status = x86_read_port(COM1+5);
        if (status == 255 || (status & 0x1e)) return 0;
        if ((status & 0x60) == 0x60) return 1;
    }
    return 0;
}

// ====== buffer owner persistente distinto dai metadati paging ===== //

static int owner_valid(const X86_UART_IRQ_OWNER *state)
{
    BOOT_U64 target = (BOOT_U64)state;
    return state && !(target & 7) && target <= ~0ULL-sizeof(*state) &&
        state->Window && state->Window->Pool && state->Profile && !state->Reserved &&
        !os_address_ranges_overlap(target,sizeof(*state),(BOOT_U64)state->Window,sizeof(*state->Window)) &&
        !os_address_ranges_overlap(target,sizeof(*state),(BOOT_U64)state->Window->Pool,sizeof(*state->Window->Pool)) &&
        !os_address_ranges_overlap(target,sizeof(*state),(BOOT_U64)state->Profile,sizeof(*state->Profile)) &&
        !os_address_ranges_overlap(target,sizeof(*state),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES);
}

// ====== ACK UART senza stampa e poi EOI soltanto per ISR reale ===== //

int x86_uart_irq_service(X86_UART_IRQ_OWNER *state)
{
    unsigned char iir;
    int acknowledged, completed;
    if (!x86_exception_environment() || !owner_valid(state) ||
        state->Active != 1 || state->Error || state->Count >= 3) return 0;
    iir = x86_read_port(COM1+2);
    acknowledged = x86_uart_thre(iir) && uart_disable() &&
        (x86_read_port(COM1+2) & 1);
    // Disabilitare IER anche se un dispositivo presenta una causa inattesa.
    if (!acknowledged) { uart_disable(); state->Error = 1; }
    completed = x86_uart_eoi(state->Window,state->Profile);
    if (!completed) state->Error = 2;
    if (!acknowledged || !completed) return 0;
    ++state->Count;
    return 1;
}

// ====== unico owner statico durante la finestra IF uno ===== //

int x86_uart_irq_handle(void)
{
    return x86_uart_irq_service(&owner);
}

// ====== pending LAPIC vuoti dopo il completamento e prima del prossimo arm ===== //

static int pending_empty(void *state)
{
    BOOT_U32 index, value;
    (void)state;
    for (index = 0; index < 8; ++index) {
        if (!x86_apic_control_read(0x100+index*16,&value) || value ||
            !x86_apic_control_read(0x200+index*16,&value) || value) return 0;
    }
    return 1;
}

// ====== ripristino OUT2 originale con readback e UART interrupt chiusi ===== //

static int uart_restore(unsigned char mcr)
{
    int disabled = uart_disable();
    x86_write_port(COM1+4,mcr);
    return disabled && x86_read_port(COM1+4) == mcr;
}

// ====== fotografia dopo ACK EOI e revoca dei mapping ===== //

static int report(void)
{
    return kernel_section("primo driver COM1 a interrupt") &&
        kernel_text("KERNEL_UART_IRQ_COUNT: ") && kernel_number(owner.Count) &&
        kernel_text("\nKERNEL_UART_IRQ_ERROR: ") && kernel_number(owner.Error) &&
        kernel_text("\nKERNEL_UART_IRQ_GSI: ") && kernel_number(route.Gsi) &&
        kernel_text("\nKERNEL_UART_IRQ_VECTOR: ") && kernel_number(0x40) &&
        kernel_text("\nKERNEL_UART_IRQ_OWNER: ") && kernel_number((BOOT_U64)&owner) &&
        kernel_text("\n") && kernel_log(0,"KERNEL_UART_IRQ_OK");
}

// ====== tre consegne reali con attesa limitata e IF zero fra gli arm ===== //

int x86_uart_irq_run(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile, const X86_APIC_CAPABILITIES *caps)
{
    BOOT_U32 goal;
    unsigned char saved;
    int result = 0, closed, restored;
    if (finished || owner.Active || !x86_irq_ready() ||
        !x86_apic_mapping_ready(context,window,inventory,topology,profile) ||
        !x86_uart_route_plan(topology,caps,&route) || !uart_disable() || !uart_idle()) return 0;
    saved = x86_read_port(COM1+4);
    if (saved & ~3U) return 0;
    owner.Window = window; owner.Profile = profile;
    owner.Count = owner.Error = 0; owner.Active = 1;
    x86_write_port(COM1+4,saved | 8);
    if (x86_read_port(COM1+4) != (saved | 8) ||
        !x86_uart_route_apply(window,profile,caps,&route,0)) goto cleanup;
    for (goal = 1; goal <= 3; ++goal) {
        if (!x86_exception_environment() || !uart_idle() ||
            !x86_apic_borrow(window,profile->Lapic,profile->CacheBits,0,pending_empty,&owner)) goto cleanup;
        x86_write_port(COM1+1,2);
        // Il riarmo a THRE gia alto e verificato sul modello QEMU 16550A.
        // UART fisiche possono richiedere una vera transizione TX: timeout = rifiuto.
        if (x86_read_port(COM1+1) != 2 || !x86_uart_irq_wait(&owner.Count,goal,10000000) ||
            owner.Count != goal || owner.Error || !x86_exception_environment()) goto cleanup;
    }
    result = 1;
cleanup:
    restored = uart_restore(saved);
    closed = x86_uart_route_apply(window,profile,caps,&route,1);
    owner.Active = 0;
    finished = 1;
    if (!restored || !closed || !result || !x86_irq_ready() ||
        !x86_apic_borrow(window,profile->Lapic,profile->CacheBits,0,pending_empty,&owner) ||
        !x86_apic_mapping_ready(context,window,inventory,topology,profile)) return 0;
    return report();
}

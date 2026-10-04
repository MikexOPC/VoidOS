#include "buffer.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/mm/devices/lapic.h"
#include "arch/x86/mm/window/page.h"
#include "lib/memory/address_ranges.h"
#define COM1 0x3f8U

// ====== memoria kernel ordinaria distinta da dispositivi e viste temporanee ===== //

static int range(const void *pointer, BOOT_U64 bytes)
{
    BOOT_U64 address = (BOOT_U64)pointer;
    return pointer && bytes &&
        address <= ~0ULL-bytes &&
        !os_address_ranges_overlap(address,bytes,
            X86_LAPIC_VIRTUAL-4096,12288) &&
        !os_address_ranges_overlap(address,bytes,
            X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) &&
        !os_address_ranges_overlap(address,bytes,
            128ULL<<39,1ULL<<39);
}

// ====== identita del buffer e indici bounded prima di ogni accesso ===== //

int x86_uart_tx_validate(const X86_UART_TX *buffer)
{
    if (!range(buffer,sizeof(*buffer)) || ((BOOT_U64)buffer & 7)) {
        return 0;
    }
    return buffer->Self == (BOOT_U64)buffer &&
        !buffer->Reserved &&
        x86_uart_tx_bounds(buffer->Length,buffer->Next,buffer->Done);
}

// ====== ogni scrittura e chiusura corrisponde a una consegna confermata ===== //

int x86_uart_tx_progress(const X86_UART_TX *buffer, BOOT_U32 deliveries)
{
    if (!x86_uart_tx_validate(buffer)) {
        return 0;
    }
    // Rust limita Next a 64 e Done a uno prima della somma.
    return deliveries ==
        buffer->Next+buffer->Done;
}

// ====== owner vergine senza riuso implicito durante una trasmissione ===== //

static int virgin(const X86_UART_TX *buffer)
{
    BOOT_U32 byte;
    if (!range(buffer,sizeof(*buffer)) || ((BOOT_U64)buffer & 7)) {
        return 0;
    }
    for (byte = 0; byte < sizeof(*buffer); ++byte) {
        if (((const unsigned char *)buffer)[byte]) {
            return 0;
        }
    }
    return 1;
}

// ====== copia posseduta con sorgente esplicita e output disgiunto ===== //

int x86_uart_tx_prepare(X86_UART_TX *buffer,
    const unsigned char *source, BOOT_U32 bytes)
{
    X86_UART_TX draft = {0};
    BOOT_U32 byte;
    if (!x86_exception_environment() ||
        !x86_uart_tx_bounds(bytes,0,0) ||
        !virgin(buffer) ||
        !range(source,bytes) ||
        os_address_ranges_overlap((BOOT_U64)source,bytes,
            (BOOT_U64)buffer,sizeof(*buffer))) {
        return 0;
    }
    draft.Self = (BOOT_U64)buffer;
    draft.Length = bytes;
    for (byte = 0; byte < bytes; ++byte) {
        ((volatile unsigned char *)draft.Data)[byte] = source[byte];
    }
    for (byte = 0; byte < sizeof(draft); ++byte) {
        ((volatile unsigned char *)buffer)[byte] = ((const unsigned char *)&draft)[byte];
    }
    return x86_uart_tx_validate(buffer);
}

// ====== DLAB escluso e unica sorgente THRE prima di scrivere dati ===== //

static int transmitting(void)
{
    return x86_read_port(COM1+3) == 3 &&
        x86_read_port(COM1+1) == 2;
}

// ====== THRE con FIFO funzionante oppure disabilitata ===== //

static int thre(unsigned char iir)
{
    return iir == 2 || iir == 0xc2;
}

// ====== errori ricevitore e FIFO impediscono un successo ambiguo ===== //

static int line(unsigned char lsr)
{
    return lsr != 255 &&
        !(lsr & 0x9e);
}

// ====== abilitazione solo dopo copia pronta e trasmettitore completamente vuoto ===== //

int x86_uart_tx_start(const X86_UART_TX *buffer)
{
    unsigned char lsr;
    if (!x86_exception_environment() ||
        !x86_uart_tx_validate(buffer) ||
        buffer->Next ||
        buffer->Done ||
        x86_read_port(COM1+3) != 3 || x86_read_port(COM1+1)) {
        return 0;
    }
    lsr = x86_read_port(COM1+5);
    if (!line(lsr) || (lsr & 0x60) != 0x60) {
        return 0;
    }
    x86_write_port(COM1+1,2);
    if (x86_read_port(COM1+1) != 2) {
        // Il chiamante chiude la sorgente e la rotta anche dopo un errore di avvio.
        return 0;
    }
    return 1;
}

// ====== coda esaurita chiude IER senza un nuovo accesso al THR ===== //

static int finish(X86_UART_TX *buffer)
{
    unsigned char iir;
    x86_write_port(COM1+1,0);
    if (x86_read_port(COM1+1)) {
        return 0;
    }
    iir = x86_read_port(COM1+2);
    if (iir != 1 && iir != 0xc1) {
        return 0;
    }
    buffer->Done = 1;
    return 1;
}

// ====== un byte massimo per IRQ senza attese log o allocazioni ===== //

int x86_uart_tx_service(X86_UART_TX *buffer)
{
    unsigned char iir, lsr;
    BOOT_U32 index;
    if (!x86_exception_environment() ||
        !x86_uart_tx_validate(buffer) ||
        buffer->Done ||
        !transmitting()) {
        return 0;
    }
    iir = x86_read_port(COM1+2);
    if (!thre(iir)) {
        return 0;
    }
    lsr = x86_read_port(COM1+5);
    if (!line(lsr) || !(lsr & 0x20)) {
        return 0;
    }
    index = buffer->Next;
    if (index == buffer->Length) {
        return finish(buffer);
    }
    // Gli indici sono gia verificati. Nessun produttore modifica il buffer con IF=1.
    x86_write_port(COM1,buffer->Data[index]);
    buffer->Next = index+1;
    // Non leggere IIR dopo THR: la nuova causa THRE puo gia essere pendente.
    return 1;
}

// ====== TEMT prova la fine del flusso dopo lultimo IRQ e CLI ===== //

int x86_uart_tx_drain(const X86_UART_TX *buffer)
{
    BOOT_U32 attempt;
    if (!x86_exception_environment() ||
        !x86_uart_tx_validate(buffer) ||
        !buffer->Done ||
        x86_read_port(COM1+3) != 3 || x86_read_port(COM1+1)) {
        return 0;
    }
    for (attempt = 0; attempt < 100000; ++attempt) {
        unsigned char lsr = x86_read_port(COM1+5);
        if (!line(lsr)) {
            return 0;
        }
        if ((lsr & 0x60) == 0x60) {
            return 1;
        }
    }
    return 0;
}

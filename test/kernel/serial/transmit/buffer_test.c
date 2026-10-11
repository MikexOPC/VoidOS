#include "arch/x86/kernel/serial/transmit/buffer.h"
static X86_UART_TX buffer;
static unsigned char ports[8], wire[64];
static BOOT_U32 writes, bad_port, environment, block_ier, busy, reads;
static unsigned char closed_iir;

// ====== UART simulata con THRE distinto da TEMT ===== //

int x86_exception_environment(void) { return (int)environment; }
unsigned char x86_read_port(unsigned short port)
{
    if (port < 0x3f8 || port > 0x3ff) { bad_port = 1; return 255; }
    if (port == 0x3fd) {
        ++reads;
        return busy ? 0x20 : ports[5];
    }
    if (port == 0x3fa && !ports[1] && closed_iir) return closed_iir;
    return ports[port-0x3f8];
}
void x86_write_port(unsigned short port, unsigned char value)
{
    if (port == 0x3f8) {
        if (writes >= 64) { bad_port = 1; return; }
        wire[writes++] = value;
    } else if (port == 0x3f9) {
        if (!block_ier) ports[1] = value;
        if (!value && !block_ier) ports[2] = 0xc1;
    } else bad_port = 1;
}

// ====== stato vergine e dispositivi ripristinati fra prove ===== //

static void reset(void)
{
    BOOT_U32 byte;
    for (byte = 0; byte < sizeof(buffer); ++byte) ((unsigned char *)&buffer)[byte] = 0;
    for (byte = 0; byte < 64; ++byte) wire[byte] = 0;
    for (byte = 0; byte < 8; ++byte) ports[byte] = 0;
    ports[1] = 2; ports[2] = 0xc2; ports[3] = 3; ports[5] = 0x60;
    writes = bad_port = block_ier = busy = reads = 0;
    environment = 1;
    closed_iir = 0;
}

// ====== tutte le lunghezze e copia privata indipendente dal chiamante ===== //

static int lengths(void)
{
    unsigned char source[64];
    BOOT_U32 length, index;
    for (length = 1; length <= 64; ++length) {
        reset();
        for (index = 0; index < 64; ++index) source[index] = (unsigned char)(index+length);
        if (!x86_uart_tx_prepare(&buffer,source,length)) return 1;
        for (index = 0; index < 64; ++index) source[index] = 0;
        for (index = 0; index < length; ++index) {
            if (!x86_uart_tx_service(&buffer) || writes != index+1 || buffer.Next != index+1 ||
                buffer.Done || wire[index] != (unsigned char)(index+length) || ports[1] != 2 ||
                !x86_uart_tx_progress(&buffer,index+1) || x86_uart_tx_progress(&buffer,index)) return 2;
        }
        if (!x86_uart_tx_service(&buffer) || !buffer.Done || ports[1] ||
            writes != length || !x86_uart_tx_drain(&buffer) || bad_port ||
            !x86_uart_tx_progress(&buffer,length+1)) return 3;
        if (x86_uart_tx_service(&buffer) || writes != length) return 4;
        for (index = length; index < 64; ++index) if (buffer.Data[index]) return 5;
    }
    return 0;
}

// ====== overflow alias owner spostato e preparazione fallita senza scritture ===== //

static int ownership(void)
{
    X86_UART_TX copy;
    unsigned char source[64] = {1};
    BOOT_U32 index;
    reset();
    if (x86_uart_tx_prepare(0,source,1) || x86_uart_tx_prepare(&buffer,0,1) ||
        x86_uart_tx_prepare(&buffer,source,0) || x86_uart_tx_prepare(&buffer,source,65) ||
        x86_uart_tx_prepare(&buffer,source,~0U) ||
        x86_uart_tx_prepare(&buffer,buffer.Data,1)) return 1;
    for (index = 0; index < sizeof(buffer); ++index) if (((unsigned char *)&buffer)[index]) return 2;
    if (!x86_uart_tx_prepare(&buffer,source,64) || x86_uart_tx_prepare(&buffer,source,1)) return 3;
    copy = buffer;
    if (x86_uart_tx_validate(&copy) || x86_uart_tx_service(&copy) || writes) return 4;
    environment = 0;
    if (x86_uart_tx_service(&buffer) || writes) return 5;
    return 0;
}

// ====== tutti i bit dei metadati e stati impossibili prima del THR ===== //

static int corruption(void)
{
    unsigned char source[2] = {3,5};
    BOOT_U32 bit;
    for (bit = 0; bit < 64; ++bit) {
        reset();
        if (!x86_uart_tx_prepare(&buffer,source,2)) return 1;
        buffer.Self ^= 1ULL<<bit;
        if (x86_uart_tx_service(&buffer) || writes) return 2;
    }
    for (bit = 0; bit < 32; ++bit) {
        reset();
        if (!x86_uart_tx_prepare(&buffer,source,2)) return 3;
        buffer.Reserved = 1U<<bit;
        if (x86_uart_tx_service(&buffer) || writes) return 4;
    }
    reset();
    if (!x86_uart_tx_prepare(&buffer,source,2)) return 5;
    buffer.Next = 3;
    if (x86_uart_tx_service(&buffer) || writes) return 6;
    buffer.Next = 0; buffer.Done = 1;
    if (x86_uart_tx_service(&buffer) || writes) return 7;
    buffer.Done = 2;
    if (x86_uart_tx_service(&buffer) || writes) return 8;
    return 0;
}

// ====== cause inattese DLAB errori LSR e IER bloccato ===== //

static int hardware(void)
{
    unsigned char source = 0x7a;
    BOOT_U32 variant;
    for (variant = 0; variant < 8; ++variant) {
        reset();
        if (!x86_uart_tx_prepare(&buffer,&source,1)) return 1;
        if (variant == 0) ports[2] = 0xc4;
        if (variant == 1) ports[2] = 0x82;
        if (variant == 2) ports[3] = 0x83;
        if (variant == 3) ports[1] = 3;
        if (variant == 4) ports[5] = 0x40;
        if (variant == 5) ports[5] = 255;
        if (variant == 6) ports[5] = 0x62;
        if (variant == 7) ports[5] = 0xe0;
        if (x86_uart_tx_service(&buffer) || writes || buffer.Next) return 2;
    }
    reset();
    if (!x86_uart_tx_prepare(&buffer,&source,1) || !x86_uart_tx_service(&buffer)) return 3;
    block_ier = 1;
    if (x86_uart_tx_service(&buffer) || buffer.Done || writes != 1) return 4;
    for (variant = 0; variant < 2; ++variant) {
        reset();
        if (!x86_uart_tx_prepare(&buffer,&source,1) || !x86_uart_tx_service(&buffer)) return 5;
        closed_iir = variant ? 255 : 0xc5;
        if (x86_uart_tx_service(&buffer) || buffer.Done || ports[1] || writes != 1) return 6;
    }
    return 0;
}

// ====== ultimo byte consegnato al THR non significa linea gia svuotata ===== //

static int draining(void)
{
    unsigned char source = 9;
    reset();
    if (!x86_uart_tx_prepare(&buffer,&source,1) || x86_uart_tx_drain(&buffer)) return 1;
    if (!x86_uart_tx_service(&buffer) || !x86_uart_tx_service(&buffer)) return 2;
    busy = 1; reads = 0;
    if (x86_uart_tx_drain(&buffer) || reads != 100000 || writes != 1) return 3;
    busy = 0;
    if (!x86_uart_tx_drain(&buffer)) return 4;
    ports[1] = 2;
    if (x86_uart_tx_drain(&buffer)) return 5;
    return 0;
}

// ====== gruppi con esito diagnostico distinto ===== //

static int starting(void)
{
    unsigned char source = 11;
    BOOT_U32 variant;
    for (variant = 0; variant < 7; ++variant) {
        reset(); ports[1] = 0;
        if (!x86_uart_tx_prepare(&buffer,&source,1)) return 1;
        if (variant == 1) ports[1] = 1;
        if (variant == 2) ports[3] = 0x83;
        if (variant == 3) ports[5] = 0x20;
        if (variant == 4) ports[5] = 0x62;
        if (variant == 5) block_ier = 1;
        if (variant == 6) environment = 0;
        if (x86_uart_tx_start(&buffer) != (variant == 0) || writes || buffer.Next) return 2;
    }
    return 0;
}

// ====== indirizzi impossibili rifiutati prima di leggerne i bytes ===== //

static int address_bounds(void)
{
    unsigned char source = 1;
    reset();
    if (x86_uart_tx_prepare(&buffer,(const unsigned char *)(~0ULL-1),64) ||
        x86_uart_tx_prepare((X86_UART_TX *)((BOOT_U64)&buffer+1),&source,1) ||
        x86_uart_tx_prepare(&buffer,(const unsigned char *)0x0000700000120000ULL,1) ||
        x86_uart_tx_prepare((X86_UART_TX *)0x0000700000120000ULL,&source,1) ||
        x86_uart_tx_prepare(&buffer,(const unsigned char *)(128ULL<<39),1)) return 1;
    return 0;
}

// ====== esecuzione dei gruppi del buffer e del protocollo UART ===== //

static int reuse(void)
{
    unsigned char source[64];
    X86_UART_TX saved;
    BOOT_U32 cycle, byte, length, base = 3;
    reset(); ports[1] = 0;
    for (cycle = 0; cycle < 256; ++cycle) {
        length = cycle & 1 ? 1 : 64;
        for (byte = 0; byte < 64; ++byte) source[byte] = (unsigned char)(cycle+byte);
        if (!x86_uart_tx_prepare(&buffer,source,length) || x86_uart_tx_release(&buffer,0)) return 1;
        if (!x86_uart_tx_start(&buffer)) return 2;
        for (byte = 0; byte <= length; ++byte) {
            ports[2] = 0xc2;
            if (!x86_uart_tx_service(&buffer)) return 3;
        }
        if (writes != length || !x86_uart_tx_drain(&buffer)) return 4;
        saved = buffer;
        if (x86_uart_tx_release(&buffer,length)) return 5;
        for (byte = 0; byte < sizeof(buffer); ++byte)
            if (((unsigned char *)&buffer)[byte] != ((unsigned char *)&saved)[byte]) return 6;
        if (x86_uart_tx_goal(base,length) != base+length+1 ||
            !x86_uart_tx_release(&buffer,length+1) || x86_uart_tx_release(&buffer,length+1)) return 7;
        base += length+1;
        for (byte = 0; byte < sizeof(buffer); ++byte) if (((unsigned char *)&buffer)[byte]) return 8;
        writes = 0;
    }
    return 0;
}

// ====== reset rifiutato lascia intatta la prova precedente ===== //

static int release_failures(void)
{
    unsigned char source = 8;
    X86_UART_TX saved;
    BOOT_U32 variant, byte;
    for (variant = 0; variant < 6; ++variant) {
        reset();
        if (!x86_uart_tx_prepare(&buffer,&source,1) || !x86_uart_tx_service(&buffer) ||
            !x86_uart_tx_service(&buffer)) return 1;
        saved = buffer;
        if (variant == 0) environment = 0;
        if (variant == 1) busy = 1;
        if (variant == 2) ports[1] = 2;
        if (variant == 3) ports[3] = 0x83;
        if (variant == 4) closed_iir = 255;
        if (variant == 5) closed_iir = 0xc2;
        if (x86_uart_tx_release(&buffer,2)) return 2;
        for (byte = 0; byte < sizeof(buffer); ++byte)
            if (((unsigned char *)&buffer)[byte] != ((unsigned char *)&saved)[byte]) return 3;
    }
    return 0;
}

// ====== test buffer trasmissione e riuso esplicito ===== //

__declspec(dllexport) int run_uart_tx_tests(void)
{
    int result = lengths();
    if (result) return 100+result;
    result = ownership(); if (result) return 200+result;
    result = corruption(); if (result) return 300+result;
    result = hardware(); if (result) return 400+result;
    result = draining(); if (result) return 500+result;
    result = starting(); if (result) return 600+result;
    result = address_bounds(); if (result) return 700+result;
    result = reuse(); if (result) return 800+result;
    result = release_failures(); if (result) return 900+result;
    return 0;
}

#include "icr.h"

// ====== read_private ======

static uint32_t read_private(const X86_ICR_PORT *port, uint32_t offset, uint32_t *value) {
    if (!port->Poll(port->Budget)) return X86_ICR_STOPPED;
    return port->Read(port->Device, offset, value) ? X86_ICR_OK : X86_ICR_IO;
}

// ====== write_private ======

static uint32_t write_private(const X86_ICR_PORT *port, uint32_t offset, uint32_t value) {
    if (!port->Poll(port->Budget)) return X86_ICR_STOPPED;
    return port->Write(port->Device, offset, value) ? X86_ICR_OK : X86_ICR_IO;
}

// ====== idle_private ======

// Intel SDM 078US 11.6.1: wait until the previous IPI left the local APIC.
static uint32_t idle_private(const X86_ICR_PORT *port) {
    for (;;) {
        uint32_t value = 0;
        uint32_t status = read_private(port, X86_ICR_LOW, &value);
        if (status != X86_ICR_OK) return status;
        if (!(value & X86_ICR_PENDING)) return X86_ICR_OK;
        port->Pause(port->Device);
    }
}

// ====== errors_private ======

// Intel SDM 078US 11.5.3: a write to ESR latches the errors seen since the last write.
static uint32_t errors_private(const X86_ICR_PORT *port) {
    uint32_t value = 0;
    uint32_t status = write_private(port, X86_ICR_ESR, 0);
    if (status == X86_ICR_OK) status = read_private(port, X86_ICR_ESR, &value);
    if (status != X86_ICR_OK) return status;
    return value ? X86_ICR_APIC : X86_ICR_OK;
}

// ====== x86_icr_send ======

uint32_t x86_icr_send(const X86_ICR_PORT *port, uint32_t destination, uint32_t command, uint32_t flags) {
    if (!port || !port->Read || !port->Write || !port->Poll || !port->Pause ||
        destination > 0xff || (flags & ~(X86_ICR_CHECK_ESR | X86_ICR_WAIT_SENT)))
        return X86_ICR_INVALID;
    int check = (flags & X86_ICR_CHECK_ESR) != 0;
    uint32_t status = idle_private(port);
    if (status == X86_ICR_OK && check) status = errors_private(port);
    // Intel SDM 078US 11.6.1: writing the low half sends, so the destination goes first.
    if (status == X86_ICR_OK) status = write_private(port, X86_ICR_HIGH, destination << 24);
    if (status == X86_ICR_OK) status = write_private(port, X86_ICR_LOW, command);
    if (status != X86_ICR_OK || !(flags & X86_ICR_WAIT_SENT)) return status;
    status = idle_private(port);
    if (status == X86_ICR_OK && check) status = errors_private(port);
    return status;
}

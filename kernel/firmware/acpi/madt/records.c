#include "topology.h"
#include "lib/memory/address_ranges.h"

// ====== campi firmware decodificati senza strutture packed ===== //

static OS_ACPI_U64 number(const unsigned char *data, OS_ACPI_U32 bytes)
{
    OS_ACPI_U64 value = 0;
    OS_ACPI_U32 index;
    for (index = 0; index < bytes; ++index) {
        value |= (OS_ACPI_U64)data[index] << (index * 8);
    }
    return value;
}

// ====== pagine MMIO solo numeriche nessun mapping o accesso ai registri ===== //

static int controller_address(OS_ACPI_U64 address, OS_ACPI_U32 bits)
{
    return !(address & 4095) && kernel_acpi_physical_span(address, 4096, bits);
}

// ====== CPU non utilizzabili ignorate secondo la specifica ===== //

static int append_cpu(KERNEL_MADT_TOPOLOGY *draft, const unsigned char *data)
{
    KERNEL_MADT_CPU cpu;
    OS_ACPI_U32 index, raw = (OS_ACPI_U32)number(data + (data[0] == 0 ? 4 : 8), 4);
    cpu.Flags = raw & 1 ? 1 : raw & 2;
    if (!cpu.Flags) {
        return 1;
    }
    cpu.Kind = data[0];
    cpu.Uid = data[0] == 0 ? data[2] : (OS_ACPI_U32)number(data + 12, 4);
    cpu.Id = data[0] == 0 ? data[3] : (OS_ACPI_U32)number(data + 4, 4);
    if (draft->CpuCount == KERNEL_MADT_CPUS || !kernel_madt_cpu_fields(cpu.Uid, cpu.Id, cpu.Flags, cpu.Kind)) return 0;
    for (index = 0; index < draft->CpuCount; ++index) {
        if (cpu.Uid == draft->Cpus[index].Uid || cpu.Id == draft->Cpus[index].Id) return 0;
    }
    draft->Cpus[draft->CpuCount++] = cpu;
    return 1;
}

// ====== I O APIC distinti senza inventare il numero delle redirection entries ===== //

static int append_io(KERNEL_MADT_TOPOLOGY *draft, const unsigned char *data, OS_ACPI_U32 bits)
{
    KERNEL_MADT_IO_APIC io;
    OS_ACPI_U32 index;
    io.Id = data[2]; io.Address = number(data + 4, 4); io.GsiBase = (OS_ACPI_U32)number(data + 8, 4);
    if (draft->IoCount == KERNEL_MADT_IO || !controller_address(io.Address, bits)) return 0;
    for (index = 0; index < draft->IoCount; ++index) {
        if (io.Id == draft->Io[index].Id || io.Address == draft->Io[index].Address || io.GsiBase == draft->Io[index].GsiBase) return 0;
    }
    draft->Io[draft->IoCount++] = io;
    return 1;
}

// ====== un override ISA per sorgente e flag MPS normalizzati ===== //

static int append_iso(KERNEL_MADT_TOPOLOGY *draft, const unsigned char *data)
{
    KERNEL_MADT_ISA_OVERRIDE iso;
    OS_ACPI_U32 index, raw = (OS_ACPI_U32)number(data + 8, 2);
    if (data[2] || data[3] > 15 || draft->IsoCount == KERNEL_MADT_ISO || !kernel_madt_interrupt_flags(raw)) return 0;
    iso.Source = data[3]; iso.Gsi = (OS_ACPI_U32)number(data + 4, 4); iso.Flags = raw & 15; iso.Reserved = 0;
    for (index = 0; index < draft->IsoCount; ++index) {
        if (iso.Source == draft->Iso[index].Source) return 0;
    }
    draft->Iso[draft->IsoCount++] = iso;
    return 1;
}

// ====== connessioni NMI globali e locali conservate senza abilitazione ===== //

static int append_nmi(KERNEL_MADT_TOPOLOGY *draft, const unsigned char *data)
{
    KERNEL_MADT_NMI_SOURCE nmi;
    OS_ACPI_U32 offset = data[0] == 4 ? 3 : 2;
    OS_ACPI_U32 raw = (OS_ACPI_U32)number(data + offset, 2);
    if (draft->NmiCount == KERNEL_MADT_NMI || !kernel_madt_interrupt_flags(raw)) return 0;
    nmi.Kind = data[0]; nmi.Flags = raw & 15;
    nmi.Uid = data[0] == 3 ? 0 : data[0] == 4 ? (data[2] == 255 ? 0xffffffffU : data[2]) : (OS_ACPI_U32)number(data + 4, 4);
    nmi.Value = data[0] == 3 ? (OS_ACPI_U32)number(data + 4, 4) : data[data[0] == 4 ? 5 : 8];
    if (nmi.Kind != 3 && nmi.Value > 1) return 0;
    draft->Nmi[draft->NmiCount++] = nmi;
    return 1;
}

// ====== dispatcher dopo validazione della dimensione completa del record ===== //

int kernel_madt_append(KERNEL_MADT_TOPOLOGY *draft, const unsigned char *data, OS_ACPI_U32 bytes, OS_ACPI_U32 bits)
{
    if (!draft || !data || bytes < 2 || bits < 32 || bits > 52 || draft->Ready ||
        draft->CpuCount > KERNEL_MADT_CPUS || draft->IoCount > KERNEL_MADT_IO ||
        draft->IsoCount > KERNEL_MADT_ISO || draft->NmiCount > KERNEL_MADT_NMI ||
        draft->UnknownCount > 1024 || data[1] != bytes || !kernel_madt_record_span(data[0], bytes, bytes)) {
        return 0;
    }
    if (os_address_ranges_overlap((OS_ACPI_U64)draft, sizeof(*draft), (OS_ACPI_U64)data, bytes)) return 0;
    switch (data[0]) {
    case 0: case 9: return append_cpu(draft, data);
    case 1: return append_io(draft, data, bits);
    case 2: return append_iso(draft, data);
    case 3: case 4: case 10: return append_nmi(draft, data);
    case 5:
        if (draft->Override || !controller_address(number(data + 4, 8), bits)) return 0;
        draft->Lapic = number(data + 4, 8); draft->Override = 1;
        return 1;
    default:
        if (draft->UnknownCount == 1024) return 0;
        ++draft->UnknownCount;
        return 1;
    }
}

// ====== coda e campi riservati della fotografia devono rimanere zero ===== //

static int zero(const void *data, OS_ACPI_U32 bytes)
{
    OS_ACPI_U32 index;
    for (index = 0; index < bytes; ++index) {
        if (((const unsigned char *)data)[index]) return 0;
    }
    return 1;
}

// ====== NMI distinti e disgiunti dagli override dei dispositivi ===== //

static int nmi_valid(const KERNEL_MADT_TOPOLOGY *topology)
{
    OS_ACPI_U32 index, previous;
    for (index = 0; index < topology->NmiCount; ++index) {
        const KERNEL_MADT_NMI_SOURCE *nmi = &topology->Nmi[index];
        if ((nmi->Kind != 3 && nmi->Kind != 4 && nmi->Kind != 10) || nmi->Flags > 15 ||
            !kernel_madt_interrupt_flags(nmi->Flags) || (nmi->Kind == 3 ? nmi->Uid != 0 : nmi->Value > 1) ||
            (nmi->Kind == 4 && nmi->Uid != 0xffffffffU && nmi->Uid > 255)) return 0;
        for (previous = 0; previous < index; ++previous) {
            const KERNEL_MADT_NMI_SOURCE *other = &topology->Nmi[previous];
            if (nmi->Kind == 3 && other->Kind == 3 && nmi->Value == other->Value) return 0;
            if (nmi->Kind != 3 && other->Kind != 3 && nmi->Value == other->Value &&
                (nmi->Uid == other->Uid || nmi->Uid == 0xffffffffU || other->Uid == 0xffffffffU)) return 0;
        }
        if (nmi->Kind == 3) {
            for (previous = 0; previous < topology->IsoCount; ++previous) {
                if (nmi->Value == topology->Iso[previous].Gsi) return 0;
            }
        }
    }
    return 1;
}

// ====== verifica completa della topologia persistente prima di futuri consumatori ===== //

int kernel_madt_validate(const KERNEL_MADT_TOPOLOGY *topology, OS_ACPI_U32 bits)
{
    OS_ACPI_U32 index, previous, minimum;
    if (!topology || topology->Ready != 1 || topology->Pcat > 1 || topology->Override > 1 ||
        topology->Reserved[0] || topology->Reserved[1] || topology->UnknownCount > 1024 ||
        topology->CpuCount > KERNEL_MADT_CPUS || topology->IoCount > KERNEL_MADT_IO ||
        topology->IsoCount > KERNEL_MADT_ISO || topology->NmiCount > KERNEL_MADT_NMI ||
        !kernel_madt_header_fields(0x43495041, topology->Length, topology->Revision) ||
        !kernel_acpi_physical_span(topology->Physical, topology->Length, bits) || !controller_address(topology->Lapic, bits)) return 0;
    if (topology->CpuCount + topology->IoCount + topology->IsoCount + topology->NmiCount +
        topology->UnknownCount + topology->Override > 1024 ||
        os_address_ranges_overlap(topology->Lapic, 4096, topology->Physical, topology->Length)) return 0;
    minimum = 44 + topology->IoCount * 12 + topology->IsoCount * 10 + topology->UnknownCount * 2 + topology->Override * 12;
    for (index = 0; index < KERNEL_MADT_CPUS; ++index) {
        const KERNEL_MADT_CPU *cpu = &topology->Cpus[index];
        if (index >= topology->CpuCount) { if (!zero(cpu, sizeof(*cpu))) return 0; continue; }
        if (!kernel_madt_cpu_fields(cpu->Uid, cpu->Id, cpu->Flags, cpu->Kind)) return 0;
        minimum += cpu->Kind == 0 ? 8 : 16;
        for (previous = 0; previous < index; ++previous)
            if (cpu->Uid == topology->Cpus[previous].Uid || cpu->Id == topology->Cpus[previous].Id) return 0;
    }
    for (index = 0; index < KERNEL_MADT_IO; ++index) {
        const KERNEL_MADT_IO_APIC *io = &topology->Io[index];
        if (index >= topology->IoCount) { if (!zero(io, sizeof(*io))) return 0; continue; }
        if (io->Id > 255 || !controller_address(io->Address, bits) || io->Address == topology->Lapic ||
            os_address_ranges_overlap(io->Address, 4096, topology->Physical, topology->Length)) return 0;
        for (previous = 0; previous < index; ++previous)
            if (io->Id == topology->Io[previous].Id || io->Address == topology->Io[previous].Address || io->GsiBase == topology->Io[previous].GsiBase) return 0;
    }
    for (index = 0; index < KERNEL_MADT_ISO; ++index) {
        const KERNEL_MADT_ISA_OVERRIDE *iso = &topology->Iso[index];
        if (index >= topology->IsoCount) { if (!zero(iso, sizeof(*iso))) return 0; continue; }
        if (iso->Source > 15 || iso->Flags > 15 || iso->Reserved || !kernel_madt_interrupt_flags(iso->Flags)) return 0;
        for (previous = 0; previous < index; ++previous) {
            if (iso->Source == topology->Iso[previous].Source) return 0;
        }
    }
    for (index = 0; index < KERNEL_MADT_NMI; ++index) {
        if (index >= topology->NmiCount) {
            if (!zero(&topology->Nmi[index], sizeof(topology->Nmi[index]))) return 0;
            continue;
        }
        minimum += topology->Nmi[index].Kind == 3 ? 8 : topology->Nmi[index].Kind == 4 ? 6 : 12;
    }
    return minimum <= topology->Length && nmi_valid(topology);
}

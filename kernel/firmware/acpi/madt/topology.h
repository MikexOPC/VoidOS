#ifndef OS_KERNEL_MADT_TOPOLOGY_H
#define OS_KERNEL_MADT_TOPOLOGY_H
#include "kernel/firmware/acpi/root.h"
#define KERNEL_MADT_CPUS 64U
#define KERNEL_MADT_IO 16U
#define KERNEL_MADT_ISO 16U
#define KERNEL_MADT_NMI 64U
typedef struct { OS_ACPI_U32 Uid, Id, Flags, Kind; } KERNEL_MADT_CPU;
typedef struct { OS_ACPI_U64 Address; OS_ACPI_U32 Id, GsiBase; } KERNEL_MADT_IO_APIC;
typedef struct { OS_ACPI_U32 Source, Gsi, Flags, Reserved; } KERNEL_MADT_ISA_OVERRIDE;
typedef struct { OS_ACPI_U32 Uid, Value, Flags, Kind; } KERNEL_MADT_NMI_SOURCE;
typedef struct {
    OS_ACPI_U64 Physical, Lapic;
    OS_ACPI_U32 Length, Revision, Pcat, Override;
    OS_ACPI_U32 CpuCount, IoCount, IsoCount, NmiCount, UnknownCount, Ready, Reserved[2];
    KERNEL_MADT_CPU Cpus[KERNEL_MADT_CPUS];
    KERNEL_MADT_IO_APIC Io[KERNEL_MADT_IO];
    KERNEL_MADT_ISA_OVERRIDE Iso[KERNEL_MADT_ISO];
    KERNEL_MADT_NMI_SOURCE Nmi[KERNEL_MADT_NMI];
} KERNEL_MADT_TOPOLOGY;
_Static_assert(sizeof(KERNEL_MADT_TOPOLOGY) == 2624, "Topologia MADT bounded");
OS_ACPI_U32 kernel_madt_header_fields(OS_ACPI_U32 signature, OS_ACPI_U32 length, OS_ACPI_U32 revision);
OS_ACPI_U32 kernel_madt_record_span(OS_ACPI_U32 type, OS_ACPI_U32 length, OS_ACPI_U32 remaining);
OS_ACPI_U32 kernel_madt_interrupt_flags(OS_ACPI_U32 flags);
OS_ACPI_U32 kernel_madt_cpu_fields(OS_ACPI_U32 uid, OS_ACPI_U32 id, OS_ACPI_U32 flags, OS_ACPI_U32 kind);
int kernel_madt_append(KERNEL_MADT_TOPOLOGY *draft, const unsigned char *record, OS_ACPI_U32 bytes, OS_ACPI_U32 bits);
int kernel_madt_validate(const KERNEL_MADT_TOPOLOGY *topology, OS_ACPI_U32 bits);
int kernel_madt_capture(const KERNEL_ACPI_DIRECTORY *directory, OS_ACPI_U32 bits,
                        KERNEL_ACPI_READ read, void *context, KERNEL_MADT_TOPOLOGY *output);
#endif

#include "topology.h"
#include "lib/memory/address_ranges.h"
#include "platform.h"
#include "kernel/diagnostics/log.h"
typedef struct {
    const KERNEL_BOOT_CONTEXT *Context;
    X86_PAGE_WINDOW *Window;
} MADT_READER;

// ====== numeri little endian della sola copia locale ===== //

static OS_ACPI_U64 number(const unsigned char *data, OS_ACPI_U32 bytes)
{
    OS_ACPI_U64 value = 0;
    OS_ACPI_U32 index;
    for (index = 0; index < bytes; ++index) value |= (OS_ACPI_U64)data[index] << (index * 8);
    return value;
}

// ====== ricerca MADT unica senza dereferenziare indirizzi della directory ===== //

static int find_madt(const KERNEL_ACPI_DIRECTORY *directory, KERNEL_ACPI_READ read, void *context,
                      unsigned char *selected, OS_ACPI_U64 *physical)
{
    unsigned char header[36];
    OS_ACPI_U32 index, byte, count = 0;
    for (index = 0; index < directory->Count; ++index) {
        if (!read(context, directory->Entries[index], 36, header)) {
            return 0;
        }
        if (number(header, 4) != 0x43495041) {
            continue;
        }
        if (++count != 1) {
            return 0;
        }
        *physical = directory->Entries[index];
        for (byte = 0; byte < 36; ++byte) selected[byte] = header[byte];
    }
    return count == 1;
}

// ====== checksum incrementale senza buffer dell intera tabella ===== //

static OS_ACPI_U32 checksum(const unsigned char *data, OS_ACPI_U32 bytes)
{
    OS_ACPI_U32 index, sum = 0;
    for (index = 0; index < bytes; ++index) sum += data[index];
    return sum;
}

// ====== preflight di header esteso prima della scansione dei record ===== //

static int prepare_header(const unsigned char *selected, OS_ACPI_U64 physical, OS_ACPI_U32 bits,
                           KERNEL_ACPI_READ read, void *context, KERNEL_MADT_TOPOLOGY *draft,
                           OS_ACPI_U32 *sum)
{
    unsigned char header[44];
    OS_ACPI_U32 index, length = (OS_ACPI_U32)number(selected + 4, 4);
    if (!kernel_madt_header_fields((OS_ACPI_U32)number(selected, 4), length, selected[8]) ||
        !kernel_acpi_physical_span(physical, length, bits) || !read(context, physical, 44, header)) return 0;
    for (index = 0; index < 36; ++index) {
        if (selected[index] != header[index]) return 0;
    }
    draft->Physical = physical; draft->Length = length; draft->Revision = selected[8];
    draft->Lapic = number(header + 36, 4); draft->Pcat = (OS_ACPI_U32)number(header + 40, 4) & 1;
    *sum = checksum(header, 44);
    return 1;
}

// ====== lettore fisico con riserva ACPI e durata del prestito esplicite ===== //

static int read_physical(void *owner, OS_ACPI_U64 physical, OS_ACPI_U32 bytes, unsigned char *output)
{
    MADT_READER *reader = (MADT_READER *)owner;
    return reader && x86_page_window_read_acpi(reader->Window, reader->Context, physical, bytes, output);
}

// ====== copia persistente senza dipendenze memcpy o memset ===== //

static void publish(const KERNEL_MADT_TOPOLOGY *draft, KERNEL_MADT_TOPOLOGY *output)
{
    OS_ACPI_U32 index;
    for (index = 0; index < sizeof(*output); ++index) {
        ((volatile unsigned char *)output)[index] = ((const unsigned char *)draft)[index];
    }
}

// ====== topologia conservata nell arena solo dopo validazione e revoca dei mapping ===== //

int kernel_madt_initialize(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, X86_OBJECT_ARENA *arena,
                           const KERNEL_OBJECT_HANDLE *directory_object, KERNEL_OBJECT_HANDLE *output)
{
    const KERNEL_ACPI_DIRECTORY *directory;
    KERNEL_MADT_TOPOLOGY draft, *persistent;
    KERNEL_OBJECT_HANDLE object;
    MADT_READER reader = {context, window};
    OS_ACPI_U64 budget;
    OS_ACPI_U32 index, enabled = 0;
    if (!context || !window || !window->Pool || !arena || !directory_object || !output ||
        (OS_ACPI_U64)output > ~0ULL - sizeof(*output) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)context, sizeof(*context)) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)window, sizeof(*window)) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)window->Pool, sizeof(*window->Pool)) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)arena, sizeof(*arena)) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)arena->Objects.Arena, arena->Objects.Capacity) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)context->MemoryMap, context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)directory_object, sizeof(*directory_object))) return 0;
    for (index = 0; index < sizeof(*output); ++index) {
        if (((const unsigned char *)output)[index]) return 0;
    }
    if (!context->Info.Acpi.Source) return os_acpi_root_valid(&context->Info.Acpi, window->Pool->PhysicalBits);
    directory = (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(arena, directory_object);
    if (!directory || directory_object->Bytes != sizeof(*directory)) {
        return 0;
    }
    budget = window->Pool->FreePages;
    if (!kernel_section("discovery topologia MADT") ||
        !kernel_madt_capture(directory, window->Pool->PhysicalBits, read_physical, &reader, &draft) ||
        window->Pool->FreePages != budget || window->Active || window->Lease.Id) return 0;
    for (index = 0; index < sizeof(object); ++index) {
        ((volatile unsigned char *)&object)[index] = 0;
    }
    if (!x86_object_arena_allocate(arena, sizeof(draft), 8, &object)) return 0;
    persistent = (KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(arena, &object);
    if (!persistent) {
        x86_object_arena_release(arena, &object);
        return 0;
    }
    publish(&draft, persistent);
    if (!kernel_madt_validate(persistent, window->Pool->PhysicalBits)) {
        x86_object_arena_release(arena, &object);
        return 0;
    }
    *output = object;
    for (index = 0; index < persistent->CpuCount; ++index) {
        if (persistent->Cpus[index].Flags == 1) ++enabled;
    }
    if (!kernel_text("KERNEL_MADT_TOPOLOGY: ") || !kernel_number((OS_ACPI_U64)persistent) ||
        !kernel_text("\nKERNEL_MADT_PHYSICAL: ") || !kernel_number(persistent->Physical) ||
        !kernel_text("\nKERNEL_MADT_LAPIC: ") || !kernel_number(persistent->Lapic) ||
        !kernel_text("\nKERNEL_MADT_CPU_COUNT: ") || !kernel_number(persistent->CpuCount) ||
        !kernel_text("\nKERNEL_MADT_ENABLED_COUNT: ") || !kernel_number(enabled) ||
        !kernel_text("\nKERNEL_MADT_IO_COUNT: ") || !kernel_number(persistent->IoCount) ||
        !kernel_text("\nKERNEL_MADT_ISO_COUNT: ") || !kernel_number(persistent->IsoCount) ||
        !kernel_text("\nKERNEL_MADT_NMI_COUNT: ") || !kernel_number(persistent->NmiCount) ||
        !kernel_text("\nKERNEL_MADT_UNKNOWN_COUNT: ") || !kernel_number(persistent->UnknownCount) ||
        !kernel_text("\n")) return 0;
    if (persistent->UnknownCount && !kernel_log(2, "MADT_RECORDS_UNSUPPORTED")) return 0;
    return kernel_log(0, "KERNEL_MADT_DISCOVERY_OK");
}

// ====== ogni record letto solo dopo controlli su dimensione e rimanente ===== //

static int read_records(KERNEL_MADT_TOPOLOGY *draft, OS_ACPI_U32 bits, KERNEL_ACPI_READ read,
                         void *context, OS_ACPI_U32 *sum)
{
    unsigned char prefix[2], data[255];
    OS_ACPI_U32 offset = 44, count = 0;
    while (offset < draft->Length) {
        OS_ACPI_U32 remaining = draft->Length - offset;
        if (++count > 1024 || remaining < 2 || !read(context, draft->Physical + offset, 2, prefix) ||
            !kernel_madt_record_span(prefix[0], prefix[1], remaining)) return 0;
        if (!read(context, draft->Physical + offset, prefix[1], data) || data[0] != prefix[0] || data[1] != prefix[1] ||
            !kernel_madt_append(draft, data, prefix[1], bits)) return 0;
        *sum += checksum(data, prefix[1]);
        offset += prefix[1];
    }
    return !(*sum & 255);
}

// ====== pubblicazione atomica logica della sola bozza verificata ===== //

__declspec(noinline) int kernel_madt_capture(const KERNEL_ACPI_DIRECTORY *directory, OS_ACPI_U32 bits,
                        KERNEL_ACPI_READ read, void *context, KERNEL_MADT_TOPOLOGY *output)
{
    KERNEL_MADT_TOPOLOGY draft;
    unsigned char selected[36];
    OS_ACPI_U64 physical = 0;
    OS_ACPI_U32 index, sum;
    if (!directory || !read || !output || !kernel_acpi_directory_valid(directory, bits) ||
        (OS_ACPI_U64)output > ~0ULL - sizeof(*output) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)directory, sizeof(*directory))) return 0;
    if (!find_madt(directory, read, context, selected, &physical)) {
        return 0;
    }
    for (index = 0; index < sizeof(draft); ++index) {
        ((volatile unsigned char *)&draft)[index] = 0;
    }
    if (!prepare_header(selected, physical, bits, read, context, &draft, &sum) ||
        !read_records(&draft, bits, read, context, &sum)) return 0;
    draft.Ready = 1;
    if (!kernel_madt_validate(&draft, bits)) {
        return 0;
    }
    for (index = 0; index < sizeof(*output); ++index) {
        ((volatile unsigned char *)output)[index] = ((const unsigned char *)&draft)[index];
    }
    return 1;
}

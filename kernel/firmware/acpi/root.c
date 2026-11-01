#include "root.h"
#include "lib/memory/address_ranges.h"
#include "platform.h"
#include "kernel/diagnostics/log.h"
typedef struct {
    const KERNEL_BOOT_CONTEXT *Context;
    X86_PAGE_WINDOW *Window;
} ACPI_READER;

// ====== lettura locale little endian senza assumere allineamenti firmware ===== //

static OS_ACPI_U64 number(const unsigned char *data, OS_ACPI_U32 bytes)
{
    OS_ACPI_U64 value = 0;
    OS_ACPI_U32 index;
    for (index = 0; index < bytes; ++index) {
        value |= (OS_ACPI_U64)data[index] << (index * 8);
    }
    return value;
}

// ====== directory inizializzata senza memcpy memset o heap ===== //

static void clear_directory(KERNEL_ACPI_DIRECTORY *directory)
{
    OS_ACPI_U32 index;
    for (index = 0; index < sizeof(*directory); ++index) {
        ((volatile unsigned char *)directory)[index] = 0;
    }
}

// ====== indirizzi figli distinti e disgiunti dalla tabella root ===== //

static int entries_valid(const KERNEL_ACPI_DIRECTORY *directory, OS_ACPI_U32 bits)
{
    OS_ACPI_U32 index, previous;
    for (index = 0; index < KERNEL_ACPI_ROOT_ENTRIES; ++index) {
        OS_ACPI_U64 physical = directory->Entries[index];
        if (index >= directory->Count) {
            if (physical) return 0;
            continue;
        }
        if (!kernel_acpi_physical_span(physical, 36, bits) ||
            os_address_ranges_overlap(physical, 36, directory->Physical, directory->Length)) {
            return 0;
        }
        for (previous = 0; previous < index; ++previous) {
            if (physical == directory->Entries[previous]) return 0;
        }
    }
    return 1;
}

// ====== formato e metadati verificabili anche dopo la pubblicazione ===== //

int kernel_acpi_directory_valid(const KERNEL_ACPI_DIRECTORY *directory, OS_ACPI_U32 bits)
{
    OS_ACPI_U32 signature, layout;
    if (!directory || directory->Reserved || !kernel_acpi_physical_span(directory->Physical, directory->Length, bits)) {
        return 0;
    }
    signature = directory->Width == 8 ? 0x54445358 : 0x54445352;
    layout = kernel_acpi_root_layout(signature, directory->Length, 1, directory->Width);
    if (!layout || directory->Count != layout - 1) {
        return 0;
    }
    return entries_valid(directory, bits);
}

// ====== header letto separatamente prima di richiedere il contenuto completo ===== //

static int read_header(OS_ACPI_U64 physical, OS_ACPI_U32 width, OS_ACPI_U32 bits,
                       KERNEL_ACPI_READ read, void *context, unsigned char *header,
                       OS_ACPI_U32 *length, OS_ACPI_U32 *count)
{
    OS_ACPI_U32 layout;
    if (!kernel_acpi_physical_span(physical, 36, bits) || !read(context, physical, 36, header)) {
        return 0;
    }
    *length = (OS_ACPI_U32)number(header + 4, 4);
    layout = kernel_acpi_root_layout((OS_ACPI_U32)number(header, 4), *length, header[8], width);
    if (!layout || !kernel_acpi_physical_span(physical, *length, bits)) {
        return 0;
    }
    *count = layout - 1;
    return 1;
}

// ====== checksum su tutti i byte e coerenza tra le due letture ===== //

static int content_valid(const unsigned char *header, const unsigned char *table, OS_ACPI_U32 length)
{
    OS_ACPI_U32 index, checksum = 0;
    for (index = 0; index < length; ++index) {
        if (index < 36 && header[index] != table[index]) return 0;
        checksum += table[index];
    }
    return !(checksum & 255);
}

// ====== copia finale bounded solo dopo tutti i controlli ===== //

static void publish(const KERNEL_ACPI_DIRECTORY *draft, KERNEL_ACPI_DIRECTORY *output)
{
    OS_ACPI_U32 index;
    for (index = 0; index < sizeof(*output); ++index) {
        ((volatile unsigned char *)output)[index] = ((const unsigned char *)draft)[index];
    }
}

// ====== cattura root con limite esplicito e output immutato sugli errori ===== //

__declspec(noinline) static int capture_entries(const unsigned char *table, OS_ACPI_U64 physical,
                                                OS_ACPI_U32 width, OS_ACPI_U32 length, OS_ACPI_U32 count,
                                                OS_ACPI_U32 bits, KERNEL_ACPI_DIRECTORY *output)
{
    KERNEL_ACPI_DIRECTORY draft;
    OS_ACPI_U32 index;
    clear_directory(&draft);
    draft.Physical = physical;
    draft.Width = width;
    draft.Length = length;
    draft.Count = count;
    for (index = 0; index < count; ++index) {
        draft.Entries[index] = number(table + 36 + index * width, width);
    }
    if (!kernel_acpi_directory_valid(&draft, bits)) return 0;
    publish(&draft, output);
    return 1;
}

// ====== frame locali bounded e separati per evitare stack probe CRT ===== //

int kernel_acpi_root_capture(const OS_ACPI_ROOT *root, OS_ACPI_U32 bits,
                             KERNEL_ACPI_READ read, void *context, KERNEL_ACPI_DIRECTORY *output)
{
    unsigned char header[36];
    unsigned char table[36 + KERNEL_ACPI_ROOT_ENTRIES * 8];
    OS_ACPI_U64 physical;
    OS_ACPI_U32 width, length, count;
    if (!root || !read || !output || !os_acpi_root_valid(root, bits) || !root->Source ||
        (OS_ACPI_U64)output > ~0ULL - sizeof(*output) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)root, sizeof(*root))) {
        return 0;
    }
    physical = os_acpi_root_table(root, bits);
    width = root->Length == 36 ? 8 : 4;
    if (!read_header(physical, width, bits, read, context, header, &length, &count)) {
        return 0;
    }
    if (length > sizeof(table) || !read(context, physical, length, table) ||
        !content_valid(header, table, length)) {
        return 0;
    }
    return capture_entries(table, physical, width, length, count, bits, output);
}

// ====== adattatore che mantiene espliciti i prestiti del lettore ===== //

static int read_physical(void *owner, OS_ACPI_U64 physical, OS_ACPI_U32 bytes, unsigned char *output)
{
    ACPI_READER *reader = (ACPI_READER *)owner;
    return reader && x86_page_window_read_acpi(reader->Window, reader->Context, physical, bytes, output);
}

// ====== directory persistente dopo checksum e revoca delle viste temporanee ===== //

int kernel_acpi_initialize(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
                           X86_OBJECT_ARENA *arena, KERNEL_OBJECT_HANDLE *output)
{
    KERNEL_ACPI_DIRECTORY draft, *persistent;
    KERNEL_OBJECT_HANDLE object;
    ACPI_READER reader = {context, window};
    OS_ACPI_U64 budget;
    OS_ACPI_U32 index;
    if (!context || !window || !window->Pool || !arena || !output ||
        (OS_ACPI_U64)output > ~0ULL - sizeof(*output) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)context, sizeof(*context)) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)arena, sizeof(*arena)) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)arena->Objects.Arena, arena->Objects.Capacity) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)context->MemoryMap, context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)window, sizeof(*window)) ||
        os_address_ranges_overlap((OS_ACPI_U64)output, sizeof(*output), (OS_ACPI_U64)window->Pool, sizeof(*window->Pool))) {
        return 0;
    }
    for (index = 0; index < sizeof(*output); ++index) {
        if (((const unsigned char *)output)[index]) return 0;
    }
    if (!context->Info.Acpi.Source) return os_acpi_root_valid(&context->Info.Acpi, window->Pool->PhysicalBits);
    if (!kernel_physical_budget_read(window->Pool, &budget)) return 0;
    if (!kernel_section("directory tabelle ACPI") ||
        !kernel_acpi_root_capture(&context->Info.Acpi, window->Pool->PhysicalBits, read_physical, &reader, &draft) ||
        !kernel_physical_budget_matches(window->Pool, budget) || window->Active || window->Lease.Id) {
        return 0;
    }
    for (index = 0; index < sizeof(object); ++index) {
        ((volatile unsigned char *)&object)[index] = 0;
    }
    if (!x86_object_arena_allocate(arena, sizeof(draft), 8, &object)) {
        return 0;
    }
    persistent = (KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(arena, &object);
    if (!persistent) {
        x86_object_arena_release(arena, &object);
        return 0;
    }
    publish(&draft, persistent);
    if (!kernel_acpi_directory_valid(persistent, window->Pool->PhysicalBits)) {
        x86_object_arena_release(arena, &object);
        return 0;
    }
    *output = object;
    if (!kernel_text("KERNEL_ACPI_DIRECTORY: ") || !kernel_number((OS_ACPI_U64)persistent) ||
        !kernel_text("\nKERNEL_ACPI_TABLE_COUNT: ") || !kernel_number(persistent->Count) ||
        !kernel_text("\nKERNEL_ACPI_TABLE_BYTES: ") || !kernel_number(persistent->Length) ||
        !kernel_text("\n") || !kernel_log(0, persistent->Width == 8 ? "KERNEL_ACPI_XSDT_OK" : "KERNEL_ACPI_RSDT_OK")) {
        return 0;
    }
    return kernel_log(0, "KERNEL_ACPI_DIRECTORY_OK");
}

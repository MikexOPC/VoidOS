#include "elf/validate.h"

extern UINT32 elf_check_load_segment(UINT32 flags, UINT64 offset, UINT64 virtual_address,
                                    UINT64 file_bytes, UINT64 memory_bytes, UINT64 alignment,
                                    UINT64 file_size);
typedef char check_segment_result_codes[(ELF_VALID == 0 && ELF_UNSUPPORTED_PROFILE == 3 &&
                                        ELF_BAD_SEGMENT == 5) ? 1 : -1];

// ====== decodifica little endian senza cast del buffer ======

static UINT16 read16(const UINT8 *data) {
    return (UINT16)((UINT16)data[0] | ((UINT16)data[1] << 8));
}

static UINT32 read32(const UINT8 *data) {
    return (UINT32)read16(data) | ((UINT32)read16(data + 2) << 16);
}

static UINT64 read64(const UINT8 *data) {
    return (UINT64)read32(data) | ((UINT64)read32(data + 4) << 32);
}

// ====== controllo intervalli senza overflow ======

static int file_range(UINT64 offset, UINT64 bytes, UINTN size)
{
    return offset <= size && bytes <= size - offset;
}

static int overlaps_pages(const ELF_LOAD_SEGMENT *first, const ELF_LOAD_SEGMENT *second) {
    UINT64 first_start = first->VirtualAddress >> 12;
    UINT64 first_end = (first->VirtualAddress + first->MemoryBytes - 1) >> 12;
    UINT64 second_start = second->VirtualAddress >> 12;
    UINT64 second_end = (second->VirtualAddress + second->MemoryBytes - 1) >> 12;
    return first_start <= second_end && second_start <= first_end;
}

// ====== controlli di un segmento caricabile ======

static ELF_VALIDATION_RESULT read_segment(const UINT8 *header, UINTN size, ELF_LOAD_SEGMENT *segment) {

    UINT64 alignment = read64(header + 48);
    segment->Flags = read32(header + 4);
    segment->FileOffset = read64(header + 8);
    segment->VirtualAddress = read64(header + 16);
    segment->FileBytes = read64(header + 32);
    segment->MemoryBytes = read64(header + 40);

    return (ELF_VALIDATION_RESULT)elf_check_load_segment(segment->Flags, segment->FileOffset,
        segment->VirtualAddress, segment->FileBytes, segment->MemoryBytes, alignment, size);
}

// ====== validazione header e tabella programmi ======

static ELF_VALIDATION_RESULT validate_file(const UINT8 *file, UINTN size, ELF_LOAD_PLAN *plan) {

    UINT64 table_offset;
    UINT16 header_count;
    UINTN index, previous;
    int entry_found = 0;
    int have_previous_load = 0;
    UINT64 previous_address = 0;

    if (size > ELF_MAX_FILE_BYTES)
        return ELF_LIMIT_EXCEEDED;
    if (size < 64 || file[0] != 0x7f || file[1] != 'E' || file[2] != 'L' || file[3] != 'F')
        return ELF_BAD_HEADER;
    if (file[4] != 2 || file[5] != 1 || file[6] != 1 || file[7] != 0 || file[8] != 0 ||
        read16(file + 16) != 2 || read16(file + 18) != 62 || read32(file + 20) != 1 ||
        read32(file + 48) != 0)
        return ELF_UNSUPPORTED_PROFILE;
    if (read16(file + 52) != 64 || read16(file + 54) != 56)
        return ELF_BAD_HEADER;
    table_offset = read64(file + 32);
    header_count = read16(file + 56);
    if (!header_count || header_count == 0xffff || header_count > ELF_MAX_PROGRAM_HEADERS)
        return ELF_LIMIT_EXCEEDED;
    if (table_offset < 64 || !file_range(table_offset, (UINT64)header_count * 56, size))
        return ELF_BAD_TABLE;
    plan->Entry = read64(file + 24);

    // ====== segmenti e permessi ======

    for (index = 0; index < header_count; ++index) {
        const UINT8 *header = file + table_offset + index * 56;
        UINT32 type = read32(header);
        ELF_LOAD_SEGMENT *segment;
        ELF_VALIDATION_RESULT result;
        if (type == 0)
            continue;
        if (type == 4) {
            if (!file_range(read64(header + 8), read64(header + 32), size))
                return ELF_BAD_SEGMENT;
            continue;
        }
        if (type == 0x6474e551) {
            if (read32(header + 4) != 6)
                return ELF_UNSUPPORTED_PROFILE;
            continue;
        }
        if (type != 1)
            return ELF_UNSUPPORTED_PROFILE;
        if (plan->SegmentCount == ELF_MAX_LOAD_SEGMENTS)
            return ELF_LIMIT_EXCEEDED;
        segment = &plan->Segments[plan->SegmentCount];
        result = read_segment(header, size, segment);
        if (result != ELF_VALID)
            return result;
        if (have_previous_load && segment->VirtualAddress < previous_address)
            return ELF_BAD_SEGMENT;
        previous_address = segment->VirtualAddress;
        have_previous_load = 1;
        if (segment->MemoryBytes > ELF_MAX_MEMORY_BYTES - plan->TotalMemoryBytes)
            return ELF_LIMIT_EXCEEDED;
        for (previous = 0; previous < plan->SegmentCount; ++previous)
            if (overlaps_pages(segment, &plan->Segments[previous]))
                return ELF_OVERLAPPING_SEGMENTS;
        plan->TotalMemoryBytes += segment->MemoryBytes;
        ++plan->SegmentCount;
        if ((segment->Flags & 1) && plan->Entry >= segment->VirtualAddress &&
            plan->Entry - segment->VirtualAddress < segment->FileBytes)
            entry_found = 1;
    }
    if (!entry_found)
        return ELF_BAD_ENTRY;
    return ELF_VALID;
}

// ====== interfaccia pubblica e invalidazione piano ======

ELF_VALIDATION_RESULT elf_validate(const UINT8 *file, UINTN size, ELF_LOAD_PLAN *plan)
{
    ELF_VALIDATION_RESULT result;
    if (!plan)
        return ELF_BAD_ARGUMENT;
    plan->Entry = 0;
    plan->TotalMemoryBytes = 0;
    plan->SegmentCount = 0;
    if (!file)
        return ELF_BAD_ARGUMENT;
    result = validate_file(file, size, plan);
    if (result != ELF_VALID) {
        plan->Entry = 0;
        plan->TotalMemoryBytes = 0;
        plan->SegmentCount = 0;
    }
    return result;
}

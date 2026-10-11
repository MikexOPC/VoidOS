#include "kernel/firmware/acpi/madt/topology.h"
static unsigned char table[65536];
static KERNEL_ACPI_DIRECTORY directory;
static KERNEL_MADT_TOPOLOGY output;
static OS_ACPI_U32 used, calls, fail_call, changed, duplicate_table;

// ====== inizializzazione bounded senza runtime C ===== //

static void clear(void *data, OS_ACPI_U32 bytes)
{
    OS_ACPI_U32 index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)data)[index] = 0;
}

// ====== fixture little endian indipendente dal parser ===== //

static void put(unsigned char *data, OS_ACPI_U64 value, OS_ACPI_U32 bytes)
{
    OS_ACPI_U32 index;
    for (index = 0; index < bytes; ++index) data[index] = (unsigned char)(value >> (index * 8));
}

// ====== nuovo record con campi inizializzati dal chiamante ===== //

static unsigned char *record(OS_ACPI_U32 type, OS_ACPI_U32 length)
{
    unsigned char *data = table + used;
    clear(data, length);
    data[0] = (unsigned char)type; data[1] = (unsigned char)length;
    used += length;
    return data;
}

// ====== lunghezza e checksum ricalcolati dopo ogni mutazione semantica ===== //

static void seal(void)
{
    OS_ACPI_U32 index, sum = 0;
    put(table + 4, used, 4); table[9] = 0;
    for (index = 0; index < used; ++index) sum += table[index];
    table[9] = (unsigned char)(0U - sum);
}

// ====== directory con una MADT e due altre tabelle non interpretate ===== //

static void fixture(void)
{
    unsigned char *data;
    clear(table, sizeof(table)); clear(&directory, sizeof(directory)); clear(&output, sizeof(output));
    calls = fail_call = changed = duplicate_table = 0; used = 44;
    directory.Physical = 0x200000; directory.Length = 60; directory.Width = 8; directory.Count = 3;
    directory.Entries[0] = 0x300000; directory.Entries[1] = 0x400000; directory.Entries[2] = 0x500000;
    put(table, 0x43495041, 4); table[8] = 5; put(table + 36, 0xfee00000, 4); table[40] = 1;
    data = record(0, 8); data[2] = 7; data[3] = 2; data[4] = 1;
    data = record(1, 12); data[2] = 3; put(data + 4, 0xfec00000, 4);
    data = record(2, 10); data[3] = 0; data[4] = 2; data[8] = 15;
    data = record(4, 6); data[2] = 255; data[5] = 1;
    seal();
}

// ====== lettore simulato senza privilegi con errori iniettati ===== //

static int read_bytes(void *context, OS_ACPI_U64 physical, OS_ACPI_U32 bytes, unsigned char *data)
{
    OS_ACPI_U32 index;
    if (context != table || !data || !bytes) return 0;
    ++calls;
    if (calls == fail_call) return 0;
    if (physical == 0x300000 || physical == 0x500000) {
        if (bytes != 36) return 0;
        clear(data, bytes);
        put(data, duplicate_table ? 0x43495041 : 0x50434146, 4);
        if (duplicate_table) for (index = 4; index < 36; ++index) data[index] = table[index];
        return 1;
    }
    if (physical < 0x400000 || physical - 0x400000 > used || bytes > used - (physical - 0x400000)) return 0;
    for (index = 0; index < bytes; ++index) data[index] = table[physical - 0x400000 + index];
    if (changed && bytes == 44) data[10] ^= 1;
    return 1;
}

// ====== fallimenti senza pubblicazione parziale ===== //

static int rejected(void)
{
    OS_ACPI_U32 index;
    for (index = 0; index < sizeof(output); ++index) ((volatile unsigned char *)&output)[index] = 0xa5;
    if (kernel_madt_capture(&directory, 52, read_bytes, table, &output)) return 0;
    for (index = 0; index < sizeof(output); ++index) if (((unsigned char *)&output)[index] != 0xa5) return 0;
    return 1;
}

// ====== topologia legacy x2APIC NMI e override LAPIC ===== //

static int success(void)
{
    unsigned char *data;
    fixture();
    if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || !kernel_madt_validate(&output, 52) ||
        output.Physical != 0x400000 || output.Lapic != 0xfee00000 || output.Pcat != 1 ||
        output.CpuCount != 1 || output.Cpus[0].Uid != 7 || output.Cpus[0].Id != 2 ||
        output.IoCount != 1 || output.Io[0].Address != 0xfec00000 || output.IsoCount != 1 ||
        output.Iso[0].Source || output.Iso[0].Gsi != 2 || output.Iso[0].Flags != 15 ||
        output.NmiCount != 1 || output.Nmi[0].Uid != 0xffffffff || output.Nmi[0].Value != 1) return 1;
    fixture(); data = record(9, 16); put(data + 4, 300, 4); data[8] = 2; put(data + 12, 700, 4);
    data = record(5, 12); put(data + 4, 0x1fee00000ULL, 8);
    data = record(3, 8); data[4] = 55;
    data = record(10, 12); put(data + 4, 700, 4); data[8] = 0;
    data = record(255, 3); data[2] = 0xff; seal();
    if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.CpuCount != 2 ||
        output.Cpus[1].Kind != 9 || output.Cpus[1].Flags != 2 || output.Override != 1 ||
        output.Lapic != 0x1fee00000ULL || output.NmiCount != 3 || output.UnknownCount != 1) return 2;
    fixture(); table[48] = 0; table[46] = 255; table[47] = 255; seal();
    if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.CpuCount) return 3;
    fixture(); table[48] = 0xfd; table[40] = 0xfd; table[55] = 0xff; table[72] = 0xcf; seal();
    if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.Cpus[0].Flags != 1 ||
        output.Pcat != 1 || output.Iso[0].Flags != 15) return 4;
    return 0;
}

// ====== checksum e confini dei record malformati ===== //

static int corruption(void)
{
    OS_ACPI_U32 bit, index, baseline;
    fixture(); baseline = used;
    for (bit = 0; bit < baseline * 8; ++bit) {
        fixture(); table[bit / 8] ^= (unsigned char)(1U << (bit % 8));
        if (!rejected()) return 1;
    }
    fixture();
    if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output)) return 2;
    baseline = calls;
    for (index = 1; index <= baseline; ++index) { fixture(); fail_call = index; if (!rejected()) return 3; }
    fixture(); changed = 1; if (!rejected()) return 4;
    for (index = 0; index < 2; ++index) { fixture(); table[45] = (unsigned char)index; seal(); if (!rejected()) return 5; }
    fixture(); table[45] = 255; seal(); if (!rejected()) return 6;
    fixture(); used += 1; seal(); if (!rejected()) return 7;
    fixture(); duplicate_table = 1; if (!rejected()) return 8;
    fixture(); table[0] = 'B'; seal(); if (!rejected()) return 9;
    return 0;
}

// ====== duplicati conflitti GSI e capacita fisse ===== //

static int semantics(void)
{
    OS_ACPI_U32 index;
    unsigned char *data;
    fixture(); data = record(0, 8); data[2] = 8; data[3] = 2; data[4] = 1; seal(); if (!rejected()) return 1;
    fixture(); data = record(0, 8); data[2] = 7; data[3] = 3; data[4] = 1; seal(); if (!rejected()) return 2;
    fixture(); data = record(1, 12); data[2] = 4; put(data + 4, 0xfec00000, 4); data[8] = 24; seal(); if (!rejected()) return 3;
    fixture(); data = record(2, 10); data[4] = 5; seal(); if (!rejected()) return 4;
    fixture(); data = record(3, 8); data[4] = 2; seal(); if (!rejected()) return 5;
    fixture(); table[72] = 2; seal(); if (!rejected()) return 6;
    fixture(); table[66] = 1; seal(); if (!rejected()) return 7;
    fixture(); data = record(5, 12); put(data + 4, 0xfee00000, 8);
    data = record(5, 12); put(data + 4, 0xfee00000, 8); seal(); if (!rejected()) return 8;
    fixture(); used = 44;
    for (index = 0; index < 64; ++index) { data = record(0, 8); data[2] = data[3] = (unsigned char)index; data[4] = 1; }
    seal(); if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.CpuCount != 64) return 9;
    data = record(0, 8); data[2] = data[3] = 64; data[4] = 1; seal(); if (!rejected()) return 10;
    return 0;
}

// ====== fotografia persistente e argomenti invalidi ===== //

static int snapshot(void)
{
    fixture();
    if (kernel_madt_capture(0, 52, read_bytes, table, &output) ||
        kernel_madt_capture(&directory, 52, 0, table, &output) ||
        kernel_madt_capture(&directory, 52, read_bytes, table, 0) ||
        kernel_madt_capture(&directory, 52, read_bytes, table, (KERNEL_MADT_TOPOLOGY *)(void *)&directory) || calls) return 1;
    if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output)) return 2;
    clear(table, sizeof(table)); if (!kernel_madt_validate(&output, 52)) return 3;
    output.Cpus[63].Uid = 1; if (kernel_madt_validate(&output, 52)) return 4;
    output.Cpus[63].Uid = 0; output.Reserved[0] = 1; if (kernel_madt_validate(&output, 52)) return 5;
    return 0;
}

// ====== capacita di I O APIC override ISA NMI e record non supportati ===== //

static int capacities(void)
{
    OS_ACPI_U32 index;
    unsigned char *data;
    fixture(); used = 44;
    for (index = 0; index < 17; ++index) {
        data = record(1, 12); data[2] = (unsigned char)index;
        put(data + 4, 0xfec00000 + index * 4096, 4); put(data + 8, index * 24, 4); seal();
        if (index < 16) {
            if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.IoCount != index + 1) return 1;
        } else if (!rejected()) return 2;
    }
    fixture(); used = 44;
    for (index = 0; index < 16; ++index) { data = record(2, 10); data[3] = (unsigned char)index; data[4] = (unsigned char)index; }
    seal(); if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.IsoCount != 16) return 3;
    data = record(2, 10); data[3] = 16; seal(); if (!rejected()) return 4;
    fixture(); used = 44;
    for (index = 0; index < 65; ++index) {
        data = record(3, 8); put(data + 4, index + 16, 4); seal();
        if (index < 64) {
            if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.NmiCount != index + 1) return 5;
        } else if (!rejected()) return 6;
    }
    fixture(); used = 44;
    for (index = 0; index < 1024; ++index) record(128, 2);
    seal(); if (!kernel_madt_capture(&directory, 52, read_bytes, table, &output) || output.UnknownCount != 1024) return 7;
    record(128, 2); seal(); if (!rejected()) return 8;
    fixture(); data = record(4, 6); data[2] = 7; data[5] = 1; seal(); if (!rejected()) return 9;
    fixture(); put(table + 56, 0x400000, 4); seal(); if (!rejected()) return 10;
    fixture(); clear(&output, sizeof(output)); output.CpuCount = 65;
    if (kernel_madt_append(&output, table + 44, 8, 52)) return 11;
    clear(&output, sizeof(output));
    if (kernel_madt_append(&output, (const unsigned char *)&output, 8, 52)) return 12;
    return 0;
}

// ====== suite completa senza istruzioni privilegiate ===== //

__declspec(dllexport) int run_madt_tests(void)
{
    int result = success(); if (result) return 100 + result;
    result = corruption(); if (result) return 200 + result;
    result = semantics(); if (result) return 300 + result;
    result = snapshot(); if (result) return 400 + result;
    result = capacities(); if (result) return 500 + result;
    return 0;
}

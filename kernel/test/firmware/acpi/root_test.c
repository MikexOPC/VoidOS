#include "kernel/firmware/acpi/root.h"
static unsigned char table[36 + 257 * 8];
static OS_ACPI_ROOT root;
static KERNEL_ACPI_DIRECTORY output;
static unsigned int calls, fail_call, change_header;

// ====== scritture little endian indipendenti dal parser ===== //

static void put(unsigned char *bytes, OS_ACPI_U64 value, unsigned int count)
{
    unsigned int index;
    for (index = 0; index < count; ++index) bytes[index] = (unsigned char)(value >> (index * 8));
}

// ====== azzeramento bounded senza dipendenze CRT ===== //

static void clear(void *data, unsigned int bytes)
{
    unsigned int index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)data)[index] = 0;
}

// ====== checksum dell intera tabella costruito dal test ===== //

static void checksum(unsigned int length)
{
    unsigned int index, sum = 0;
    table[9] = 0;
    for (index = 0; index < length; ++index) sum += table[index];
    table[9] = (unsigned char)(0U - sum);
}

// ====== fixture legacy o moderna con indirizzi non allineati ===== //

static void fixture(unsigned int width, unsigned int count)
{
    unsigned int index, sum = 0;
    clear(&root, sizeof(root)); clear(&output, sizeof(output)); clear(table, sizeof(table));
    calls = fail_call = change_header = 0;
    root.Physical = 0x100000; root.Source = width == 8 ? 2 : 1; root.Length = width == 8 ? 36 : 20;
    put(root.Bytes, 0x2052545020445352ULL, 8);
    if (width == 8) {
        root.Bytes[15] = 2; put(root.Bytes + 20, 36, 4); put(root.Bytes + 24, 0x200003, 8);
    } else put(root.Bytes + 16, 0x200003, 4);
    for (index = 0; index < 20; ++index) sum += root.Bytes[index];
    root.Bytes[8] = (unsigned char)(0U - sum); sum = 0;
    if (width == 8) {
        for (index = 0; index < 36; ++index) sum += root.Bytes[index];
        root.Bytes[32] = (unsigned char)(0U - sum);
    }
    put(table, width == 8 ? 0x54445358 : 0x54445352, 4);
    put(table + 4, 36 + count * width, 4); table[8] = 1;
    for (index = 0; index < count; ++index) {
        put(table + 36 + index * width, 0x300007 + index * 4096, width);
    }
    checksum(36 + count * width);
}

// ====== lettore bounded con errori e variazione tra header e copia ===== //

static int read_table(void *context, OS_ACPI_U64 physical, OS_ACPI_U32 bytes, unsigned char *data)
{
    unsigned int index;
    if (context != table || physical != 0x200003 || !data || bytes > sizeof(table)) {
        return 0;
    }
    ++calls;
    if (calls == fail_call) {
        return 0;
    }
    for (index = 0; index < bytes; ++index) {
        data[index] = table[index];
    }
    if (calls == 2 && change_header) data[10] ^= 1;
    return 1;
}

// ====== output precedente conservato dopo ogni fallimento ===== //

static int rejected(void)
{
    unsigned int index;
    for (index = 0; index < sizeof(output); ++index) ((volatile unsigned char *)&output)[index] = 0xa5;
    if (kernel_acpi_root_capture(&root, 52, read_table, table, &output)) return 0;
    for (index = 0; index < sizeof(output); ++index) {
        if (((unsigned char *)&output)[index] != 0xa5) return 0;
    }
    return 1;
}

// ====== tutte le capacita legacy e moderne con coda inizializzata ===== //

static int capacities(void)
{
    unsigned int width, count, index;
    for (width = 4; width <= 8; width += 4) {
        for (count = 0; count <= 256; ++count) {
            fixture(width, count);
            if (!kernel_acpi_root_capture(&root, 52, read_table, table, &output) || calls != 2 ||
                output.Physical != 0x200003 || output.Count != count || output.Width != width ||
                output.Length != 36 + width * count || output.Reserved) return 1;
            for (index = 0; index < 256; ++index) {
                OS_ACPI_U64 expected = index < count ? 0x300007 + index * 4096 : 0;
                if (output.Entries[index] != expected) return 2;
            }
        }
        fixture(width, 257);
        if (!rejected() || calls != 1) return 3;
    }
    return 0;
}

// ====== ogni bit del contenuto e doppia lettura incoerente respinti ===== //

static int corruption(void)
{
    unsigned int bit;
    for (bit = 0; bit < 52 * 8; ++bit) {
        fixture(8, 2); table[bit / 8] ^= (unsigned char)(1U << (bit % 8));
        if (!rejected()) return 1;
    }
    fixture(8, 2); fail_call = 1;
    if (!rejected() || calls != 1) return 2;
    fixture(8, 2); fail_call = 2;
    if (!rejected() || calls != 2) return 3;
    fixture(8, 2); change_header = 1;
    if (!rejected()) return 4;
    fixture(8, 2); table[8] = 2; checksum(52);
    if (!rejected()) return 5;
    fixture(8, 2); put(table + 4, 0xffffffff, 4);
    if (!rejected() || calls != 1) return 6;
    return 0;
}

// ====== puntatori numerici limiti CPU duplicati e riferimenti alla radice ===== //

static int pointers(void)
{
    const OS_ACPI_U64 invalid[] = {0, 4095, 0x200003, 0x200004, (1ULL << 52) - 35, 1ULL << 52, ~0ULL};
    unsigned int index;
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        fixture(8, 2); put(table + 36, invalid[index], 8); checksum(52);
        if (!rejected()) return 1;
    }
    fixture(8, 2); put(table + 44, 0x300007, 8); checksum(52);
    if (!rejected()) return 2;
    fixture(8, 1); put(table + 36, (1ULL << 32) - 35, 8); checksum(44);
    if (kernel_acpi_root_capture(&root, 32, read_table, table, &output)) return 3;
    fixture(8, 1); clear(&root, sizeof(root));
    if (!rejected() || calls) return 4;
    return 0;
}

// ====== alias output root e argomenti nulli senza pubblicazione ===== //

static int aliases(void)
{
    fixture(8, 2);
    if (kernel_acpi_root_capture(0, 52, read_table, table, &output) ||
        kernel_acpi_root_capture(&root, 52, 0, table, &output) ||
        kernel_acpi_root_capture(&root, 52, read_table, table, 0) ||
        kernel_acpi_root_capture(&root, 31, read_table, table, &output) ||
        kernel_acpi_root_capture(&root, 52, read_table, table, (KERNEL_ACPI_DIRECTORY *)(void *)&root) || calls) return 1;
    return 0;
}

// ====== suite senza firmware o istruzioni privilegiate ===== //

static int persistent_corruption(void)
{
    unsigned int index;
    fixture(8, 2);
    if (!kernel_acpi_root_capture(&root, 52, read_table, table, &output) ||
        !kernel_acpi_directory_valid(&output, 52)) return 1;
    output.Reserved = 1;
    if (kernel_acpi_directory_valid(&output, 52)) return 2;
    output.Reserved = 0;
    output.Count = 257;
    if (kernel_acpi_directory_valid(&output, 52)) return 3;
    output.Count = 2;
    for (index = 2; index < 256; ++index) {
        output.Entries[index] = 4096;
        if (kernel_acpi_directory_valid(&output, 52)) return 4;
        output.Entries[index] = 0;
    }
    output.Width = 4;
    if (kernel_acpi_directory_valid(&output, 52)) return 5;
    return 0;
}

// ====== regressione del parser e dei metadati posseduti dal kernel ===== //

__declspec(dllexport) int run_acpi_root_tests(void)
{
    int result = capacities(); if (result) return 100 + result;
    result = corruption(); if (result) return 200 + result;
    result = pointers(); if (result) return 300 + result;
    result = aliases(); if (result) return 400 + result;
    result = persistent_corruption(); if (result) return 500 + result;
    return 0;
}

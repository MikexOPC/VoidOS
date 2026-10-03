#include "rsdp.h"

// ====== lettura bounded little endian della sola copia locale del RSDP ===== //

static OS_ACPI_U64 read_number(const unsigned char *bytes, OS_ACPI_U32 count)
{
    OS_ACPI_U64 result = 0;
    OS_ACPI_U32 index;
    for (index = 0; index < count; ++index) result |= (OS_ACPI_U64)bytes[index] << (index * 8);
    return result;
}

// ====== validazione della copia senza dereferenziare alcun indirizzo fisico ===== //

int os_acpi_root_valid(const OS_ACPI_ROOT *root, OS_ACPI_U32 bits)
{
    OS_ACPI_U32 index, legacy_sum = 0, extended_sum = 0;
    OS_ACPI_U64 xsdt = 0;
    if (!root || bits < 32 || bits > 52 || root->Reserved) return 0;
    if (!root->Source) {
        if (root->Physical || root->Length) return 0;
        for (index = 0; index < 36; ++index) if (root->Bytes[index]) return 0;
        return 1;
    }
    if (root->Physical < 4096 || root->Physical >= (1ULL << bits) ||
        (root->Length != 20 && root->Length != 36) || root->Length > (1ULL << bits) - root->Physical) return 0;
    for (index = 0; index < 36; ++index) {
        if (index < 20) legacy_sum += root->Bytes[index];
        if (index < root->Length) extended_sum += root->Bytes[index];
        else if (root->Bytes[index]) return 0;
    }
    if (root->Length == 36) {
        if (read_number(root->Bytes + 20, 4) != 36) return 0;
        xsdt = read_number(root->Bytes + 24, 8);
    }
    return os_acpi_rsdp_fields(read_number(root->Bytes, 8), root->Bytes[15], root->Length,
        legacy_sum & 255, extended_sum & 255, read_number(root->Bytes + 16, 4), xsdt, root->Source, bits) != 0;
}

// ====== indirizzo da mappare in futuro ottenuto solo da una copia verificata ===== //

OS_ACPI_U64 os_acpi_root_table(const OS_ACPI_ROOT *root, OS_ACPI_U32 bits)
{
    if (!os_acpi_root_valid(root, bits) || !root->Source) return 0;
    return root->Length == 36 ? read_number(root->Bytes + 24, 8) : read_number(root->Bytes + 16, 4);
}

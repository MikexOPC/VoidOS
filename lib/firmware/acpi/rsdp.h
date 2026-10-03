#ifndef OS_ACPI_RSDP_H
#define OS_ACPI_RSDP_H
typedef unsigned long long OS_ACPI_U64;
typedef unsigned int OS_ACPI_U32;
typedef struct {
    OS_ACPI_U64 Physical;
    OS_ACPI_U32 Length, Source;
    unsigned char Bytes[36];
    OS_ACPI_U32 Reserved;
} OS_ACPI_ROOT;
_Static_assert(sizeof(OS_ACPI_ROOT) == 56, "Copia RSDP senza puntatori firmware");
OS_ACPI_U32 os_acpi_rsdp_fields(OS_ACPI_U64 signature, OS_ACPI_U32 revision, OS_ACPI_U32 length,
                            OS_ACPI_U32 legacy_sum, OS_ACPI_U32 extended_sum, OS_ACPI_U64 rsdt,
                            OS_ACPI_U64 xsdt, OS_ACPI_U32 source, OS_ACPI_U32 bits);
int os_acpi_root_valid(const OS_ACPI_ROOT *root, OS_ACPI_U32 bits);
OS_ACPI_U64 os_acpi_root_table(const OS_ACPI_ROOT *root, OS_ACPI_U32 bits);
#endif

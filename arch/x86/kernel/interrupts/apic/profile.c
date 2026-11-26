#include "profile.h"
#include "lib/memory/address_ranges.h"
#include "kernel/diagnostics/log.h"

// ====== intervalli delle strutture tipizzate prima di dereferenziare gli input ===== //

static int inputs_valid(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_RAW *raw)
{
    BOOT_U64 first = (BOOT_U64)topology, second = (BOOT_U64)raw;
    if (!first || !second || (first & 7) || (second & 7)) {
        return 0;
    }
    return first <= ~0ULL - sizeof(*topology) && second <= ~0ULL - sizeof(*raw);
}

// ====== buffer output distinto dagli input prima di ogni pubblicazione ===== //

static int target_valid(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_RAW *raw, X86_APIC_PROFILE *output)
{
    BOOT_U64 target = (BOOT_U64)output;
    if (!inputs_valid(topology, raw) || !output || (target & 7) || target > ~0ULL - sizeof(*output)) {
        return 0;
    }
    return !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)topology, sizeof(*topology)) &&
           !os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)raw, sizeof(*raw));
}

// ====== feature bit raw e IF verificati indipendentemente dal backend ===== //

static int raw_valid(const X86_APIC_RAW *raw, BOOT_U32 bits)
{
    if (!raw || raw->MaxLeaf < 1 || raw->PhysicalBits != bits || raw->Reserved ||
        !(raw->Rflags & 2) || (raw->Rflags & (1ULL << 9))) {
        return 0;
    }
    BOOT_U32 mode = x86_apic_mode(raw->ApicBase, raw->Leaf1Ecx, raw->Leaf1Edx, bits);
    return mode == 1 || mode == 2;
}

// ====== matching del solo Initial APIC ID senza assumere CPU zero ===== //

static int find_bsp(const KERNEL_MADT_TOPOLOGY *topology, BOOT_U32 id, BOOT_U32 *uid)
{
    BOOT_U32 index, matches = 0;
    for (index = 0; index < topology->CpuCount; ++index) {
        const KERNEL_MADT_CPU *cpu = &topology->Cpus[index];
        if (cpu->Id != id) {
            continue;
        }
        if (cpu->Flags != 1 || ++matches != 1) {
            return 0;
        }
        *uid = cpu->Uid;
    }
    return matches == 1;
}

// ====== copia bounded della bozza senza memcpy introdotta dal compilatore ===== //

static void publish(const X86_APIC_PROFILE *draft, X86_APIC_PROFILE *output)
{
    BOOT_U32 index;
    for (index = 0; index < sizeof(*output); ++index) {
        ((volatile unsigned char *)output)[index] = ((const unsigned char *)draft)[index];
    }
}

// ====== piano puro APIC e PAT nessun accesso ai registri del controller ===== //

int x86_apic_plan(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_RAW *raw,
                   BOOT_U32 bits, X86_APIC_PROFILE *output)
{
    X86_APIC_PROFILE draft;
    BOOT_U32 index, slot;
    if (!target_valid(topology, raw, output) || !raw_valid(raw, bits) ||
        !kernel_madt_validate(topology, bits) || topology->UnknownCount || !topology->IoCount) {
        return 0;
    }
    for (index = 0; index < sizeof(draft); ++index) {
        ((volatile unsigned char *)&draft)[index] = 0;
    }
    draft.Lapic = raw->ApicBase & 0x0000000ffffff000ULL;
    draft.InitialId = raw->Leaf1Ebx >> 24;
    slot = x86_apic_uc_slot(raw->Pat);
    if (!slot || draft.Lapic != topology->Lapic || !find_bsp(topology, draft.InitialId, &draft.Uid)) {
        return 0;
    }
    for (index = 0; index < sizeof(*raw); ++index) {
        ((unsigned char *)&draft.Raw)[index] = ((const unsigned char *)raw)[index];
    }
    draft.UcIndex = slot - 1;
    draft.CacheBits = x86_apic_cache_bits(draft.UcIndex);
    draft.Ready = 1;
    publish(&draft, output);
    return 1;
}

// ====== ogni campo derivato ricostruito prima di futuri mapping MMIO ===== //

int x86_apic_profile_valid(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, BOOT_U32 bits)
{
    X86_APIC_PROFILE expected;
    BOOT_U32 index;
    if (!profile || ((BOOT_U64)profile & 7) || (BOOT_U64)profile > ~0ULL - sizeof(*profile) ||
        !topology || os_address_ranges_overlap((BOOT_U64)profile, sizeof(*profile), (BOOT_U64)topology, sizeof(*topology)) ||
        profile->Ready != 1 || !x86_apic_plan(topology, &profile->Raw, bits, &expected)) {
        return 0;
    }
    for (index = 0; index < sizeof(expected); ++index) {
        if (((const unsigned char *)profile)[index] != ((const unsigned char *)&expected)[index]) {
            return 0;
        }
    }
    return 1;
}

// ====== registri stabili fra due catture salvo flag aritmetici transitori ===== //

static int samples_match(const X86_APIC_RAW *first, const X86_APIC_RAW *second)
{
    return first->ApicBase == second->ApicBase && first->Pat == second->Pat &&
           first->Leaf1Ebx == second->Leaf1Ebx && first->Leaf1Ecx == second->Leaf1Ecx &&
           first->Leaf1Edx == second->Leaf1Edx && first->MaxLeaf == second->MaxLeaf &&
           first->PhysicalBits == second->PhysicalBits && first->Reserved == second->Reserved &&
           ((first->Rflags ^ second->Rflags) & (1ULL << 9)) == 0;
}

// ====== nessun byte indeterminato se un backend interrompe la cattura ===== //

static void clear_sample(X86_APIC_RAW *sample)
{
    BOOT_U32 index;
    for (index = 0; index < sizeof(*sample); ++index) {
        ((volatile unsigned char *)sample)[index] = 0;
    }
}

// ====== logging del solo profilo CPU verificato non di controller attivati ===== //

static int log_profile(const X86_APIC_PROFILE *profile)
{
    if (!kernel_section("preflight BSP APIC e cache policy") ||
        !kernel_text("KERNEL_APIC_PROFILE: ") || !kernel_number((BOOT_U64)profile) ||
        !kernel_text("\nKERNEL_APIC_BASE_MSR: ") || !kernel_number(profile->Raw.ApicBase) ||
        !kernel_text("\nKERNEL_APIC_PAT: ") || !kernel_number(profile->Raw.Pat) ||
        !kernel_text("\nKERNEL_APIC_INITIAL_ID: ") || !kernel_number(profile->InitialId) ||
        !kernel_text("\nKERNEL_APIC_BSP_UID: ") || !kernel_number(profile->Uid) ||
        !kernel_text("\nKERNEL_APIC_UC_INDEX: ") || !kernel_number(profile->UcIndex) ||
        !kernel_text("\nKERNEL_APIC_CACHE_BITS: ") || !kernel_number(profile->CacheBits) ||
        !kernel_text("\n")) {
        return 0;
    }
    return kernel_log(0, "KERNEL_APIC_PREFLIGHT_OK");
}

// ====== durata degli snapshot raw confinata alla verifica della bozza ===== //

static int capture_consistent(const KERNEL_MADT_TOPOLOGY *topology, BOOT_U32 bits, X86_APIC_PROFILE *draft)
{
    X86_APIC_RAW first, second;
    clear_sample(&first);
    clear_sample(&second);
    if (!x86_apic_read_profile(&first) || !x86_apic_plan(topology, &first, bits, draft)) {
        return 0;
    }
    if (!x86_apic_read_profile(&second) || !raw_valid(&second, bits) || !samples_match(&first, &second)) {
        return 0;
    }
    return x86_apic_profile_valid(topology, draft, bits);
}

// ====== preflight conservato solo dopo due catture CPU coerenti ===== //

int x86_apic_initialize(const KERNEL_MADT_TOPOLOGY *topology, BOOT_U32 bits, X86_APIC_PROFILE *output)
{
    X86_APIC_PROFILE draft;
    BOOT_U32 index;
    if (!topology || ((BOOT_U64)topology & 7) || (BOOT_U64)topology > ~0ULL - sizeof(*topology) ||
        !output || ((BOOT_U64)output & 7) ||
        (BOOT_U64)output > ~0ULL - sizeof(*output) ||
        os_address_ranges_overlap((BOOT_U64)output, sizeof(*output), (BOOT_U64)topology, sizeof(*topology))) {
        return 0;
    }
    for (index = 0; index < sizeof(*output); ++index) {
        if (((const unsigned char *)output)[index]) {
            return 0;
        }
    }
    if (!kernel_madt_validate(topology, bits) ||
        topology->UnknownCount ||
        !topology->IoCount) {
        return 0;
    }

    if (!capture_consistent(topology, bits, &draft)) {
        return 0;
    }
    publish(&draft, output);
    return x86_apic_profile_valid(topology, output, bits) && log_profile(output);
}

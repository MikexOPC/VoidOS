#include "arch/x86/kernel/interrupts/apic/profile.h"
#include "arch/x86/kernel/platform.h"
void mock_serial_mode(unsigned int mode);

static KERNEL_MADT_TOPOLOGY topology;
static X86_APIC_RAW raw;
static X86_APIC_PROFILE output;
static unsigned int capture_count, capture_failure, capture_change;

// ====== buffer validi espliciti senza dipendenze CRT ===== //

static void clear(void *target, unsigned int bytes)
{
    unsigned int index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)target)[index] = 0;
}

// ====== snapshot CPU simulato senza eseguire RDMSR sul sistema host ===== //

int x86_apic_read_profile(X86_APIC_RAW *target)
{
    unsigned int index;
    ++capture_count;
    if (capture_failure == capture_count) return 0;
    for (index = 0; index < sizeof(raw); ++index) {
        ((unsigned char *)target)[index] = ((const unsigned char *)&raw)[index];
    }
    if (capture_change && capture_count == 2) {
        if (capture_change == 1) target->Pat ^= 1ULL << 8;
        else if (capture_change == 2) target->Rflags |= 1ULL << 9;
        else target->Rflags |= 1ULL << 6;
    }
    return 1;
}

// ====== BSP non coincidente con il primo record MADT ===== //

static void fixture(void)
{
    clear(&topology, sizeof(topology));
    clear(&raw, sizeof(raw));
    clear(&output, sizeof(output));
    capture_count = capture_failure = capture_change = 0;
    topology.Physical = 0x400000;
    topology.Lapic = 0xfee00000;
    topology.Length = 72;
    topology.Revision = 5;
    topology.Ready = 1;
    topology.CpuCount = 2;
    topology.IoCount = 1;
    topology.Cpus[0].Uid = 40;
    topology.Cpus[0].Id = 3;
    topology.Cpus[0].Flags = 1;
    topology.Cpus[1].Uid = 99;
    topology.Cpus[1].Id = 7;
    topology.Cpus[1].Flags = 1;
    topology.Io[0].Address = 0xfec00000;
    raw.ApicBase = 0xfee00900;
    raw.Pat = 0x0007040600070406ULL;
    raw.Rflags = 2;
    raw.Leaf1Ebx = 7U << 24;
    raw.Leaf1Edx = (1U << 5) | (1U << 9) | (1U << 16);
    raw.MaxLeaf = 1;
    raw.PhysicalBits = 36;
}

// ====== fallimenti del piano non pubblicano byte parziali ===== //

static int rejected(void)
{
    unsigned int index;
    for (index = 0; index < sizeof(output); ++index) ((volatile unsigned char *)&output)[index] = 0xa5;
    if (x86_apic_plan(&topology, &raw, 36, &output)) return 0;
    for (index = 0; index < sizeof(output); ++index) {
        if (((const unsigned char *)&output)[index] != 0xa5) return 0;
    }
    return 1;
}

// ====== identita distinta da UID indice e CPU soltanto online capable ===== //

static int identities(void)
{
    fixture();
    if (!x86_apic_plan(&topology, &raw, 36, &output) || !x86_apic_profile_valid(&topology, &output, 36) ||
        output.Uid != 99 || output.InitialId != 7 || output.UcIndex != 3 || output.CacheBits != 0x18 ||
        output.Lapic != 0xfee00000 || output.Ready != 1) return 1;
    topology.Cpus[1].Flags = 2;
    if (!rejected()) return 2;
    fixture(); raw.Leaf1Ebx = 9U << 24;
    if (!rejected()) return 3;
    fixture(); topology.Lapic = 0xfed00000;
    if (!rejected()) return 4;
    fixture(); topology.UnknownCount = 1; topology.Length += 2;
    if (!rejected()) return 5;
    fixture(); topology.IoCount = 0;
    clear(&topology.Io[0], sizeof(topology.Io[0]));
    if (!rejected()) return 6;
    fixture(); topology.Cpus[1].Kind = 9; topology.Length += 8;
    if (!x86_apic_plan(&topology, &raw, 36, &output)) return 7;
    fixture(); topology.Cpus[1].Id = 300; topology.Cpus[1].Kind = 9; topology.Length += 8;
    if (!rejected()) return 8;
    return 0;
}

// ====== ogni precondizione privilegiata verificata anche nel piano puro ===== //

static int cpu_state(void)
{
    unsigned int bit;
    for (bit = 0; bit < 3; ++bit) {
        const unsigned int features[] = {5, 9, 16};
        fixture(); raw.Leaf1Edx &= ~(1U << features[bit]);
        if (!rejected()) return 1;
    }
    fixture(); raw.Rflags |= 1ULL << 9;
    if (!rejected()) return 2;
    fixture(); raw.Rflags = 0;
    if (!rejected()) return 3;
    fixture(); raw.MaxLeaf = 0;
    if (!rejected()) return 4;
    fixture(); raw.PhysicalBits = 35;
    if (!rejected()) return 5;
    fixture(); raw.Reserved = 1;
    if (!rejected()) return 6;
    fixture(); raw.ApicBase &= ~(1ULL << 8);
    if (!rejected()) return 7;
    fixture(); raw.ApicBase &= ~(1ULL << 11);
    if (!rejected()) return 8;
    fixture(); raw.ApicBase |= 1ULL << 10; raw.Leaf1Ecx |= 1U << 21;
    if (!rejected()) return 9;
    for (bit = 0; bit < 64; ++bit) {
        if ((bit >= 12 && bit < 36) || bit == 8 || bit == 10 || bit == 11) continue;
        fixture(); raw.ApicBase |= 1ULL << bit;
        if (!rejected()) return 10;
    }
    return 0;
}

// ====== PAT corrente verificato senza supporre il layout di reset ===== //

static int cache_policy(void)
{
    unsigned int slot;
    for (slot = 1; slot < 8; ++slot) {
        fixture(); raw.Pat = 0x0606060606060606ULL & ~(255ULL << (slot * 8));
        if (!x86_apic_plan(&topology, &raw, 36, &output) || output.UcIndex != slot ||
            output.CacheBits != x86_apic_cache_bits(slot)) return 1;
    }
    fixture(); raw.Pat = 0x0707070707070706ULL;
    if (!rejected()) return 2;
    fixture(); raw.Pat &= ~255ULL;
    if (!rejected()) return 3;
    fixture(); raw.Pat |= 2ULL << 24;
    if (!rejected()) return 4;
    fixture(); raw.Pat |= 128ULL << 32;
    if (!rejected()) return 5;
    return 0;
}

// ====== corruzioni del profilo conservato e alias fra input output ===== //

static int ownership(void)
{
    unsigned int index;
    fixture();
    if (x86_apic_plan(&topology, &raw, 36, (X86_APIC_PROFILE *)&topology) ||
        x86_apic_plan(&topology, &raw, 36, (X86_APIC_PROFILE *)&raw) ||
        x86_apic_plan(0, &raw, 36, &output) || x86_apic_plan(&topology, 0, 36, &output)) return 1;
    if (!x86_apic_plan(&topology, &raw, 36, &output)) return 2;
    for (index = 48; index < sizeof(output); ++index) {
        ((unsigned char *)&output)[index] ^= 128;
        if (x86_apic_profile_valid(&topology, &output, 36)) return 3;
        ((unsigned char *)&output)[index] ^= 128;
    }
    return 0;
}

// ====== doppia lettura coerente prima della pubblicazione runtime ===== //

static int runtime_state(void)
{
    fixture(); capture_failure = 1;
    if (x86_apic_initialize(&topology, 36, &output) || output.Ready) return 1;
    fixture(); capture_failure = 2;
    if (x86_apic_initialize(&topology, 36, &output) || output.Ready) return 2;
    fixture(); capture_change = 1;
    if (x86_apic_initialize(&topology, 36, &output) || output.Ready) return 3;
    fixture(); output.Ready = 1;
    if (x86_apic_initialize(&topology, 36, &output) || capture_count) return 4;
    fixture(); capture_change = 2;
    if (x86_apic_initialize(&topology, 36, &output) || output.Ready) return 5;
    fixture(); capture_change = 3; mock_serial_mode(0);
    if (!x86_serial_initialize() || !x86_apic_initialize(&topology, 36, &output) ||
        !x86_apic_profile_valid(&topology, &output, 36) || capture_count != 2) return 6;
    if (x86_apic_initialize(&topology, 36, &output) || capture_count != 2) return 7;
    fixture();
    if (x86_apic_initialize(&topology, 36, (X86_APIC_PROFILE *)&topology) || capture_count) return 8;
    fixture(); topology.Ready = 0;
    if (x86_apic_initialize(&topology, 36, &output) || capture_count || output.Ready) return 9;
    fixture(); topology.UnknownCount = 1; topology.Length += 2;
    if (x86_apic_initialize(&topology, 36, &output) || capture_count || output.Ready) return 10;
    fixture();
    if (x86_apic_plan(&topology, &raw, 36, (X86_APIC_PROFILE *)((unsigned char *)&output + 1)) ||
        x86_apic_plan(&topology, &raw, 36, (X86_APIC_PROFILE *)(~0ULL - 7))) return 11;
    return 0;
}

// ====== suite APIC host senza accessi MMIO o istruzioni privilegiate ===== //

__declspec(dllexport) int run_apic_profile_tests(void)
{
    int result = identities(); if (result) return 100 + result;
    result = cpu_state(); if (result) return 200 + result;
    result = cache_policy(); if (result) return 300 + result;
    result = ownership(); if (result) return 400 + result;
    result = runtime_state(); if (result) return 500 + result;
    return 0;
}

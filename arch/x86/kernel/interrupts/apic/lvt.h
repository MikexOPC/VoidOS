#ifndef OS_X86_APIC_LVT_H
#define OS_X86_APIC_LVT_H

// Intel SDM 253668-078US §11.5.1 Figure 11-8: LVT vector[7:0], mask bit 16,
// timer mode[18:17] with 01b periodic. Vector 0x40 is the masked placeholder.
#define X86_LVT_MASK            0x10000U
#define X86_LVT_TIMER_PERIODIC  0x20000U
#define X86_LVT_MASKED_IDLE     (X86_LVT_MASK | 0x40U)
#define X86_TIMER_VECTOR        0x41U

// Intel SDM 253668-078US §11.8.3.1: priority class is vector[7:4]; class Dh is above
// the device vectors 40h-43h. The same vector serves the preemption timer on every CPU.
#define X86_PREEMPT_VECTOR      0xD1U

#endif

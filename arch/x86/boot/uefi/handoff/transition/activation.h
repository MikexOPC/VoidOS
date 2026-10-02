#ifndef OS_TRANSITION_ACTIVATION_H
#define OS_TRANSITION_ACTIVATION_H
#include "handoff/firmware/terminal/handoff.h"

#define BOOT_ACTIVATION_OFFSET 640U
#define BOOT_ACTIVATION_CONTEXT_OFFSET 1024U

// ====== soli valori stabili dopo l'ultimo aggiornamento della mappa ====== //

typedef struct {
    UINT64 Root, KernelEntry, StackTop, InfoPhysical, InfoVirtual, Gdtr, Idtr;
} BOOT_ACTIVATION_CONTEXT;
_Static_assert(sizeof(BOOT_ACTIVATION_CONTEXT) == 56, "Operandi assembly x64");
_Static_assert(BOOT_ACTIVATION_CONTEXT_OFFSET >= sizeof(BOOT_DESCRIPTOR_BUNDLE) &&
               BOOT_ACTIVATION_CONTEXT_OFFSET + sizeof(BOOT_ACTIVATION_CONTEXT) <= 4096,
               "Contesto separato dai descrittori nella stessa pagina posseduta");
UINT32 boot_activation_check_state(UINT64 cr4, UINT64 root, UINT32 physical_bits);
EFI_STATUS boot_prepare_activation(const BOOT_EXIT_HANDOFF *handoff);
__declspec(noreturn) void x86_enter_transition(UINT64 context, UINT64 code);
__declspec(noreturn) void x86_stop_transition(void);
#endif

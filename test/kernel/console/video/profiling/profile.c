#include "profile.h"
#include "../../../../../kernel/console/video/measurement/budget.h"

// ====== kernel_console_profile_init ======

int kernel_console_profile_init(KERNEL_CONSOLE_PROFILE *owner, uint32_t mask)
{
    uint32_t index;
    if (!owner || (uintptr_t)owner % _Alignof(KERNEL_CONSOLE_PROFILE) ||
        (mask != 0xffffff && mask != UINT32_MAX)) return 0;
    owner->Self = (uintptr_t)owner;
    owner->Mask = mask; owner->Next = owner->Failed = 0;
    owner->Cells = owner->Total = 0;
    for (index = 0; index < KERNEL_CONSOLE_PROFILE_PHASES; ++index)
        owner->Sum[index] = owner->Max[index] = 0;
    for (index = 0; index <= KERNEL_CONSOLE_PROFILE_PHASES; ++index) owner->Stamp[index] = 0;
    return 1;
}

// ====== kernel_console_profile_mark ======

int kernel_console_profile_mark(KERNEL_CONSOLE_PROFILE *owner, uint32_t phase, uint32_t stamp)
{
    uint32_t index, delta[KERNEL_CONSOLE_PROFILE_PHASES], total;
    uint64_t sum = 0;
    if (!owner || (uintptr_t)owner % _Alignof(KERNEL_CONSOLE_PROFILE) ||
        owner->Self != (uintptr_t)owner || owner->Failed) return 0;
    if (phase != owner->Next || phase > KERNEL_CONSOLE_PROFILE_PHASES ||
        (owner->Mask != 0xffffff && owner->Mask != UINT32_MAX) || (stamp & ~owner->Mask)) {
        owner->Failed = 1; return 0;
    }
    owner->Stamp[phase] = stamp;
    if (phase < KERNEL_CONSOLE_PROFILE_PHASES) { ++owner->Next; return 1; }
    total = (stamp - owner->Stamp[0]) & owner->Mask;
    for (index = 0; index < KERNEL_CONSOLE_PROFILE_PHASES; ++index) {
        delta[index] = (owner->Stamp[index + 1] - owner->Stamp[index]) & owner->Mask;
        sum += delta[index];
        if (owner->Sum[index] > UINT64_MAX - delta[index]) break;
    }
    if (index != KERNEL_CONSOLE_PROFILE_PHASES || sum != total || !total ||
        total > kernel_console_pm_limit() || owner->Cells == UINT64_MAX ||
        owner->Total > UINT64_MAX - total) { owner->Failed = 1; return 0; }
    for (index = 0; index < KERNEL_CONSOLE_PROFILE_PHASES; ++index) {
        owner->Sum[index] += delta[index];
        if (delta[index] > owner->Max[index]) owner->Max[index] = delta[index];
    }
    owner->Total += total; ++owner->Cells; owner->Next = 0;
    return 1;
}

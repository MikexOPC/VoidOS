#ifndef VTD_ACTIVATION_MODEL_H
#define VTD_ACTIVATION_MODEL_H
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/invalidation/model.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/activation/activation.h"
#define ACT_FRO 0x220U
#define ACT_NFR 4U
typedef struct { BOOT_U32 Offset; BOOT_U64 Value; } ACT_WRITE;
extern BOOT_U64 rtaddr,records[ACT_NFR][2];
extern BOOT_U32 fsts,stall_srtp,stall_te_on,stall_te_off,bad_readback,refill,act_writes,refused;
extern ACT_WRITE act_log[64];
void act_fixture(void);
void act_inject(BOOT_U32 slot,BOOT_U32 source,BOOT_U32 reason,BOOT_U32 read,BOOT_U64 address);
BOOT_U32 act_status(void);
void act_set_status(BOOT_U32 value);
#endif

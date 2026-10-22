#ifndef KERNEL_VGPU_CONFIG_READ_H
#define KERNEL_VGPU_CONFIG_READ_H
#include <stdint.h>
typedef int (*KERNEL_VGPU_CONFIG_READ)(void *,uint32_t,uint32_t,unsigned char *);
int kernel_vgpu_config_read(KERNEL_VGPU_CONFIG_READ read, void *context, unsigned char output[256]);
#endif

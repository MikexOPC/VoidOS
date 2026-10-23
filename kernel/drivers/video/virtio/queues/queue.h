#ifndef KERNEL_VGPU_QUEUE_H
#define KERNEL_VGPU_QUEUE_H
#include <stdint.h>
#include <stddef.h>
#define VGPU_QUEUE_IO 1U
#define VGPU_QUEUE_STATE 2U
#define VGPU_QUEUE_TIME 3U
#define VGPU_QUEUE_RESPONSE 4U
typedef struct { uint64_t Address; uint32_t Length; uint16_t Flags, Next; } VGPU_DESCRIPTOR;
typedef struct { uint32_t Id, Length; } VGPU_USED;
typedef struct {
    VGPU_DESCRIPTOR  Descriptors[8];
    uint16_t  Flags, Available, Heads[8], Padding[6];
    uint16_t  UsedFlags, Used;
    VGPU_USED  Entries[8];
} VGPU_RING;
_Static_assert(sizeof(VGPU_DESCRIPTOR)==16 && offsetof(VGPU_RING,Flags)==128 &&
    offsetof(VGPU_RING,UsedFlags)==160 && sizeof(VGPU_RING)==232,"Split queue layout");
typedef int (*VGPU_QUEUE_ACCESS)(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);
typedef struct {
    void  *Context;
    VGPU_QUEUE_ACCESS  Access;
    int  (*Clock)(void *,uint32_t *);
    int  (*Barrier)(void *);
    uint32_t  Mask, Deadline, Samples;
} VGPU_QUEUE_IO_CONTEXT;
// Physical: CPU-side frames. Device: addresses published to the device; equal to Physical
// without an IOMMU, IOVAs of the owned domain with VIRTIO_F_ACCESS_PLATFORM (VirtIO 1.2 6).
typedef struct {
    volatile unsigned char  *Views[4];
    uint64_t  Physical[4];
    uint64_t  Device[4];
    uint32_t  Bits, Reserved;
} VGPU_BUFFERS;
typedef struct {
    uint64_t  Self, Sequence;
    VGPU_QUEUE_IO_CONTEXT  Io;
    VGPU_BUFFERS  Buffers;
    uint32_t  Notify[2], Fault, State, Width, Height;
    uint16_t  Available, Used;
    uint32_t  Expected, Scanout;
} VGPU_QUEUE;
uint32_t kernel_vgpu_queue_register(uint32_t offset,uint32_t width,uint32_t write);
uint32_t kernel_vgpu_queue_size(uint32_t maximum);
uint32_t kernel_vgpu_queue_completion(uint32_t old,uint32_t current,uint32_t id,uint32_t bytes);
uint32_t kernel_vgpu_queue_display(uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t enabled,uint32_t flags);
int kernel_vgpu_queue_start(VGPU_QUEUE *queue,const VGPU_QUEUE_IO_CONTEXT *io,const VGPU_BUFFERS *buffers);
int kernel_vgpu_queue_exchange(VGPU_QUEUE *queue);
int kernel_vgpu_queue_control(VGPU_QUEUE *queue,uint32_t command,const unsigned char *body,uint32_t bytes);
uint32_t kernel_vgpu_queue_message(uint32_t command,uint32_t bytes);
uint32_t kernel_vgpu_queue_completion_sized(uint32_t old,uint32_t current,uint32_t id,uint32_t bytes,uint32_t expected);
int kernel_vgpu_queue_access(VGPU_QUEUE *queue,uint32_t region,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value);
int kernel_vgpu_queue_response(const unsigned char *data,uint32_t bytes,uint64_t expected,uint32_t *width,uint32_t *height);
int kernel_vgpu_queue_response_selected(const unsigned char *data,uint32_t bytes,uint64_t expected,uint32_t *width,uint32_t *height,uint32_t *scanout);
#endif

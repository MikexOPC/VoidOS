#ifndef OS_KERNEL_GUI_H
#define OS_KERNEL_GUI_H

#include <stdint.h>
#include "kernel/mm/heap/heap.h"
#include "kernel/sync/lock.h"

// Lock order: process table (150) -> gui (160) -> heap (200) -> physical (300).
#define KERNEL_GUI_LOCK_RANK          160U

// Upper bounds to prevent unbounded kernel heap exhaustion from userspace requests.
#define KERNEL_GUI_SHM_INIT_OBJECTS   16U
#define KERNEL_GUI_SHM_MAX_OBJECTS    256U
#define KERNEL_GUI_SHM_INIT_MAPPINGS  32U
#define KERNEL_GUI_SHM_MAX_MAPPINGS   1024U
// Up to 8 MiB per SHM object (2048 * 4 KiB), sufficient for a 1920x1080x32 framebuffer.
#define KERNEL_GUI_SHM_MAX_PAGES      2048U
// Bounded input event ring (power-of-two capacity for hardware PS/2 bursts).
#define KERNEL_GUI_INPUT_CAPACITY     256U
// Maximum dirty rectangles accepted per SYS_DISPLAY_PRESENT call.
#define KERNEL_GUI_MAX_PRESENT_RECTS  32U

#define KERNEL_INPUT_NONE             0U
#define KERNEL_INPUT_KEY              1U
#define KERNEL_INPUT_MOUSE            2U

typedef struct {
    uint32_t  Width;
    uint32_t  Height;
    uint32_t  Stride;
    uint32_t  Format;
    uint64_t  UptimeMs;
    uint64_t  OwnerPid;
    uint64_t  BackbufferShmId;
    uint64_t  RtcPacked;
} KERNEL_DISPLAY_INFO;

_Static_assert(sizeof(KERNEL_DISPLAY_INFO) == 48U, "KERNEL_DISPLAY_INFO ABI size");

typedef struct {
    uint32_t  X;
    uint32_t  Y;
    uint32_t  Width;
    uint32_t  Height;
} KERNEL_DISPLAY_RECT;

_Static_assert(sizeof(KERNEL_DISPLAY_RECT) == 16U, "KERNEL_DISPLAY_RECT ABI size");

typedef struct {
    uint32_t  Kind;
    uint32_t  Flags;
    uint32_t  Code;
    uint32_t  Value;
    int32_t   DeltaX;
    int32_t   DeltaY;
} KERNEL_INPUT_EVENT;

_Static_assert(sizeof(KERNEL_INPUT_EVENT) == 24U, "KERNEL_INPUT_EVENT ABI size");

typedef struct {
    uint64_t  Id;
    uint64_t  CreatorPid;
    uint64_t  ReaderPid;
    uint64_t  PhysBase;
    uint32_t  Pages;
    uint32_t  RefCount;
    uint32_t  Active;
    uint32_t  IsDisplay;
    uint32_t  OwnerReleased;
} KERNEL_SHM_OBJECT;

typedef struct {
    uint64_t  Pid;
    uint64_t  ShmId;
    uint64_t  UserVirt;
    uint32_t  Pages;
    uint32_t  Active;
} KERNEL_SHM_MAPPING;

typedef struct {
    uintptr_t            Self;
    KERNEL_HEAP         *Heap;
    KERNEL_LOCK          Gate;
    uint32_t             Ready;
    uint32_t             DisplayReady;
    uint32_t             Width;
    uint32_t             Height;
    uint32_t             Stride;
    uint32_t             Format;
    uint64_t             DisplayOwnerPid;
    uint64_t             BackbufferShmId;
    uint32_t             ConsoleRestorePending;
    uint32_t             Reserved;
    KERNEL_INPUT_EVENT   InputRing[KERNEL_GUI_INPUT_CAPACITY];
    uint32_t             InputHead;
    uint32_t             InputTail;
    uint32_t             InputCount;
    uint32_t             InputDropped;
    KERNEL_SHM_OBJECT   *ShmObjects;
    uint32_t             ShmObjectCapacity;
    uint32_t             ShmObjectCount;
    KERNEL_SHM_MAPPING  *ShmMappings;
    uint32_t             ShmMappingCapacity;
    uint32_t             ShmMappingCount;
    uint64_t             NextShmId;
} KERNEL_GUI;

int kernel_gui_init(KERNEL_GUI *gui, KERNEL_HEAP *heap);
int kernel_gui_destroy(KERNEL_GUI *gui);

void kernel_gui_global_set(KERNEL_GUI *gui);
KERNEL_GUI *kernel_gui_global(void);

int kernel_gui_set_display_geometry(KERNEL_GUI *gui,
    uint32_t width, uint32_t height, uint32_t stride, uint32_t format);
int kernel_gui_display_info(KERNEL_GUI *gui, uint64_t uptime_ms, KERNEL_DISPLAY_INFO *out);
int kernel_gui_display_acquire(KERNEL_GUI *gui, uint64_t pid, uint64_t backbuffer_shm_id);
int kernel_gui_display_release(KERNEL_GUI *gui, uint64_t pid, uint64_t *backbuffer_shm_out);
uint64_t kernel_gui_display_owner(KERNEL_GUI *gui);
int kernel_gui_take_console_restore(KERNEL_GUI *gui);
int kernel_gui_rect_valid(uint32_t display_width, uint32_t display_height,
    const KERNEL_DISPLAY_RECT *rect);

int kernel_gui_input_push_key(KERNEL_GUI *gui, uint32_t code, uint32_t scalar, uint32_t flags);
int kernel_gui_input_push_mouse(KERNEL_GUI *gui, int32_t dx, int32_t dy, uint32_t buttons);
int kernel_gui_input_poll(KERNEL_GUI *gui, uint64_t pid, KERNEL_INPUT_EVENT *out);

int kernel_gui_shm_create(KERNEL_GUI *gui, uint64_t pid, uint32_t pages,
    uint64_t phys_base, int is_display, uint64_t *shm_id_out);
int kernel_gui_shm_lookup(KERNEL_GUI *gui, uint64_t shm_id,
    uint32_t *pages_out, uint64_t *phys_base_out);
int kernel_gui_shm_publish(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id);
int kernel_gui_shm_pin(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id,
    uint32_t *pages_out, uint64_t *phys_base_out, int *writable_out);
int kernel_gui_shm_unpin(KERNEL_GUI *gui, uint64_t shm_id, int *freed_object_out);
int kernel_gui_shm_map_record(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id, uint64_t user_virt);
int kernel_gui_shm_find_mapping(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id,
    uint64_t *user_virt_out, uint32_t *pages_out, uint64_t *phys_base_out);
int kernel_gui_shm_unmap_record(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id,
    uint64_t *user_virt_out, uint32_t *pages_out, uint64_t *phys_base_out, int *freed_object_out);
int kernel_gui_shm_pop_owned(KERNEL_GUI *gui, uint64_t pid, uint64_t *shm_id_out,
    uint64_t *user_virt_out, uint32_t *pages_out, uint64_t *phys_base_out, int *freed_object_out);
int kernel_gui_shm_release_unmapped(KERNEL_GUI *gui, uint64_t shm_id,
    uint32_t *pages_out, uint64_t *phys_base_out, int *freed_object_out);

#endif

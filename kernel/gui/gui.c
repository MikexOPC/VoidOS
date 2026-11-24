#include "gui.h"
#include "kernel/sync/internal.h"
#include <stddef.h>

typedef struct {
    KERNEL_LOCK_CONTEXT  *Context;
    KERNEL_LOCK_GUARD     Guard;
    uint64_t              SavedIf;
} GUI_ACCESS;

static KERNEL_GUI *gui_global_instance;

// ====== zero_bytes_private ======

static void zero_bytes_private(void *dst, size_t size)
{
    unsigned char *bytes = (unsigned char *)dst;
    for (size_t index = 0; index < size; ++index)
        bytes[index] = 0;
}

// ====== copy_bytes_private ======

static void copy_bytes_private(void *dst, const void *src, size_t size)
{
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;
    for (size_t index = 0; index < size; ++index)
        out[index] = in[index];
}

// ====== enter_private ======

static int enter_private(KERNEL_GUI *gui, GUI_ACCESS *access)
{
    if (!gui || !access || gui->Self != (uintptr_t)gui || !gui->Ready || !gui->Heap)
        return 0;
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (!context) {
        ks_irq_restore(saved_if);
        return 0;
    }
    zero_bytes_private(access, sizeof(*access));
    if (!kernel_lock_acquire(&gui->Gate, context, &access->Guard)) {
        ks_irq_restore(saved_if);
        return 0;
    }
    if (gui->Self != (uintptr_t)gui || !gui->Ready) {
        (void)kernel_lock_release(context, &access->Guard);
        ks_irq_restore(saved_if);
        return 0;
    }
    access->Context = context;
    access->SavedIf = saved_if;
    return 1;
}

// ====== leave_private ======

static void leave_private(GUI_ACCESS *access)
{
    if (!access || !access->Context)
        return;
    (void)kernel_lock_release(access->Context, &access->Guard);
    ks_irq_restore(access->SavedIf);
    access->Context = NULL;
}

// ====== find_shm_object_private ======

static KERNEL_SHM_OBJECT *find_shm_object_private(KERNEL_GUI *gui, uint64_t shm_id)
{
    if (!shm_id || !gui->ShmObjects)
        return NULL;
    for (uint32_t index = 0; index < gui->ShmObjectCapacity; ++index) {
        if (gui->ShmObjects[index].Active && gui->ShmObjects[index].Id == shm_id)
            return &gui->ShmObjects[index];
    }
    return NULL;
}

// ====== grow_shm_objects_private ======

static int grow_shm_objects_private(KERNEL_GUI *gui)
{
    if (gui->ShmObjectCapacity >= KERNEL_GUI_SHM_MAX_OBJECTS)
        return 0;
    uint32_t next_cap = gui->ShmObjectCapacity ? gui->ShmObjectCapacity * 2U : KERNEL_GUI_SHM_INIT_OBJECTS;
    if (next_cap > KERNEL_GUI_SHM_MAX_OBJECTS)
        next_cap = KERNEL_GUI_SHM_MAX_OBJECTS;
    size_t bytes = (size_t)next_cap * sizeof(KERNEL_SHM_OBJECT);
    KERNEL_SHM_OBJECT *grown = kernel_heap_alloc(gui->Heap, bytes, 16);
    if (!grown)
        return 0;
    zero_bytes_private(grown, bytes);
    if (gui->ShmObjects) {
        copy_bytes_private(grown, gui->ShmObjects, (size_t)gui->ShmObjectCapacity * sizeof(KERNEL_SHM_OBJECT));
        (void)kernel_heap_free(gui->Heap, gui->ShmObjects);
    }
    gui->ShmObjects = grown;
    gui->ShmObjectCapacity = next_cap;
    return 1;
}

// ====== grow_shm_mappings_private ======

static int grow_shm_mappings_private(KERNEL_GUI *gui)
{
    if (gui->ShmMappingCapacity >= KERNEL_GUI_SHM_MAX_MAPPINGS)
        return 0;
    uint32_t next_cap = gui->ShmMappingCapacity ? gui->ShmMappingCapacity * 2U : KERNEL_GUI_SHM_INIT_MAPPINGS;
    if (next_cap > KERNEL_GUI_SHM_MAX_MAPPINGS)
        next_cap = KERNEL_GUI_SHM_MAX_MAPPINGS;
    size_t bytes = (size_t)next_cap * sizeof(KERNEL_SHM_MAPPING);
    KERNEL_SHM_MAPPING *grown = kernel_heap_alloc(gui->Heap, bytes, 16);
    if (!grown)
        return 0;
    zero_bytes_private(grown, bytes);
    if (gui->ShmMappings) {
        copy_bytes_private(grown, gui->ShmMappings, (size_t)gui->ShmMappingCapacity * sizeof(KERNEL_SHM_MAPPING));
        (void)kernel_heap_free(gui->Heap, gui->ShmMappings);
    }
    gui->ShmMappings = grown;
    gui->ShmMappingCapacity = next_cap;
    return 1;
}

// ====== push_input_locked ======

static int push_input_locked(KERNEL_GUI *gui, const KERNEL_INPUT_EVENT *event)
{
    if (!gui->DisplayOwnerPid)
        return 0;
    if (gui->InputCount >= KERNEL_GUI_INPUT_CAPACITY) {
        if (gui->InputDropped != UINT32_MAX)
            ++gui->InputDropped;
        return 0;
    }
    gui->InputRing[gui->InputTail] = *event;
    gui->InputTail = (gui->InputTail + 1U) % KERNEL_GUI_INPUT_CAPACITY;
    ++gui->InputCount;
    return 1;
}

// ====== kernel_gui_init ======

int kernel_gui_init(KERNEL_GUI *gui, KERNEL_HEAP *heap)
{
    if (!gui || !heap)
        return 0;
    zero_bytes_private(gui, sizeof(*gui));
    gui->Self = (uintptr_t)gui;
    gui->Heap = heap;
    gui->NextShmId = 1ULL;
    if (!kernel_lock_initialize(&gui->Gate, KERNEL_GUI_LOCK_RANK))
        return 0;
    if (!grow_shm_objects_private(gui))
        return 0;
    if (!grow_shm_mappings_private(gui)) {
        (void)kernel_heap_free(heap, gui->ShmObjects);
        gui->ShmObjects = NULL;
        gui->ShmObjectCapacity = 0;
        return 0;
    }
    gui->Ready = 1;
    return 1;
}

// ====== kernel_gui_destroy ======

int kernel_gui_destroy(KERNEL_GUI *gui)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_HEAP *heap = gui->Heap;
    KERNEL_SHM_OBJECT *objects = gui->ShmObjects;
    KERNEL_SHM_MAPPING *mappings = gui->ShmMappings;
    gui->Ready = 0;
    gui->ShmObjects = NULL;
    gui->ShmMappings = NULL;
    gui->ShmObjectCapacity = 0;
    gui->ShmMappingCapacity = 0;
    gui->ShmObjectCount = 0;
    gui->ShmMappingCount = 0;
    leave_private(&access);
    if (objects)
        (void)kernel_heap_free(heap, objects);
    if (mappings)
        (void)kernel_heap_free(heap, mappings);
    if (gui_global_instance == gui)
        gui_global_instance = NULL;
    return 1;
}

// ====== kernel_gui_global_set ======

void kernel_gui_global_set(KERNEL_GUI *gui)
{
    gui_global_instance = gui;
}

// ====== kernel_gui_global ======

KERNEL_GUI *kernel_gui_global(void)
{
    return gui_global_instance;
}

// ====== kernel_gui_set_display_geometry ======

int kernel_gui_set_display_geometry(KERNEL_GUI *gui,
    uint32_t width, uint32_t height, uint32_t stride, uint32_t format)
{
    if (!width || !height || stride < width)
        return 0;
    uint64_t bytes = (uint64_t)stride * (uint64_t)height * 4ULL;
    if (!bytes || bytes > (uint64_t)KERNEL_GUI_SHM_MAX_PAGES * 4096ULL)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    gui->Width = width;
    gui->Height = height;
    gui->Stride = stride;
    gui->Format = format;
    gui->DisplayReady = 1;
    leave_private(&access);
    return 1;
}

// ====== kernel_gui_display_info ======

int kernel_gui_display_info(KERNEL_GUI *gui, uint64_t uptime_ms, KERNEL_DISPLAY_INFO *out)
{
    if (!out)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int ok = gui->DisplayReady != 0;
    if (ok) {
        zero_bytes_private(out, sizeof(*out));
        out->Width = gui->Width;
        out->Height = gui->Height;
        out->Stride = gui->Stride;
        out->Format = gui->Format;
        out->UptimeMs = uptime_ms;
        out->OwnerPid = gui->DisplayOwnerPid;
        out->BackbufferShmId = gui->BackbufferShmId;
    }
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_display_acquire ======

int kernel_gui_display_acquire(KERNEL_GUI *gui, uint64_t pid, uint64_t backbuffer_shm_id)
{
    if (!pid || !backbuffer_shm_id)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int ok = 0;
    if (gui->DisplayReady && gui->DisplayOwnerPid == 0) {
        KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, backbuffer_shm_id);
        if (obj && obj->IsDisplay) {
            gui->DisplayOwnerPid = pid;
            gui->BackbufferShmId = backbuffer_shm_id;
            gui->ConsoleRestorePending = 0;
            gui->InputHead = 0;
            gui->InputTail = 0;
            gui->InputCount = 0;
            ok = 1;
        }
    }
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_display_release ======

int kernel_gui_display_release(KERNEL_GUI *gui, uint64_t pid, uint64_t *backbuffer_shm_out)
{
    if (!pid)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int released = 0;
    if (gui->DisplayOwnerPid == pid) {
        if (backbuffer_shm_out)
            *backbuffer_shm_out = gui->BackbufferShmId;
        gui->DisplayOwnerPid = 0;
        gui->BackbufferShmId = 0;
        gui->ConsoleRestorePending = 1;
        gui->InputHead = 0;
        gui->InputTail = 0;
        gui->InputCount = 0;
        released = 1;
    }
    leave_private(&access);
    return released;
}

// ====== kernel_gui_display_owner ======

uint64_t kernel_gui_display_owner(KERNEL_GUI *gui)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    uint64_t owner = gui->DisplayOwnerPid;
    leave_private(&access);
    return owner;
}

// ====== kernel_gui_take_console_restore ======

int kernel_gui_take_console_restore(KERNEL_GUI *gui)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int pending = gui->ConsoleRestorePending != 0;
    gui->ConsoleRestorePending = 0;
    leave_private(&access);
    return pending;
}

// ====== kernel_gui_rect_valid ======

int kernel_gui_rect_valid(uint32_t display_width, uint32_t display_height,
    const KERNEL_DISPLAY_RECT *rect)
{
    if (!rect || !display_width || !display_height)
        return 0;
    if (!rect->Width || !rect->Height)
        return 0;
    uint64_t right = (uint64_t)rect->X + (uint64_t)rect->Width;
    uint64_t bottom = (uint64_t)rect->Y + (uint64_t)rect->Height;
    return right <= (uint64_t)display_width && bottom <= (uint64_t)display_height;
}

// ====== kernel_gui_input_push_key ======

int kernel_gui_input_push_key(KERNEL_GUI *gui, uint32_t code, uint32_t scalar, uint32_t flags)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_INPUT_EVENT event = {0};
    event.Kind = KERNEL_INPUT_KEY;
    event.Flags = flags;
    event.Code = code;
    event.Value = scalar;
    int ok = push_input_locked(gui, &event);
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_input_push_mouse ======

int kernel_gui_input_push_mouse(KERNEL_GUI *gui, int32_t dx, int32_t dy, uint32_t buttons)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_INPUT_EVENT event = {0};
    event.Kind = KERNEL_INPUT_MOUSE;
    event.Flags = buttons;
    event.DeltaX = dx;
    event.DeltaY = dy;
    int ok = push_input_locked(gui, &event);
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_input_poll ======

int kernel_gui_input_poll(KERNEL_GUI *gui, uint64_t pid, KERNEL_INPUT_EVENT *out)
{
    if (!pid || !out)
        return -1;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return -1;
    int status = -1;
    if (gui->DisplayOwnerPid == pid) {
        if (gui->InputCount == 0) {
            status = 0;
        } else {
            *out = gui->InputRing[gui->InputHead];
            gui->InputHead = (gui->InputHead + 1U) % KERNEL_GUI_INPUT_CAPACITY;
            --gui->InputCount;
            status = 1;
        }
    }
    leave_private(&access);
    return status;
}

// ====== kernel_gui_shm_create ======

int kernel_gui_shm_create(KERNEL_GUI *gui, uint64_t pid, uint32_t pages,
    uint64_t phys_base, int is_display, uint64_t *shm_id_out)
{
    if (!pid || !pages || pages > KERNEL_GUI_SHM_MAX_PAGES || !phys_base || !shm_id_out)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int ok = 0;
    if (gui->ShmObjectCount >= gui->ShmObjectCapacity && !grow_shm_objects_private(gui)) {
        leave_private(&access);
        return 0;
    }
    for (uint32_t index = 0; index < gui->ShmObjectCapacity; ++index) {
        if (!gui->ShmObjects[index].Active) {
            KERNEL_SHM_OBJECT *obj = &gui->ShmObjects[index];
            obj->Id = gui->NextShmId++;
            obj->CreatorPid = pid;
            obj->ReaderPid = 0;
            obj->OwnerReleased = 0;
            obj->PhysBase = phys_base;
            obj->Pages = pages;
            obj->RefCount = 0;
            obj->Active = 1;
            obj->IsDisplay = is_display ? 1U : 0U;
            ++gui->ShmObjectCount;
            *shm_id_out = obj->Id;
            ok = 1;
            break;
        }
    }
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_shm_lookup ======

int kernel_gui_shm_lookup(KERNEL_GUI *gui, uint64_t shm_id,
    uint32_t *pages_out, uint64_t *phys_base_out)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
    int ok = obj != NULL;
    if (ok) {
        if (pages_out) *pages_out = obj->Pages;
        if (phys_base_out) *phys_base_out = obj->PhysBase;
    }
    leave_private(&access);
    return ok;
}

// ====== shm_access_private ======

static int shm_access_private(const KERNEL_GUI *gui, const KERNEL_SHM_OBJECT *obj, uint64_t pid)
{
    return obj && pid && (obj->CreatorPid == pid
        || (obj->ReaderPid == pid && gui->DisplayOwnerPid == pid));
}

// ====== kernel_gui_shm_publish ======

int kernel_gui_shm_publish(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
    int ok = obj && pid && obj->CreatorPid == pid && !obj->IsDisplay
        && gui->DisplayOwnerPid && gui->DisplayOwnerPid != pid
        && (!obj->ReaderPid || obj->ReaderPid == gui->DisplayOwnerPid);
    if (ok)
        obj->ReaderPid = gui->DisplayOwnerPid;
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_shm_pin ======

int kernel_gui_shm_pin(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id,
    uint32_t *pages_out, uint64_t *phys_base_out, int *writable_out)
{
    if (!pages_out ||
        !phys_base_out ||
        !writable_out
    )
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
    int ok = shm_access_private(gui, obj, pid);
    if (ok) {
        ++obj->RefCount;
        *pages_out = obj->Pages;
        *phys_base_out = obj->PhysBase;
        *writable_out = obj->CreatorPid == pid;
    }
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_shm_unpin ======

int kernel_gui_shm_unpin(KERNEL_GUI *gui, uint64_t shm_id, int *freed_object_out)
{
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
    int ok = obj && obj->RefCount;
    int freed = 0;
    if (ok && --obj->RefCount == 0 && obj->OwnerReleased) {
        obj->Active = 0;
        --gui->ShmObjectCount;
        freed = 1;
    }
    if (freed_object_out)
        *freed_object_out = freed;
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_shm_map_record ======

int kernel_gui_shm_map_record(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id, uint64_t user_virt)
{
    if (!pid || !shm_id || !user_virt)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
    if (!shm_access_private(gui, obj, pid)) {
        leave_private(&access);
        return 0;
    }
    // Reject duplicate mapping of the same SHM object in the same process.
    for (uint32_t index = 0; index < gui->ShmMappingCapacity; ++index) {
        if (gui->ShmMappings[index].Active &&
            gui->ShmMappings[index].Pid == pid &&
            gui->ShmMappings[index].ShmId == shm_id) {
            leave_private(&access);
            return 0;
        }
    }
    if (gui->ShmMappingCount >= gui->ShmMappingCapacity && !grow_shm_mappings_private(gui)) {
        leave_private(&access);
        return 0;
    }
    int ok = 0;
    for (uint32_t index = 0; index < gui->ShmMappingCapacity; ++index) {
        if (!gui->ShmMappings[index].Active) {
            gui->ShmMappings[index].Pid = pid;
            gui->ShmMappings[index].ShmId = shm_id;
            gui->ShmMappings[index].UserVirt = user_virt;
            gui->ShmMappings[index].Pages = obj->Pages;
            gui->ShmMappings[index].Active = 1;
            ++gui->ShmMappingCount;
            ++obj->RefCount;
            if (obj->CreatorPid == pid)
                obj->OwnerReleased = 0;
            ok = 1;
            break;
        }
    }
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_shm_find_mapping ======

int kernel_gui_shm_find_mapping(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id,
    uint64_t *user_virt_out, uint32_t *pages_out, uint64_t *phys_base_out)
{
    if (!pid || !shm_id)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int ok = 0;
    for (uint32_t index = 0; index < gui->ShmMappingCapacity; ++index) {
        if (gui->ShmMappings[index].Active &&
            gui->ShmMappings[index].Pid == pid &&
            gui->ShmMappings[index].ShmId == shm_id) {
            KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
            if (obj) {
                if (user_virt_out) *user_virt_out = gui->ShmMappings[index].UserVirt;
                if (pages_out) *pages_out = obj->Pages;
                if (phys_base_out) *phys_base_out = obj->PhysBase;
                ok = 1;
            }
            break;
        }
    }
    leave_private(&access);
    return ok;
}

// ====== unmap_entry_locked ======

static void unmap_entry_locked(KERNEL_GUI *gui, uint32_t map_index,
    uint64_t *user_virt_out, uint32_t *pages_out, uint64_t *phys_base_out, int *freed_object_out)
{
    uint64_t pid = gui->ShmMappings[map_index].Pid;
    uint64_t shm_id = gui->ShmMappings[map_index].ShmId;
    uint64_t user_virt = gui->ShmMappings[map_index].UserVirt;
    uint32_t pages = gui->ShmMappings[map_index].Pages;
    gui->ShmMappings[map_index].Active = 0;
    --gui->ShmMappingCount;

    uint64_t phys_base = 0;
    int freed = 0;
    KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
    if (obj) {
        if (obj->CreatorPid == pid)
            obj->OwnerReleased = 1;
        phys_base = obj->PhysBase;
        pages = obj->Pages;
        if (obj->RefCount > 0)
            --obj->RefCount;
        if (obj->RefCount == 0) {
            obj->Active = 0;
            --gui->ShmObjectCount;
            freed = 1;
        }
    }
    if (user_virt_out) *user_virt_out = user_virt;
    if (pages_out) *pages_out = pages;
    if (phys_base_out) *phys_base_out = phys_base;
    if (freed_object_out) *freed_object_out = freed;
}

// ====== kernel_gui_shm_unmap_record ======

int kernel_gui_shm_unmap_record(KERNEL_GUI *gui, uint64_t pid, uint64_t shm_id,
    uint64_t *user_virt_out, uint32_t *pages_out, uint64_t *phys_base_out, int *freed_object_out)
{
    if (!pid || !shm_id)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int ok = 0;
    for (uint32_t index = 0; index < gui->ShmMappingCapacity; ++index) {
        if (gui->ShmMappings[index].Active &&
            gui->ShmMappings[index].Pid == pid &&
            gui->ShmMappings[index].ShmId == shm_id) {
            unmap_entry_locked(gui, index, user_virt_out, pages_out, phys_base_out, freed_object_out);
            ok = 1;
            break;
        }
    }
    leave_private(&access);
    return ok;
}

// ====== kernel_gui_shm_pop_owned ======

int kernel_gui_shm_pop_owned(KERNEL_GUI *gui, uint64_t pid, uint64_t *shm_id_out,
    uint64_t *user_virt_out, uint32_t *pages_out, uint64_t *phys_base_out, int *freed_object_out)
{
    if (!pid)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    for (uint32_t index = 0; index < gui->ShmObjectCapacity; ++index) {
        if (gui->ShmObjects[index].Active && gui->ShmObjects[index].CreatorPid == pid)
            gui->ShmObjects[index].OwnerReleased = 1;
    }
    // First pop any active mapping held by pid.
    for (uint32_t index = 0; index < gui->ShmMappingCapacity; ++index) {
        if (gui->ShmMappings[index].Active && gui->ShmMappings[index].Pid == pid) {
            if (shm_id_out) *shm_id_out = gui->ShmMappings[index].ShmId;
            unmap_entry_locked(gui, index, user_virt_out, pages_out, phys_base_out, freed_object_out);
            leave_private(&access);
            return 1;
        }
    }
    // Also reap any unmapped SHM object created by pid (RefCount == 0).
    for (uint32_t index = 0; index < gui->ShmObjectCapacity; ++index) {
        if (gui->ShmObjects[index].Active &&
            gui->ShmObjects[index].CreatorPid == pid &&
            gui->ShmObjects[index].RefCount == 0) {
            KERNEL_SHM_OBJECT *obj = &gui->ShmObjects[index];
            if (shm_id_out) *shm_id_out = obj->Id;
            if (user_virt_out) *user_virt_out = 0;
            if (pages_out) *pages_out = obj->Pages;
            if (phys_base_out) *phys_base_out = obj->PhysBase;
            if (freed_object_out) *freed_object_out = 1;
            obj->Active = 0;
            --gui->ShmObjectCount;
            leave_private(&access);
            return 1;
        }
    }
    leave_private(&access);
    return 0;
}

// ====== kernel_gui_shm_release_unmapped ======

int kernel_gui_shm_release_unmapped(KERNEL_GUI *gui, uint64_t shm_id,
    uint32_t *pages_out, uint64_t *phys_base_out, int *freed_object_out)
{
    if (!shm_id)
        return 0;
    GUI_ACCESS access;
    if (!enter_private(gui, &access))
        return 0;
    int ok = 0;
    KERNEL_SHM_OBJECT *obj = find_shm_object_private(gui, shm_id);
    if (obj && obj->RefCount == 0) {
        if (pages_out) *pages_out = obj->Pages;
        if (phys_base_out) *phys_base_out = obj->PhysBase;
        if (freed_object_out) *freed_object_out = 1;
        obj->Active = 0;
        --gui->ShmObjectCount;
        ok = 1;
    }
    leave_private(&access);
    return ok;
}

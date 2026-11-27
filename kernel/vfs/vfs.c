#include "internal.h"
#include "kernel/diagnostics/log.h"

static KERNEL_VFS system_vfs;

// ====== zero_bytes ======

static void zero_bytes(void *pointer, size_t bytes)
{
    uint8_t *target = (uint8_t *)pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = 0;
}

// ====== copy_bytes ======

static void copy_bytes(void *target_pointer, const void *source_pointer, size_t bytes)
{
    uint8_t *target = (uint8_t *)target_pointer;
    const uint8_t *source = (const uint8_t *)source_pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = source[index];
}

// ====== string_len ======

static uint32_t string_len(const char *text, uint32_t max_len)
{
    if (!text)
        return 0;
    uint32_t len = 0;
    while (len < max_len && text[len] != '\0')
        ++len;
    return len;
}

// ====== vfs_enter_private ======

int vfs_enter_private(KERNEL_VFS *vfs, VFS_ACCESS *access)
{
    if (!vfs || !access || vfs->Self != (uintptr_t)vfs || !vfs->Ready)
        return 0;
    for (uint32_t attempt = 0; attempt < 1000000U; ++attempt) {
        uint64_t saved_if = ks_irq_save();
        KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
        if (!context) {
            ks_irq_restore(saved_if);
            return 0;
        }
        KERNEL_LOCK_GUARD guard;
        zero_bytes(&guard, sizeof(guard));
        if (!kernel_lock_acquire(&vfs->Lock, context, &guard)) {
            ks_irq_restore(saved_if);
            return 0;
        }
        if (vfs->Self != (uintptr_t)vfs || !vfs->Ready) {
            (void)kernel_lock_release(context, &guard);
            ks_irq_restore(saved_if);
            return 0;
        }
        if (!vfs->IoBusy) {
            vfs->IoBusy = 1U;
            if (!kernel_lock_release(context, &guard))
                ks_fail_stop("VFS_LOCK_RELEASE_FAILED");
            ks_irq_restore(saved_if);
            access->Vfs = vfs;
            return 1;
        }
        if (!kernel_lock_release(context, &guard))
            ks_fail_stop("VFS_LOCK_RELEASE_FAILED");
        ks_irq_restore(saved_if);
        ks_pause();
    }
    return 0;
}

// ====== vfs_leave_private ======

void vfs_leave_private(VFS_ACCESS *access)
{
    KERNEL_VFS *vfs = access ? access->Vfs : NULL;
    if (!vfs)
        return;
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (!context)
        ks_fail_stop("VFS_LOCK_CONTEXT_MISSING");
    KERNEL_LOCK_GUARD guard;
    zero_bytes(&guard, sizeof(guard));
    if (!kernel_lock_acquire(&vfs->Lock, context, &guard))
        ks_fail_stop("VFS_LOCK_REACQUIRE_FAILED");
    vfs->IoBusy = 0U;
    if (!kernel_lock_release(context, &guard))
        ks_fail_stop("VFS_LOCK_RELEASE_FAILED");
    ks_irq_restore(saved_if);
    access->Vfs = NULL;
}

static int enter_private(KERNEL_VFS *vfs, VFS_ACCESS *access) { return vfs_enter_private(vfs, access); }
static void leave_private(VFS_ACCESS *access) { vfs_leave_private(access); }

// ====== kernel_vfs_init ======

int kernel_vfs_init(KERNEL_VFS *vfs, KERNEL_HEAP *heap)
{
    if (!vfs || !heap)
        return KERNEL_STORAGE_ERR_ARG;

    zero_bytes(vfs, sizeof(*vfs));
    KERNEL_VFS_OPEN_FILE *files = (KERNEL_VFS_OPEN_FILE *)kernel_heap_alloc(
        heap,
        (uint64_t)KERNEL_VFS_INIT_OPEN_FILES * sizeof(KERNEL_VFS_OPEN_FILE),
        16
    );
    if (!files)
        return KERNEL_STORAGE_ERR_NOMEM;

    if (!kernel_lock_initialize(&vfs->Lock, KERNEL_VFS_LOCK_RANK)) {
        (void)kernel_heap_free(heap, files);
        return KERNEL_STORAGE_ERR_ARG;
    }

    vfs->Heap = heap;
    vfs->OpenFiles = files;
    vfs->OpenCapacity = KERNEL_VFS_INIT_OPEN_FILES;
    vfs->Ready = 1U;
    vfs->Self = (uintptr_t)vfs;
    return KERNEL_STORAGE_OK;
}

// ====== kernel_vfs_destroy ======

void kernel_vfs_destroy(KERNEL_VFS *vfs)
{
    if (!vfs || vfs->Self != (uintptr_t)vfs || !vfs->Ready)
        return;
    KERNEL_HEAP *heap = vfs->Heap;
    KERNEL_VFS_OPEN_FILE *files = vfs->OpenFiles;
    zero_bytes(vfs, sizeof(*vfs));
    if (heap && files)
        (void)kernel_heap_free(heap, files);
}

// ====== kernel_vfs_mount_fat ======

int kernel_vfs_mount_fat(KERNEL_VFS *vfs, const char *prefix, KERNEL_BLOCK_CACHE *cache)
{
    if (!vfs || !prefix || prefix[0] != '/' || !kernel_block_cache_valid(cache))
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t plen = string_len(prefix, 32U);
    if (!plen || plen >= 32U)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t slot = KERNEL_VFS_MAX_MOUNTS;
    for (uint32_t i = 0; i < KERNEL_VFS_MAX_MOUNTS; ++i) {
        if (!vfs->Mounts[i].Active) {
            slot = i;
            break;
        }
    }
    if (slot >= KERNEL_VFS_MAX_MOUNTS) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_NOSPC;
    }

    uint8_t sector[512];
    uint64_t disk_sectors = cache->Device->TotalSectors;
    int status = kernel_block_cache_read_sectors(cache, 0ULL, 1U, sector);
    if (status != KERNEL_STORAGE_OK) {
        leave_private(&access);
        return status;
    }

    uint64_t part_lba = 0;
    if (kfat_detect_partition_lba(sector, disk_sectors, &part_lba) != KFAT_OK) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_CORRUPT;
    }

    if (part_lba != 0ULL) {
        status = kernel_block_cache_read_sectors(cache, part_lba, 1U, sector);
        if (status != KERNEL_STORAGE_OK) {
            leave_private(&access);
            return status;
        }
    }

    KERNEL_FAT_GEOMETRY geom;
    if (kfat_parse_bpb(sector, part_lba, disk_sectors, &geom) != KFAT_OK) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_CORRUPT;
    }

    KERNEL_VFS_MOUNT *mnt = &vfs->Mounts[slot];
    zero_bytes(mnt, sizeof(*mnt));
    copy_bytes(mnt->Prefix, prefix, plen);
    mnt->Prefix[plen] = '\0';
    mnt->PrefixLen = plen;
    mnt->Cache = cache;
    mnt->Geometry = geom;
    mnt->Active = 1U;
    if (slot >= vfs->MountCount)
        vfs->MountCount = slot + 1U;

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_vfs_unmount ======

int kernel_vfs_unmount(KERNEL_VFS *vfs, const char *prefix)
{
    if (!vfs || !prefix || prefix[0] != '/')
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t plen = string_len(prefix, 32U);
    if (!plen || plen >= 32U)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t found = KERNEL_VFS_MAX_MOUNTS;
    for (uint32_t i = 0; i < vfs->MountCount; ++i) {
        const KERNEL_VFS_MOUNT *m = &vfs->Mounts[i];
        if (!m->Active || m->PrefixLen != plen)
            continue;
        int match = 1;
        for (uint32_t k = 0; k < plen; ++k) {
            if (m->Prefix[k] != prefix[k]) {
                match = 0;
                break;
            }
        }
        if (match) {
            found = i;
            break;
        }
    }
    if (found >= KERNEL_VFS_MAX_MOUNTS) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }

    if (vfs->OpenFiles) {
        for (uint32_t f = 0; f < vfs->OpenCapacity; ++f) {
            if (vfs->OpenFiles[f].InUse && vfs->OpenFiles[f].MountIndex == found) {
                zero_bytes(&vfs->OpenFiles[f], sizeof(vfs->OpenFiles[f]));
                if (vfs->OpenCount > 0U)
                    --vfs->OpenCount;
            }
        }
    }

    zero_bytes(&vfs->Mounts[found], sizeof(vfs->Mounts[found]));
    while (vfs->MountCount > 0U && !vfs->Mounts[vfs->MountCount - 1U].Active)
        --vfs->MountCount;

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== vfs_resolve_mount_private ======

const KERNEL_VFS_MOUNT *vfs_resolve_mount_private(
    const KERNEL_VFS *vfs,
    const char *path,
    const char **subpath_out
)
{
    if (!path || path[0] != '/')
        return NULL;

    const KERNEL_VFS_MOUNT *best = NULL;
    uint32_t best_len = 0;
    const char *best_sub = path;

    for (uint32_t i = 0; i < vfs->MountCount; ++i) {
        const KERNEL_VFS_MOUNT *m = &vfs->Mounts[i];
        if (!m->Active)
            continue;
        if (m->PrefixLen == 1U && m->Prefix[0] == '/') {
            if (!best) {
                best = m;
                best_len = 1U;
                best_sub = path;
            }
            continue;
        }
        int match = 1;
        for (uint32_t k = 0; k < m->PrefixLen; ++k) {
            if (path[k] != m->Prefix[k]) {
                match = 0;
                break;
            }
        }
        if (match && (path[m->PrefixLen] == '\0' || path[m->PrefixLen] == '/')) {
            if (m->PrefixLen > best_len) {
                best = m;
                best_len = m->PrefixLen;
                best_sub = path[m->PrefixLen] == '\0' ? "/" : (path + m->PrefixLen);
            }
        }
    }

    if (best && subpath_out)
        *subpath_out = best_sub;
    return best;
}

static const KERNEL_VFS_MOUNT *resolve_mount_private(
    const KERNEL_VFS *vfs,
    const char *path,
    const char **subpath_out
)
{
    return vfs_resolve_mount_private(vfs, path, subpath_out);
}

// ====== vfs_read_next_cluster_private ======

int vfs_read_next_cluster_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t cluster,
    uint32_t *next_out
)
{
    uint64_t fat_lba = 0;
    uint32_t byte_off = 0;
    if (!kfat_fat_slot(&mount->Geometry, cluster, &fat_lba, &byte_off))
        return KERNEL_STORAGE_ERR_CORRUPT;

    uint8_t sector[512];
    int status = kernel_block_cache_read_sectors(mount->Cache, fat_lba, 1U, sector);
    if (status != KERNEL_STORAGE_OK)
        return status;

    uint32_t raw = 0;
    if (mount->Geometry.IsFat32) {
        raw = (uint32_t)sector[byte_off]
            | ((uint32_t)sector[byte_off + 1U] << 8)
            | ((uint32_t)sector[byte_off + 2U] << 16)
            | ((uint32_t)sector[byte_off + 3U] << 24);
    } else {
        raw = (uint32_t)sector[byte_off] | ((uint32_t)sector[byte_off + 1U] << 8);
    }

    uint32_t decoded = kfat_decode_next_cluster(&mount->Geometry, raw);
    if (decoded == FAT_CLUSTER_BAD || decoded == 0U)
        return KERNEL_STORAGE_ERR_CORRUPT;

    *next_out = decoded;
    return KERNEL_STORAGE_OK;
}

static int read_next_cluster_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t cluster,
    uint32_t *next_out
)
{
    return vfs_read_next_cluster_private(mount, cluster, next_out);
}

// ====== scan_dir_sector_private ======

typedef struct {
    uint32_t  Count;
    uint64_t  Lba[FAT_MAX_LFN_SLOTS];
    uint32_t  ByteOffset[FAT_MAX_LFN_SLOTS];
} FAT_LFN_TRACKER;

// Mode 0: find by name (`target_name`, `target_len`).
// Mode 1: find by valid entry index (`target_index`, current counter in `*seen_count`).
static int scan_dir_sector_private(
    const uint8_t sector[512],
    uint64_t sector_lba,
    KERNEL_FAT_LFN_STATE *lfn,
    FAT_LFN_TRACKER *tracker,
    int by_index,
    const char *target_name,
    uint32_t target_len,
    uint32_t target_index,
    uint32_t *seen_count,
    FAT_NODE_LOC *out_node
)
{
    for (uint32_t off = 0; off < 512U; off += 32U) {
        uint8_t first = sector[off];
        uint8_t attr = sector[off + 11U];
        if (first != 0x00U && first != 0xE5U && attr == FAT_ATTR_LONG_NAME) {
            if ((first & 0x40U) != 0U)
                tracker->Count = 0U;
            if (tracker->Count < FAT_MAX_LFN_SLOTS) {
                tracker->Lba[tracker->Count] = sector_lba;
                tracker->ByteOffset[tracker->Count] = off;
                ++tracker->Count;
            }
        }

        KERNEL_FAT_DIR_ENTRY parsed;
        uint32_t step = kfat_dir_step(sector + off, off, lfn, &parsed);
        if (step == 0U) {
            tracker->Count = 0U;
            return KERNEL_STORAGE_ERR_NOT_FOUND;
        }
        if (step != 2U) {
            if (first == 0xE5U || attr != FAT_ATTR_LONG_NAME)
                tracker->Count = 0U;
            continue;
        }

        int matched = 0;
        if (by_index) {
            if (*seen_count == target_index)
                matched = 1;
            ++(*seen_count);
        } else {
            matched = kfat_entry_matches_name(
                sector + off,
                &parsed,
                (const uint8_t *)target_name,
                target_len
            ) != 0U;
        }

        if (matched) {
            zero_bytes(out_node, sizeof(*out_node));
            out_node->DirEntryLba = sector_lba;
            out_node->DirEntryByteOffset = off;
            out_node->FirstCluster = parsed.FirstCluster;
            out_node->FileSize = parsed.FileSize;
            out_node->Attr = parsed.Attr;
            out_node->NameLen = parsed.NameLen < 63U ? parsed.NameLen : 63U;
            copy_bytes(out_node->Name, parsed.Name, out_node->NameLen);
            out_node->Name[out_node->NameLen] = '\0';
            out_node->LfnCount = tracker->Count;
            for (uint32_t k = 0; k < tracker->Count; ++k) {
                out_node->LfnLba[k] = tracker->Lba[k];
                out_node->LfnByteOffset[k] = tracker->ByteOffset[k];
            }
            tracker->Count = 0U;
            return KERNEL_STORAGE_OK;
        }
        tracker->Count = 0U;
    }
    return 1;
}

// ====== search_directory_private ======

static int search_directory_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t dir_cluster,
    int by_index,
    const char *target_name,
    uint32_t target_len,
    uint32_t target_index,
    FAT_NODE_LOC *out_node
)
{
    uint8_t sector[512];
    KERNEL_FAT_LFN_STATE lfn = {0};
    FAT_LFN_TRACKER tracker = {0};
    uint32_t seen = 0;

    // FAT16 fixed root directory when dir_cluster == 0 and !IsFat32.
    if (dir_cluster == 0U && !mount->Geometry.IsFat32) {
        for (uint32_t s = 0; s < mount->Geometry.RootDirSectors; ++s) {
            uint64_t lba = mount->Geometry.RootDirLba + s;
            int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
            if (status != KERNEL_STORAGE_OK)
                return status;

            int res = scan_dir_sector_private(
                sector,
                lba,
                &lfn,
                &tracker,
                by_index,
                target_name,
                target_len,
                target_index,
                &seen,
                out_node
            );
            if (res <= 0)
                return res;
        }
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }

    uint32_t current = (dir_cluster == 0U && mount->Geometry.IsFat32)
        ? mount->Geometry.RootCluster
        : dir_cluster;
    uint32_t hops = 0;

    while (current >= 2U && current != FAT_CLUSTER_EOF) {
        if (++hops > mount->Geometry.TotalClusters)
            return KERNEL_STORAGE_ERR_CORRUPT;

        uint64_t cluster_lba = kfat_cluster_to_lba(&mount->Geometry, current);
        if (!cluster_lba)
            return KERNEL_STORAGE_ERR_CORRUPT;

        for (uint32_t s = 0; s < mount->Geometry.SectorsPerCluster; ++s) {
            uint64_t lba = cluster_lba + s;
            int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
            if (status != KERNEL_STORAGE_OK)
                return status;

            int res = scan_dir_sector_private(
                sector,
                lba,
                &lfn,
                &tracker,
                by_index,
                target_name,
                target_len,
                target_index,
                &seen,
                out_node
            );
            if (res <= 0)
                return res;
        }

        uint32_t next = FAT_CLUSTER_EOF;
        int status = read_next_cluster_private(mount, current, &next);
        if (status != KERNEL_STORAGE_OK)
            return status;
        current = next;
    }

    return KERNEL_STORAGE_ERR_NOT_FOUND;
}

// ====== vfs_resolve_path_private ======

int vfs_resolve_path_private(
    const KERNEL_VFS_MOUNT *mount,
    const char *subpath,
    FAT_NODE_LOC *out_node
)
{
    if (!mount || !subpath || subpath[0] != '/' || !out_node)
        return KERNEL_STORAGE_ERR_ARG;

    zero_bytes(out_node, sizeof(*out_node));
    out_node->Attr = FAT_ATTR_DIRECTORY;
    out_node->FirstCluster = mount->Geometry.IsFat32 ? mount->Geometry.RootCluster : 0U;
    out_node->Name[0] = '/';
    out_node->NameLen = 1U;

    const char *cursor = subpath;
    while (*cursor == '/')
        ++cursor;

    if (*cursor == '\0')
        return KERNEL_STORAGE_OK;

    while (*cursor != '\0') {
        if (!(out_node->Attr & FAT_ATTR_DIRECTORY))
            return KERNEL_STORAGE_ERR_NOT_FOUND;

        const char *start = cursor;
        uint32_t comp_len = 0;
        while (*cursor != '\0' && *cursor != '/') {
            if (++comp_len >= KERNEL_VFS_NAME_CAPACITY)
                return KERNEL_STORAGE_ERR_ARG;
            ++cursor;
        }
        while (*cursor == '/')
            ++cursor;

        FAT_NODE_LOC child;
        int status = search_directory_private(
            mount,
            out_node->FirstCluster,
            0,
            start,
            comp_len,
            0U,
            &child
        );
        if (status != KERNEL_STORAGE_OK)
            return status;

        *out_node = child;
    }

    return KERNEL_STORAGE_OK;
}

static int resolve_path_private(
    const KERNEL_VFS_MOUNT *mount,
    const char *subpath,
    FAT_NODE_LOC *out_node
)
{
    return vfs_resolve_path_private(mount, subpath, out_node);
}

// ====== kernel_vfs_stat ======

int kernel_vfs_stat(KERNEL_VFS *vfs, const char *path, KERNEL_VFS_STAT *out)
{
    if (!vfs || !path || !out)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    const char *subpath = NULL;
    const KERNEL_VFS_MOUNT *mount = resolve_mount_private(vfs, path, &subpath);
    if (!mount) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }

    FAT_NODE_LOC node;
    int status = resolve_path_private(mount, subpath, &node);
    if (status == KERNEL_STORAGE_OK) {
        zero_bytes(out, sizeof(*out));
        copy_bytes(out->Name, node.Name, node.NameLen);
        out->Name[node.NameLen] = '\0';
        out->NameLen = node.NameLen;
        out->IsDirectory = (node.Attr & FAT_ATTR_DIRECTORY) ? 1U : 0U;
        out->Size = node.FileSize;
        out->FirstCluster = node.FirstCluster;
    }

    leave_private(&access);
    return status;
}

// ====== kernel_vfs_readdir ======

int kernel_vfs_readdir(KERNEL_VFS *vfs, const char *dir_path, uint32_t index, KERNEL_VFS_STAT *out)
{
    if (!vfs || !dir_path || !out)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    const char *subpath = NULL;
    const KERNEL_VFS_MOUNT *mount = resolve_mount_private(vfs, dir_path, &subpath);
    if (!mount) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }

    FAT_NODE_LOC dir_node;
    int status = resolve_path_private(mount, subpath, &dir_node);
    if (status != KERNEL_STORAGE_OK) {
        leave_private(&access);
        return status;
    }
    if (!(dir_node.Attr & FAT_ATTR_DIRECTORY)) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    FAT_NODE_LOC entry_node;
    status = search_directory_private(
        mount,
        dir_node.FirstCluster,
        1,
        NULL,
        0U,
        index,
        &entry_node
    );
    if (status == KERNEL_STORAGE_OK) {
        zero_bytes(out, sizeof(*out));
        copy_bytes(out->Name, entry_node.Name, entry_node.NameLen);
        out->Name[entry_node.NameLen] = '\0';
        out->NameLen = entry_node.NameLen;
        out->IsDirectory = (entry_node.Attr & FAT_ATTR_DIRECTORY) ? 1U : 0U;
        out->Size = entry_node.FileSize;
        out->FirstCluster = entry_node.FirstCluster;
    }

    leave_private(&access);
    return status;
}

// ====== kernel_vfs_read_file ======

int kernel_vfs_read_file(
    KERNEL_VFS *vfs,
    const char *path,
    uint32_t offset,
    void *buffer,
    uint32_t length,
    uint32_t *bytes_read_out
)
{
    if (!vfs || !path || (!buffer && length) || !bytes_read_out)
        return KERNEL_STORAGE_ERR_ARG;

    *bytes_read_out = 0;
    VFS_ACCESS access;
    if (!enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    const char *subpath = NULL;
    const KERNEL_VFS_MOUNT *mount = resolve_mount_private(vfs, path, &subpath);
    if (!mount) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }

    FAT_NODE_LOC node;
    int status = resolve_path_private(mount, subpath, &node);
    if (status != KERNEL_STORAGE_OK) {
        leave_private(&access);
        return status;
    }
    if (node.Attr & FAT_ATTR_DIRECTORY) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    if (offset >= node.FileSize || length == 0U || node.FirstCluster < 2U) {
        leave_private(&access);
        return KERNEL_STORAGE_OK;
    }

    uint32_t available = node.FileSize - offset;
    uint32_t to_read = length < available ? length : available;
    uint32_t cluster_bytes = mount->Geometry.SectorsPerCluster * 512U;
    uint32_t current_cluster = node.FirstCluster;
    uint32_t skip_clusters = offset / cluster_bytes;
    uint32_t in_cluster_offset = offset % cluster_bytes;
    uint32_t hops = 0;

    while (skip_clusters > 0U) {
        if (++hops > mount->Geometry.TotalClusters) {
            leave_private(&access);
            return KERNEL_STORAGE_ERR_CORRUPT;
        }
        uint32_t next = FAT_CLUSTER_EOF;
        status = read_next_cluster_private(mount, current_cluster, &next);
        if (status != KERNEL_STORAGE_OK) {
            leave_private(&access);
            return status;
        }
        if (next == FAT_CLUSTER_EOF) {
            leave_private(&access);
            return KERNEL_STORAGE_ERR_CORRUPT;
        }
        current_cluster = next;
        --skip_clusters;
    }

    uint8_t *target = (uint8_t *)buffer;
    uint32_t remaining = to_read;

    while (remaining > 0U) {
        if (current_cluster < 2U || current_cluster == FAT_CLUSTER_EOF ||
            ++hops > mount->Geometry.TotalClusters) {
            leave_private(&access);
            return KERNEL_STORAGE_ERR_CORRUPT;
        }

        uint64_t cluster_lba = kfat_cluster_to_lba(&mount->Geometry, current_cluster);
        if (!cluster_lba) {
            leave_private(&access);
            return KERNEL_STORAGE_ERR_CORRUPT;
        }

        uint32_t take = cluster_bytes - in_cluster_offset;
        if (take > remaining)
            take = remaining;

        uint64_t byte_pos = cluster_lba * 512ULL + in_cluster_offset;
        status = kernel_block_cache_read_bytes(mount->Cache, byte_pos, take, target);
        if (status != KERNEL_STORAGE_OK) {
            leave_private(&access);
            return status;
        }

        target += take;
        *bytes_read_out += take;
        remaining -= take;
        in_cluster_offset = 0U;

        if (remaining > 0U) {
            uint32_t next = FAT_CLUSTER_EOF;
            status = read_next_cluster_private(mount, current_cluster, &next);
            if (status != KERNEL_STORAGE_OK) {
                leave_private(&access);
                return status;
            }
            current_cluster = next;
        }
    }

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_vfs_sync ======

int kernel_vfs_sync(KERNEL_VFS *vfs)
{
    VFS_ACCESS access;
    if (!enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    for (uint32_t i = 0; i < vfs->MountCount; ++i) {
        if (vfs->Mounts[i].Active && vfs->Mounts[i].Cache) {
            int status = kernel_block_cache_flush(vfs->Mounts[i].Cache);
            if (status != KERNEL_STORAGE_OK) {
                leave_private(&access);
                return status;
            }
        }
    }

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_vfs ======

KERNEL_VFS *kernel_vfs(void)
{
    return (system_vfs.Self == (uintptr_t)&system_vfs && system_vfs.Ready) ? &system_vfs : NULL;
}

// ====== kernel_vfs_system_start ======

int kernel_vfs_system_start(
    KERNEL_HEAP *heap,
    KERNEL_BLOCK_CACHE *root_cache,
    KERNEL_BLOCK_CACHE *boot_cache,
    KERNEL_BLOCK_CACHE *usb_cache
)
{
    if (system_vfs.Ready)
        return 1;

    if (!heap || kernel_vfs_init(&system_vfs, heap) != KERNEL_STORAGE_OK)
        return 0;

    uint32_t root_bytes = 0;
    uint32_t boot_bytes = 0;
    char buf[64];
    int usb_used_as_root = 0;

    if (root_cache && kernel_block_cache_valid(root_cache)) {
        if (kernel_vfs_mount_fat(&system_vfs, "/", root_cache) != KERNEL_STORAGE_OK)
            return 0;
        if (kernel_vfs_read_file(&system_vfs, "/HELLO.TXT", 0U, buf, sizeof(buf), &root_bytes) != KERNEL_STORAGE_OK ||
            root_bytes != 21U || buf[0] != 'V' || buf[7] != 'F')
            return 0;

        uint32_t written = 0;
        uint32_t verify_read = 0;
        char verify_buf[32] = {0};
        KERNEL_VFS_STAT st;
        if (kernel_vfs_write_file(&system_vfs, "/VERIFY.TXT", 0U, "VoidOS FAT32 write ok", 21U, 1, 1, &written) != KERNEL_STORAGE_OK ||
            written != 21U ||
            kernel_vfs_sync(&system_vfs) != KERNEL_STORAGE_OK ||
            kernel_block_cache_invalidate(root_cache) != KERNEL_STORAGE_OK ||
            kernel_vfs_read_file(&system_vfs, "/VERIFY.TXT", 0U, verify_buf, sizeof(verify_buf), &verify_read) != KERNEL_STORAGE_OK ||
            verify_read != 21U || verify_buf[13] != 'w' ||
            kernel_vfs_unlink(&system_vfs, "/VERIFY.TXT") != KERNEL_STORAGE_OK ||
            kernel_vfs_stat(&system_vfs, "/VERIFY.TXT", &st) != KERNEL_STORAGE_ERR_NOT_FOUND)
            return 0;
    } else if (usb_cache && kernel_block_cache_valid(usb_cache)) {
        if (kernel_vfs_mount_fat(&system_vfs, "/", usb_cache) == KERNEL_STORAGE_OK) {
            usb_used_as_root = 1;
            (void)kernel_vfs_read_file(&system_vfs, "/HELLO.TXT", 0U, buf, sizeof(buf), &root_bytes);
            (void)kernel_log(0, "KERNEL_VFS_USB_ROOT_MOUNTED");
        }
    }

    if (boot_cache && kernel_block_cache_valid(boot_cache)) {
        const char *boot_prefix = system_vfs.MountCount ? "/boot" : "/";
        if (kernel_vfs_mount_fat(&system_vfs, boot_prefix, boot_cache) == KERNEL_STORAGE_OK) {
            const char *check_path = (boot_prefix[1] == '\0')
                ? "/EFI/BOOT/boot-check.txt"
                : "/boot/EFI/BOOT/boot-check.txt";
            int check_rc = kernel_vfs_read_file(&system_vfs, check_path, 0U, buf, sizeof(buf), &boot_bytes);
            if (check_rc == KERNEL_STORAGE_OK) {
                if (boot_bytes != 15U || buf[0] != 'O' || buf[1] != 'S')
                    return 0;
            } else if (root_cache && kernel_block_cache_valid(root_cache)) {
                return 0;
            }
        } else {
            (void)kernel_log(1, "KERNEL_VFS_BOOT_SKIPPED");
        }
    }

    if (!usb_used_as_root && usb_cache && kernel_block_cache_valid(usb_cache)) {
        const char *usb_prefix = system_vfs.MountCount ? "/usb" : "/";
        if (kernel_vfs_mount_fat(&system_vfs, usb_prefix, usb_cache) == KERNEL_STORAGE_OK) {
            (void)kernel_log(0, "KERNEL_VFS_USB_MOUNTED");
        }
    }

    return kernel_text("KERNEL_VFS_MOUNTS: ") && kernel_number(system_vfs.MountCount) &&
        kernel_text("\nKERNEL_VFS_ROOT_BYTES: ") && kernel_number(root_bytes) &&
        kernel_text("\nKERNEL_VFS_BOOT_BYTES: ") && kernel_number(boot_bytes) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_VFS_READY");
}

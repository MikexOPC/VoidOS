#include "internal.h"

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

// ====== read_raw_fat_private ======

static int read_raw_fat_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t cluster,
    uint32_t *raw_out
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

    if (mount->Geometry.IsFat32) {
        *raw_out = ((uint32_t)sector[byte_off]
            | ((uint32_t)sector[byte_off + 1U] << 8)
            | ((uint32_t)sector[byte_off + 2U] << 16)
            | ((uint32_t)sector[byte_off + 3U] << 24)) & 0x0FFFFFFFU;
    } else {
        *raw_out = (uint32_t)sector[byte_off] | ((uint32_t)sector[byte_off + 1U] << 8);
    }
    return KERNEL_STORAGE_OK;
}

// ====== write_fat_entry_private ======

static int write_fat_entry_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t cluster,
    uint32_t value
)
{
    uint64_t fat_lba = 0;
    uint32_t byte_off = 0;
    if (!kfat_fat_slot(&mount->Geometry, cluster, &fat_lba, &byte_off))
        return KERNEL_STORAGE_ERR_CORRUPT;

    uint8_t sector[512];
    for (uint32_t f = 0; f < mount->Geometry.NumFats; ++f) {
        uint64_t lba = fat_lba + (uint64_t)f * mount->Geometry.FatSectors;
        int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
        if (status != KERNEL_STORAGE_OK)
            return status;

        if (mount->Geometry.IsFat32) {
            uint32_t old = (uint32_t)sector[byte_off]
                | ((uint32_t)sector[byte_off + 1U] << 8)
                | ((uint32_t)sector[byte_off + 2U] << 16)
                | ((uint32_t)sector[byte_off + 3U] << 24);
            uint32_t next = (old & 0xF0000000U) | (value & 0x0FFFFFFFU);
            sector[byte_off + 0U] = (uint8_t)(next & 0xFFU);
            sector[byte_off + 1U] = (uint8_t)((next >> 8) & 0xFFU);
            sector[byte_off + 2U] = (uint8_t)((next >> 16) & 0xFFU);
            sector[byte_off + 3U] = (uint8_t)((next >> 24) & 0xFFU);
        } else {
            uint16_t next = (uint16_t)(value & 0xFFFFU);
            sector[byte_off + 0U] = (uint8_t)(next & 0xFFU);
            sector[byte_off + 1U] = (uint8_t)((next >> 8) & 0xFFU);
        }

        status = kernel_block_cache_write_sectors(mount->Cache, lba, 1U, sector);
        if (status != KERNEL_STORAGE_OK)
            return status;
    }
    return KERNEL_STORAGE_OK;
}

// ====== allocate_cluster_private ======

static int allocate_cluster_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t prev_cluster,
    uint32_t *out_cluster
)
{
    uint32_t max_cluster = mount->Geometry.TotalClusters + 1U;
    uint8_t zero_sector[512];
    zero_bytes(zero_sector, sizeof(zero_sector));

    for (uint32_t c = 2U; c <= max_cluster; ++c) {
        uint32_t raw = 1U;
        int status = read_raw_fat_private(mount, c, &raw);
        if (status != KERNEL_STORAGE_OK)
            return status;
        if (raw != 0U)
            continue;

        status = write_fat_entry_private(mount, c, FAT_CLUSTER_EOF);
        if (status != KERNEL_STORAGE_OK)
            return status;

        if (prev_cluster >= 2U) {
            status = write_fat_entry_private(mount, prev_cluster, c);
            if (status != KERNEL_STORAGE_OK)
                return status;
        }

        uint64_t lba = kfat_cluster_to_lba(&mount->Geometry, c);
        if (!lba)
            return KERNEL_STORAGE_ERR_CORRUPT;

        for (uint32_t s = 0; s < mount->Geometry.SectorsPerCluster; ++s) {
            status = kernel_block_cache_write_sectors(mount->Cache, lba + s, 1U, zero_sector);
            if (status != KERNEL_STORAGE_OK)
                return status;
        }

        *out_cluster = c;
        return KERNEL_STORAGE_OK;
    }

    return KERNEL_STORAGE_ERR_NOSPC;
}

// ====== free_cluster_chain_private ======

static int free_cluster_chain_private(const KERNEL_VFS_MOUNT *mount, uint32_t start_cluster)
{
    uint32_t current = start_cluster;
    uint32_t hops = 0;

    while (current >= 2U && current != FAT_CLUSTER_EOF) {
        if (++hops > mount->Geometry.TotalClusters)
            return KERNEL_STORAGE_ERR_CORRUPT;

        uint32_t next = FAT_CLUSTER_EOF;
        int status = vfs_read_next_cluster_private(mount, current, &next);
        if (status != KERNEL_STORAGE_OK)
            return status;

        status = write_fat_entry_private(mount, current, 0U);
        if (status != KERNEL_STORAGE_OK)
            return status;

        current = next;
    }
    return KERNEL_STORAGE_OK;
}

// ====== update_dir_entry_private ======

static int update_dir_entry_private(
    const KERNEL_VFS_MOUNT *mount,
    uint64_t lba,
    uint32_t byte_offset,
    uint32_t first_cluster,
    uint32_t file_size
)
{
    uint8_t sector[512];
    int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
    if (status != KERNEL_STORAGE_OK)
        return status;

    uint8_t *entry = sector + byte_offset;
    uint16_t hi = (uint16_t)((first_cluster >> 16) & 0xFFFFU);
    uint16_t lo = (uint16_t)(first_cluster & 0xFFFFU);
    entry[20] = (uint8_t)(hi & 0xFFU);
    entry[21] = (uint8_t)((hi >> 8) & 0xFFU);
    entry[26] = (uint8_t)(lo & 0xFFU);
    entry[27] = (uint8_t)((lo >> 8) & 0xFFU);
    entry[28] = (uint8_t)(file_size & 0xFFU);
    entry[29] = (uint8_t)((file_size >> 8) & 0xFFU);
    entry[30] = (uint8_t)((file_size >> 16) & 0xFFU);
    entry[31] = (uint8_t)((file_size >> 24) & 0xFFU);

    return kernel_block_cache_write_sectors(mount->Cache, lba, 1U, sector);
}

// ====== split_parent_and_name_private ======

static int split_parent_and_name_private(
    const char *subpath,
    char parent_out[KERNEL_VFS_PATH_CAPACITY],
    char name_out[KERNEL_VFS_NAME_CAPACITY],
    uint32_t *name_len_out
)
{
    uint32_t len = string_len(subpath, KERNEL_VFS_PATH_CAPACITY);
    if (!len || len >= KERNEL_VFS_PATH_CAPACITY || subpath[0] != '/')
        return 0;

    while (len > 1U && subpath[len - 1U] == '/')
        --len;
    if (len == 1U)
        return 0;

    uint32_t last_slash = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (subpath[i] == '/')
            last_slash = i;
    }

    uint32_t name_len = len - last_slash - 1U;
    if (!name_len || name_len >= KERNEL_VFS_NAME_CAPACITY)
        return 0;

    if (last_slash == 0U) {
        parent_out[0] = '/';
        parent_out[1] = '\0';
    } else {
        copy_bytes(parent_out, subpath, last_slash);
        parent_out[last_slash] = '\0';
    }

    copy_bytes(name_out, subpath + last_slash + 1U, name_len);
    name_out[name_len] = '\0';
    *name_len_out = name_len;
    return 1;
}

// ====== dir_has_short_name_private ======

static int dir_has_short_name_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t dir_cluster,
    const uint8_t short_11[11],
    int *found_out
)
{
    *found_out = 0;
    uint8_t sector[512];

    if (dir_cluster == 0U && !mount->Geometry.IsFat32) {
        for (uint32_t s = 0; s < mount->Geometry.RootDirSectors; ++s) {
            uint64_t lba = mount->Geometry.RootDirLba + s;
            int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
            if (status != KERNEL_STORAGE_OK)
                return status;
            for (uint32_t off = 0; off < 512U; off += 32U) {
                uint8_t first = sector[off];
                if (first == 0x00U)
                    return KERNEL_STORAGE_OK;
                if (first == 0xE5U || sector[off + 11U] == FAT_ATTR_LONG_NAME)
                    continue;
                int eq = 1;
                for (uint32_t k = 0; k < 11U; ++k) {
                    if (sector[off + k] != short_11[k]) {
                        eq = 0;
                        break;
                    }
                }
                if (eq) {
                    *found_out = 1;
                    return KERNEL_STORAGE_OK;
                }
            }
        }
        return KERNEL_STORAGE_OK;
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
            for (uint32_t off = 0; off < 512U; off += 32U) {
                uint8_t first = sector[off];
                if (first == 0x00U)
                    return KERNEL_STORAGE_OK;
                if (first == 0xE5U || sector[off + 11U] == FAT_ATTR_LONG_NAME)
                    continue;
                int eq = 1;
                for (uint32_t k = 0; k < 11U; ++k) {
                    if (sector[off + k] != short_11[k]) {
                        eq = 0;
                        break;
                    }
                }
                if (eq) {
                    *found_out = 1;
                    return KERNEL_STORAGE_OK;
                }
            }
        }
        uint32_t next = FAT_CLUSTER_EOF;
        int status = vfs_read_next_cluster_private(mount, current, &next);
        if (status != KERNEL_STORAGE_OK)
            return status;
        current = next;
    }
    return KERNEL_STORAGE_OK;
}

// ====== write_slot_private ======

static int write_slot_private(
    const KERNEL_VFS_MOUNT *mount,
    uint64_t lba,
    uint32_t byte_offset,
    const uint8_t entry_32[32]
)
{
    uint8_t sector[512];
    int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
    if (status != KERNEL_STORAGE_OK)
        return status;
    copy_bytes(sector + byte_offset, entry_32, 32U);
    return kernel_block_cache_write_sectors(mount->Cache, lba, 1U, sector);
}

// ====== create_dir_entry_private ======

static int create_dir_entry_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t dir_cluster,
    const char *name,
    uint32_t name_len,
    uint8_t attr,
    uint32_t first_cluster,
    FAT_NODE_LOC *out_node
)
{
    if (!kfat_validate_name((const uint8_t *)name, name_len))
        return KERNEL_STORAGE_ERR_ARG;

    uint8_t name_83[11];
    uint8_t lfn_entries[5 * 32];
    uint32_t lfn_count = 0U;

    if (!kfat_needs_lfn((const uint8_t *)name, name_len)) {
        if (!kfat_encode_83_name((const uint8_t *)name, name_len, name_83))
            return KERNEL_STORAGE_ERR_ARG;
    } else {
        int alias_ok = 0;
        for (uint32_t tail = 1U; tail <= 9999U; ++tail) {
            if (!kfat_build_short_alias((const uint8_t *)name, name_len, tail, name_83))
                return KERNEL_STORAGE_ERR_ARG;
            int exists = 0;
            int st = dir_has_short_name_private(mount, dir_cluster, name_83, &exists);
            if (st != KERNEL_STORAGE_OK)
                return st;
            if (!exists) {
                alias_ok = 1;
                break;
            }
        }
        if (!alias_ok)
            return KERNEL_STORAGE_ERR_NOSPC;
        lfn_count = kfat_build_lfn_entries(
            (const uint8_t *)name,
            name_len,
            name_83,
            lfn_entries,
            5U
        );
        if (!lfn_count)
            return KERNEL_STORAGE_ERR_ARG;
    }

    uint32_t need_slots = lfn_count + 1U;
    uint64_t slot_lba[6];
    uint32_t slot_off[6];
    uint32_t run = 0U;
    uint8_t sector[512];

    if (dir_cluster == 0U && !mount->Geometry.IsFat32) {
        for (uint32_t s = 0; s < mount->Geometry.RootDirSectors && run < need_slots; ++s) {
            uint64_t lba = mount->Geometry.RootDirLba + s;
            int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
            if (status != KERNEL_STORAGE_OK)
                return status;
            for (uint32_t off = 0; off < 512U && run < need_slots; off += 32U) {
                if (sector[off] == 0x00U || sector[off] == 0xE5U) {
                    slot_lba[run] = lba;
                    slot_off[run] = off;
                    ++run;
                } else {
                    run = 0U;
                }
            }
        }
        if (run < need_slots)
            return KERNEL_STORAGE_ERR_NOSPC;
    } else {
        uint32_t current = (dir_cluster == 0U && mount->Geometry.IsFat32)
            ? mount->Geometry.RootCluster
            : dir_cluster;
        uint32_t last_valid = current;
        uint32_t hops = 0;

        while (current >= 2U && current != FAT_CLUSTER_EOF && run < need_slots) {
            if (++hops > mount->Geometry.TotalClusters)
                return KERNEL_STORAGE_ERR_CORRUPT;

            last_valid = current;
            uint64_t cluster_lba = kfat_cluster_to_lba(&mount->Geometry, current);
            if (!cluster_lba)
                return KERNEL_STORAGE_ERR_CORRUPT;

            for (uint32_t s = 0; s < mount->Geometry.SectorsPerCluster && run < need_slots; ++s) {
                uint64_t lba = cluster_lba + s;
                int status = kernel_block_cache_read_sectors(mount->Cache, lba, 1U, sector);
                if (status != KERNEL_STORAGE_OK)
                    return status;

                for (uint32_t off = 0; off < 512U && run < need_slots; off += 32U) {
                    if (sector[off] == 0x00U || sector[off] == 0xE5U) {
                        slot_lba[run] = lba;
                        slot_off[run] = off;
                        ++run;
                    } else {
                        run = 0U;
                    }
                }
            }

            if (run >= need_slots)
                break;

            uint32_t next = FAT_CLUSTER_EOF;
            int status = vfs_read_next_cluster_private(mount, current, &next);
            if (status != KERNEL_STORAGE_OK)
                return status;
            if (next == FAT_CLUSTER_EOF) {
                uint32_t new_cluster = 0;
                status = allocate_cluster_private(mount, last_valid, &new_cluster);
                if (status != KERNEL_STORAGE_OK)
                    return status;
                next = new_cluster;
            }
            current = next;
        }
        if (run < need_slots)
            return KERNEL_STORAGE_ERR_NOSPC;
    }

    for (uint32_t i = 0; i < lfn_count; ++i) {
        int status = write_slot_private(mount, slot_lba[i], slot_off[i], lfn_entries + i * 32U);
        if (status != KERNEL_STORAGE_OK)
            return status;
    }

    uint8_t short_entry[32];
    zero_bytes(short_entry, sizeof(short_entry));
    copy_bytes(short_entry, name_83, 11U);
    short_entry[11] = attr;
    uint16_t hi = (uint16_t)((first_cluster >> 16) & 0xFFFFU);
    uint16_t lo = (uint16_t)(first_cluster & 0xFFFFU);
    short_entry[20] = (uint8_t)(hi & 0xFFU);
    short_entry[21] = (uint8_t)((hi >> 8) & 0xFFU);
    short_entry[26] = (uint8_t)(lo & 0xFFU);
    short_entry[27] = (uint8_t)((lo >> 8) & 0xFFU);

    int status = write_slot_private(mount, slot_lba[lfn_count], slot_off[lfn_count], short_entry);
    if (status != KERNEL_STORAGE_OK)
        return status;

    zero_bytes(out_node, sizeof(*out_node));
    out_node->DirEntryLba = slot_lba[lfn_count];
    out_node->DirEntryByteOffset = slot_off[lfn_count];
    out_node->FirstCluster = first_cluster;
    out_node->Attr = attr;
    out_node->NameLen = name_len;
    copy_bytes(out_node->Name, name, name_len);
    out_node->LfnCount = lfn_count;
    for (uint32_t i = 0; i < lfn_count; ++i) {
        out_node->LfnLba[i] = slot_lba[i];
        out_node->LfnByteOffset[i] = slot_off[i];
    }
    return KERNEL_STORAGE_OK;
}

// ====== delete_dir_entry_private ======

static int delete_dir_entry_private(const KERNEL_VFS_MOUNT *mount, const FAT_NODE_LOC *node)
{
    uint8_t sector[512];
    int status = kernel_block_cache_read_sectors(mount->Cache, node->DirEntryLba, 1U, sector);
    if (status != KERNEL_STORAGE_OK)
        return status;
    sector[node->DirEntryByteOffset] = 0xE5U;
    status = kernel_block_cache_write_sectors(mount->Cache, node->DirEntryLba, 1U, sector);
    if (status != KERNEL_STORAGE_OK)
        return status;

    for (uint32_t i = 0; i < node->LfnCount && i < FAT_MAX_LFN_SLOTS; ++i) {
        status = kernel_block_cache_read_sectors(mount->Cache, node->LfnLba[i], 1U, sector);
        if (status != KERNEL_STORAGE_OK)
            return status;
        sector[node->LfnByteOffset[i]] = 0xE5U;
        status = kernel_block_cache_write_sectors(mount->Cache, node->LfnLba[i], 1U, sector);
        if (status != KERNEL_STORAGE_OK)
            return status;
    }
    return KERNEL_STORAGE_OK;
}

// ====== dir_is_empty_private ======

static int dir_is_empty_private(const KERNEL_VFS_MOUNT *mount, uint32_t dir_cluster, int *empty_out)
{
    *empty_out = 1;
    uint8_t sector[512];
    KERNEL_FAT_LFN_STATE lfn = {0};
    uint32_t current = dir_cluster;
    uint32_t hops = 0;

    while (current >= 2U && current != FAT_CLUSTER_EOF) {
        if (++hops > mount->Geometry.TotalClusters)
            return KERNEL_STORAGE_ERR_CORRUPT;
        uint64_t cluster_lba = kfat_cluster_to_lba(&mount->Geometry, current);
        if (!cluster_lba)
            return KERNEL_STORAGE_ERR_CORRUPT;

        for (uint32_t s = 0; s < mount->Geometry.SectorsPerCluster; ++s) {
            int status = kernel_block_cache_read_sectors(mount->Cache, cluster_lba + s, 1U, sector);
            if (status != KERNEL_STORAGE_OK)
                return status;
            for (uint32_t off = 0; off < 512U; off += 32U) {
                KERNEL_FAT_DIR_ENTRY parsed;
                uint32_t step = kfat_dir_step(sector + off, off, &lfn, &parsed);
                if (step == 0U)
                    return KERNEL_STORAGE_OK;
                if (step == 2U) {
                    *empty_out = 0;
                    return KERNEL_STORAGE_OK;
                }
            }
        }
        uint32_t next = FAT_CLUSTER_EOF;
        int status = vfs_read_next_cluster_private(mount, current, &next);
        if (status != KERNEL_STORAGE_OK)
            return status;
        current = next;
    }
    return KERNEL_STORAGE_OK;
}

// ====== kernel_vfs_write_file ======

int kernel_vfs_write_file(
    KERNEL_VFS *vfs,
    const char *path,
    uint32_t offset,
    const void *buffer,
    uint32_t length,
    int create_if_missing,
    int truncate,
    uint32_t *bytes_written_out
)
{
    if (!vfs || !path || (!buffer && length) || !bytes_written_out)
        return KERNEL_STORAGE_ERR_ARG;

    *bytes_written_out = 0;
    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    const char *subpath = NULL;
    const KERNEL_VFS_MOUNT *mount = vfs_resolve_mount_private(vfs, path, &subpath);
    if (!mount) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }
    if (mount->Cache->Device->ReadOnly) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_READ_ONLY;
    }

    FAT_NODE_LOC node;
    int status = vfs_resolve_path_private(mount, subpath, &node);
    if (status == KERNEL_STORAGE_ERR_NOT_FOUND && create_if_missing) {
        char parent_path[KERNEL_VFS_PATH_CAPACITY];
        char file_name[KERNEL_VFS_NAME_CAPACITY];
        uint32_t name_len = 0;
        if (!split_parent_and_name_private(subpath, parent_path, file_name, &name_len)) {
            vfs_leave_private(&access);
            return KERNEL_STORAGE_ERR_ARG;
        }

        FAT_NODE_LOC parent_node;
        status = vfs_resolve_path_private(mount, parent_path, &parent_node);
        if (status != KERNEL_STORAGE_OK) {
            vfs_leave_private(&access);
            return status;
        }
        if (!(parent_node.Attr & FAT_ATTR_DIRECTORY)) {
            vfs_leave_private(&access);
            return KERNEL_STORAGE_ERR_ARG;
        }

        status = create_dir_entry_private(
            mount,
            parent_node.FirstCluster,
            file_name,
            name_len,
            FAT_ATTR_ARCHIVE,
            0U,
            &node
        );
    }

    if (status != KERNEL_STORAGE_OK) {
        vfs_leave_private(&access);
        return status;
    }
    if ((node.Attr & FAT_ATTR_DIRECTORY) || !node.DirEntryLba) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    if (truncate) {
        // Update directory entry to (0, 0) BEFORE freeing the FAT cluster chain so an I/O
        // failure on the directory sector never leaves a visible entry pointing to free clusters.
        uint32_t old_first = node.FirstCluster;
        status = update_dir_entry_private(
            mount,
            node.DirEntryLba,
            node.DirEntryByteOffset,
            0U,
            0U
        );
        if (status != KERNEL_STORAGE_OK) {
            vfs_leave_private(&access);
            return status;
        }
        node.FirstCluster = 0U;
        node.FileSize = 0U;
        if (old_first >= 2U) {
            status = free_cluster_chain_private(mount, old_first);
            if (status != KERNEL_STORAGE_OK) {
                vfs_leave_private(&access);
                return status;
            }
        }
    }

    if (offset > node.FileSize) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_RANGE;
    }
    if (length == 0U) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_OK;
    }

    if (node.FirstCluster < 2U) {
        uint32_t first = 0;
        status = allocate_cluster_private(mount, 0U, &first);
        if (status != KERNEL_STORAGE_OK) {
            vfs_leave_private(&access);
            return status;
        }
        node.FirstCluster = first;
    }

    uint32_t cluster_bytes = mount->Geometry.SectorsPerCluster * 512U;
    uint32_t current_cluster = node.FirstCluster;
    uint32_t skip_clusters = offset / cluster_bytes;
    uint32_t in_cluster_offset = offset % cluster_bytes;
    uint32_t hops = 0;

    while (skip_clusters > 0U) {
        if (++hops > mount->Geometry.TotalClusters) {
            vfs_leave_private(&access);
            return KERNEL_STORAGE_ERR_CORRUPT;
        }
        uint32_t next = FAT_CLUSTER_EOF;
        status = vfs_read_next_cluster_private(mount, current_cluster, &next);
        if (status != KERNEL_STORAGE_OK) {
            vfs_leave_private(&access);
            return status;
        }
        if (next == FAT_CLUSTER_EOF) {
            status = allocate_cluster_private(mount, current_cluster, &next);
            if (status != KERNEL_STORAGE_OK) {
                vfs_leave_private(&access);
                return status;
            }
        }
        current_cluster = next;
        --skip_clusters;
    }

    const uint8_t *source = (const uint8_t *)buffer;
    uint32_t remaining = length;

    while (remaining > 0U) {
        if (++hops > mount->Geometry.TotalClusters) {
            vfs_leave_private(&access);
            return KERNEL_STORAGE_ERR_CORRUPT;
        }

        uint64_t cluster_lba = kfat_cluster_to_lba(&mount->Geometry, current_cluster);
        if (!cluster_lba) {
            vfs_leave_private(&access);
            return KERNEL_STORAGE_ERR_CORRUPT;
        }

        uint32_t take = cluster_bytes - in_cluster_offset;
        if (take > remaining)
            take = remaining;

        uint64_t byte_pos = cluster_lba * 512ULL + in_cluster_offset;
        status = kernel_block_cache_write_bytes(mount->Cache, byte_pos, take, source);
        if (status != KERNEL_STORAGE_OK) {
            vfs_leave_private(&access);
            return status;
        }

        source += take;
        *bytes_written_out += take;
        remaining -= take;
        in_cluster_offset = 0U;

        if (remaining > 0U) {
            uint32_t next = FAT_CLUSTER_EOF;
            status = vfs_read_next_cluster_private(mount, current_cluster, &next);
            if (status != KERNEL_STORAGE_OK) {
                vfs_leave_private(&access);
                return status;
            }
            if (next == FAT_CLUSTER_EOF) {
                status = allocate_cluster_private(mount, current_cluster, &next);
                if (status != KERNEL_STORAGE_OK) {
                    vfs_leave_private(&access);
                    return status;
                }
            }
            current_cluster = next;
        }
    }

    uint32_t new_end = offset + length;
    if (new_end > node.FileSize)
        node.FileSize = new_end;

    status = update_dir_entry_private(
        mount,
        node.DirEntryLba,
        node.DirEntryByteOffset,
        node.FirstCluster,
        node.FileSize
    );

    vfs_leave_private(&access);
    return status;
}

// ====== kernel_vfs_unlink ======

int kernel_vfs_unlink(KERNEL_VFS *vfs, const char *path)
{
    if (!vfs || !path)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    const char *subpath = NULL;
    const KERNEL_VFS_MOUNT *mount = vfs_resolve_mount_private(vfs, path, &subpath);
    if (!mount) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }
    if (mount->Cache->Device->ReadOnly) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_READ_ONLY;
    }

    FAT_NODE_LOC node;
    int status = vfs_resolve_path_private(mount, subpath, &node);
    if (status != KERNEL_STORAGE_OK) {
        vfs_leave_private(&access);
        return status;
    }
    if ((node.Attr & FAT_ATTR_DIRECTORY) || !node.DirEntryLba) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    // Mark the directory entry (and any preceding LFN entries) deleted (0xE5) on disk BEFORE freeing
    // the FAT cluster chain so a directory write error never leaves a visible file referencing freed clusters.
    uint32_t old_first = node.FirstCluster;
    status = delete_dir_entry_private(mount, &node);
    if (status == KERNEL_STORAGE_OK && old_first >= 2U) {
        status = free_cluster_chain_private(mount, old_first);
    }

    vfs_leave_private(&access);
    return status;
}

// ====== kernel_vfs_mkdir ======

// Microsoft FAT32 File System Specification v1.03, §6.5: "." points to self, ".." points to parent (0 if root).
int kernel_vfs_mkdir(KERNEL_VFS *vfs, const char *path)
{
    if (!vfs || !path)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    const char *subpath = NULL;
    const KERNEL_VFS_MOUNT *mount = vfs_resolve_mount_private(vfs, path, &subpath);
    if (!mount) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }
    if (mount->Cache->Device->ReadOnly) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_READ_ONLY;
    }

    FAT_NODE_LOC existing;
    int status = vfs_resolve_path_private(mount, subpath, &existing);
    if (status == KERNEL_STORAGE_OK) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }
    if (status != KERNEL_STORAGE_ERR_NOT_FOUND) {
        vfs_leave_private(&access);
        return status;
    }

    char parent_path[KERNEL_VFS_PATH_CAPACITY];
    char dir_name[KERNEL_VFS_NAME_CAPACITY];
    uint32_t name_len = 0;
    if (!split_parent_and_name_private(subpath, parent_path, dir_name, &name_len)) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    FAT_NODE_LOC parent_node;
    status = vfs_resolve_path_private(mount, parent_path, &parent_node);
    if (status != KERNEL_STORAGE_OK) {
        vfs_leave_private(&access);
        return status;
    }
    if (!(parent_node.Attr & FAT_ATTR_DIRECTORY)) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    uint32_t new_cluster = 0U;
    status = allocate_cluster_private(mount, 0U, &new_cluster);
    if (status != KERNEL_STORAGE_OK) {
        vfs_leave_private(&access);
        return status;
    }

    uint64_t new_lba = kfat_cluster_to_lba(&mount->Geometry, new_cluster);
    if (!new_lba) {
        (void)free_cluster_chain_private(mount, new_cluster);
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_CORRUPT;
    }

    uint8_t sector[512];
    zero_bytes(sector, sizeof(sector));

    // "." entry
    for (uint32_t k = 0; k < 11U; ++k)
        sector[k] = ' ';
    sector[0] = '.';
    sector[11] = FAT_ATTR_DIRECTORY;
    sector[20] = (uint8_t)((new_cluster >> 16) & 0xFFU);
    sector[21] = (uint8_t)((new_cluster >> 24) & 0xFFU);
    sector[26] = (uint8_t)(new_cluster & 0xFFU);
    sector[27] = (uint8_t)((new_cluster >> 8) & 0xFFU);

    // ".." entry (cluster 0 when parent is root)
    uint32_t dotdot_cluster = parent_node.DirEntryLba == 0ULL ? 0U : parent_node.FirstCluster;
    for (uint32_t k = 0; k < 11U; ++k)
        sector[32U + k] = ' ';
    sector[32] = '.';
    sector[33] = '.';
    sector[32 + 11] = FAT_ATTR_DIRECTORY;
    sector[32 + 20] = (uint8_t)((dotdot_cluster >> 16) & 0xFFU);
    sector[32 + 21] = (uint8_t)((dotdot_cluster >> 24) & 0xFFU);
    sector[32 + 26] = (uint8_t)(dotdot_cluster & 0xFFU);
    sector[32 + 27] = (uint8_t)((dotdot_cluster >> 8) & 0xFFU);

    status = kernel_block_cache_write_sectors(mount->Cache, new_lba, 1U, sector);
    if (status != KERNEL_STORAGE_OK) {
        (void)free_cluster_chain_private(mount, new_cluster);
        vfs_leave_private(&access);
        return status;
    }

    FAT_NODE_LOC created;
    status = create_dir_entry_private(
        mount,
        parent_node.FirstCluster,
        dir_name,
        name_len,
        FAT_ATTR_DIRECTORY,
        new_cluster,
        &created
    );
    if (status != KERNEL_STORAGE_OK)
        (void)free_cluster_chain_private(mount, new_cluster);

    vfs_leave_private(&access);
    return status;
}

// ====== kernel_vfs_rmdir ======

int kernel_vfs_rmdir(KERNEL_VFS *vfs, const char *path)
{
    if (!vfs || !path)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    const char *subpath = NULL;
    const KERNEL_VFS_MOUNT *mount = vfs_resolve_mount_private(vfs, path, &subpath);
    if (!mount) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_NOT_FOUND;
    }
    if (mount->Cache->Device->ReadOnly) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_READ_ONLY;
    }

    FAT_NODE_LOC node;
    int status = vfs_resolve_path_private(mount, subpath, &node);
    if (status != KERNEL_STORAGE_OK) {
        vfs_leave_private(&access);
        return status;
    }
    if (!(node.Attr & FAT_ATTR_DIRECTORY) || !node.DirEntryLba) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    int is_empty = 0;
    status = dir_is_empty_private(mount, node.FirstCluster, &is_empty);
    if (status != KERNEL_STORAGE_OK) {
        vfs_leave_private(&access);
        return status;
    }
    if (!is_empty) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    uint32_t old_first = node.FirstCluster;
    status = delete_dir_entry_private(mount, &node);
    if (status == KERNEL_STORAGE_OK && old_first >= 2U)
        status = free_cluster_chain_private(mount, old_first);

    vfs_leave_private(&access);
    return status;
}

// ====== grow_open_files_private ======

static int grow_open_files_private(KERNEL_VFS *vfs)
{
    if (vfs->OpenCapacity >= KERNEL_VFS_MAX_OPEN_FILES)
        return 0;
    uint32_t next_cap = vfs->OpenCapacity * 2U;
    if (next_cap > KERNEL_VFS_MAX_OPEN_FILES)
        next_cap = KERNEL_VFS_MAX_OPEN_FILES;

    KERNEL_VFS_OPEN_FILE *next = (KERNEL_VFS_OPEN_FILE *)kernel_heap_alloc(
        vfs->Heap,
        (uint64_t)next_cap * sizeof(KERNEL_VFS_OPEN_FILE),
        16
    );
    if (!next)
        return 0;

    copy_bytes(next, vfs->OpenFiles, (size_t)vfs->OpenCapacity * sizeof(KERNEL_VFS_OPEN_FILE));
    (void)kernel_heap_free(vfs->Heap, vfs->OpenFiles);
    vfs->OpenFiles = next;
    vfs->OpenCapacity = next_cap;
    return 1;
}

// ====== kernel_vfs_open ======

int kernel_vfs_open(
    KERNEL_VFS *vfs,
    uint64_t owner_pid,
    const char *path,
    uint32_t flags,
    uint32_t *fd_out
)
{
    if (!vfs || !path || !fd_out)
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t plen = string_len(path, KERNEL_VFS_PATH_CAPACITY);
    if (!plen || plen >= KERNEL_VFS_PATH_CAPACITY || path[0] != '/')
        return KERNEL_STORAGE_ERR_ARG;

    int create = (flags & KERNEL_VFS_O_CREAT) != 0U;
    int trunc = (flags & KERNEL_VFS_O_TRUNC) != 0U;
    uint32_t mode = flags & 3U;

    if (create || trunc) {
        if (mode == KERNEL_VFS_O_RDONLY)
            return KERNEL_STORAGE_ERR_ARG;
        uint32_t dummy = 0;
        int wstatus = kernel_vfs_write_file(vfs, path, 0U, NULL, 0U, create, trunc, &dummy);
        if (wstatus != KERNEL_STORAGE_OK)
            return wstatus;
    } else {
        KERNEL_VFS_STAT st;
        int sstatus = kernel_vfs_stat(vfs, path, &st);
        if (sstatus != KERNEL_STORAGE_OK)
            return sstatus;
        if (st.IsDirectory)
            return KERNEL_STORAGE_ERR_ARG;
    }

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t slot = UINT32_MAX;
    for (uint32_t i = 0; i < vfs->OpenCapacity; ++i) {
        if (!vfs->OpenFiles[i].InUse) {
            slot = i;
            break;
        }
    }
    if (slot == UINT32_MAX) {
        uint32_t old_cap = vfs->OpenCapacity;
        if (!grow_open_files_private(vfs)) {
            vfs_leave_private(&access);
            return KERNEL_STORAGE_ERR_NOMEM;
        }
        slot = old_cap;
    }

    KERNEL_VFS_OPEN_FILE *f = &vfs->OpenFiles[slot];
    zero_bytes(f, sizeof(*f));
    f->InUse = 1U;
    f->Flags = flags;
    f->Position = 0U;
    f->OwnerPid = owner_pid;
    copy_bytes(f->Path, path, plen);
    f->Path[plen] = '\0';
    ++vfs->OpenCount;
    *fd_out = slot + 1U;

    vfs_leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_vfs_read ======

int kernel_vfs_read(
    KERNEL_VFS *vfs,
    uint64_t owner_pid,
    uint32_t fd,
    void *buffer,
    uint32_t length,
    uint32_t *bytes_read_out
)
{
    if (!vfs || !fd || (!buffer && length) || !bytes_read_out)
        return KERNEL_STORAGE_ERR_ARG;

    char path_copy[KERNEL_VFS_PATH_CAPACITY];
    uint32_t pos = 0;

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t slot = fd - 1U;
    if (slot >= vfs->OpenCapacity || !vfs->OpenFiles[slot].InUse ||
        vfs->OpenFiles[slot].OwnerPid != owner_pid ||
        (vfs->OpenFiles[slot].Flags & 3U) == KERNEL_VFS_O_WRONLY) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }
    pos = vfs->OpenFiles[slot].Position;
    copy_bytes(path_copy, vfs->OpenFiles[slot].Path, KERNEL_VFS_PATH_CAPACITY);
    vfs_leave_private(&access);

    int status = kernel_vfs_read_file(vfs, path_copy, pos, buffer, length, bytes_read_out);
    if (status == KERNEL_STORAGE_OK && *bytes_read_out > 0U) {
        if (vfs_enter_private(vfs, &access)) {
            if (slot < vfs->OpenCapacity && vfs->OpenFiles[slot].InUse &&
                vfs->OpenFiles[slot].OwnerPid == owner_pid)
                vfs->OpenFiles[slot].Position = pos + *bytes_read_out;
            vfs_leave_private(&access);
        }
    }
    return status;
}

// ====== kernel_vfs_write ======

int kernel_vfs_write(
    KERNEL_VFS *vfs,
    uint64_t owner_pid,
    uint32_t fd,
    const void *buffer,
    uint32_t length,
    uint32_t *bytes_written_out
)
{
    if (!vfs || !fd || (!buffer && length) || !bytes_written_out)
        return KERNEL_STORAGE_ERR_ARG;

    char path_copy[KERNEL_VFS_PATH_CAPACITY];
    uint32_t pos = 0;

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t slot = fd - 1U;
    if (slot >= vfs->OpenCapacity || !vfs->OpenFiles[slot].InUse ||
        vfs->OpenFiles[slot].OwnerPid != owner_pid ||
        (vfs->OpenFiles[slot].Flags & 3U) == KERNEL_VFS_O_RDONLY) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }
    pos = vfs->OpenFiles[slot].Position;
    copy_bytes(path_copy, vfs->OpenFiles[slot].Path, KERNEL_VFS_PATH_CAPACITY);
    vfs_leave_private(&access);

    int status = kernel_vfs_write_file(vfs, path_copy, pos, buffer, length, 0, 0, bytes_written_out);
    if (status == KERNEL_STORAGE_OK && *bytes_written_out > 0U) {
        if (vfs_enter_private(vfs, &access)) {
            if (slot < vfs->OpenCapacity && vfs->OpenFiles[slot].InUse &&
                vfs->OpenFiles[slot].OwnerPid == owner_pid)
                vfs->OpenFiles[slot].Position = pos + *bytes_written_out;
            vfs_leave_private(&access);
        }
    }
    return status;
}

// ====== kernel_vfs_close ======

int kernel_vfs_close(KERNEL_VFS *vfs, uint64_t owner_pid, uint32_t fd)
{
    if (!vfs || !fd)
        return KERNEL_STORAGE_ERR_ARG;

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return KERNEL_STORAGE_ERR_ARG;

    uint32_t slot = fd - 1U;
    if (slot >= vfs->OpenCapacity || !vfs->OpenFiles[slot].InUse ||
        vfs->OpenFiles[slot].OwnerPid != owner_pid) {
        vfs_leave_private(&access);
        return KERNEL_STORAGE_ERR_ARG;
    }

    zero_bytes(&vfs->OpenFiles[slot], sizeof(vfs->OpenFiles[slot]));
    --vfs->OpenCount;
    vfs_leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_vfs_close_owned ======

void kernel_vfs_close_owned(KERNEL_VFS *vfs, uint64_t owner_pid)
{
    if (!vfs)
        return;

    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return;

    for (uint32_t i = 0; i < vfs->OpenCapacity; ++i) {
        if (vfs->OpenFiles[i].InUse && vfs->OpenFiles[i].OwnerPid == owner_pid) {
            zero_bytes(&vfs->OpenFiles[i], sizeof(vfs->OpenFiles[i]));
            --vfs->OpenCount;
        }
    }
    vfs_leave_private(&access);
}

// ====== kernel_vfs_count_owned ======

uint32_t kernel_vfs_count_owned(KERNEL_VFS *vfs, uint64_t owner_pid)
{
    if (!vfs || !owner_pid)
        return 0U;
    VFS_ACCESS access;
    if (!vfs_enter_private(vfs, &access))
        return 0U;
    uint32_t count = 0U;
    for (uint32_t i = 0; i < vfs->OpenCapacity; ++i) {
        if (vfs->OpenFiles[i].InUse && vfs->OpenFiles[i].OwnerPid == owner_pid) {
            ++count;
        }
    }
    vfs_leave_private(&access);
    return count;
}

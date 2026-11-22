#ifndef OS_KERNEL_VFS_H
#define OS_KERNEL_VFS_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/mm/heap/heap.h"
#include "kernel/storage/cache.h"
#include "kernel/sync/lock.h"

// ====== vfs_and_fat_contract ======

#define KFAT_OK                    0U
#define KFAT_ERR_CORRUPT           1U
#define KFAT_ERR_NOT_FOUND         2U
#define KFAT_ERR_EOF               3U

#define FAT_CLUSTER_EOF            0x0FFFFFFFU
#define FAT_CLUSTER_BAD            0x0FFFFFF7U

#define FAT_ATTR_READ_ONLY         0x01U
#define FAT_ATTR_HIDDEN            0x02U
#define FAT_ATTR_SYSTEM            0x04U
#define FAT_ATTR_VOLUME_ID         0x08U
#define FAT_ATTR_DIRECTORY         0x10U
#define FAT_ATTR_ARCHIVE           0x20U
#define FAT_ATTR_LONG_NAME         0x0FU

#define KERNEL_VFS_MAX_MOUNTS      8U
#define KERNEL_VFS_NAME_CAPACITY   64U
#define KERNEL_VFS_PATH_CAPACITY   256U
#define KERNEL_VFS_MAX_OPEN_FILES  256U
#define KERNEL_VFS_INIT_OPEN_FILES 16U

#define KERNEL_VFS_O_RDONLY        0U
#define KERNEL_VFS_O_WRONLY        1U
#define KERNEL_VFS_O_RDWR          2U
#define KERNEL_VFS_O_CREAT         (1U << 8)
#define KERNEL_VFS_O_TRUNC         (1U << 9)

typedef struct {
    uint64_t  PartitionLba;
    uint64_t  TotalSectors;
    uint64_t  FatStartLba;
    uint32_t  FatSectors;
    uint32_t  NumFats;
    uint64_t  RootDirLba;
    uint32_t  RootDirSectors;
    uint32_t  RootDirEntries;
    uint64_t  DataStartLba;
    uint32_t  TotalClusters;
    uint32_t  SectorsPerCluster;
    uint32_t  RootCluster;
    uint32_t  IsFat32;
    uint32_t  Reserved;
} KERNEL_FAT_GEOMETRY;

typedef struct {
    uint8_t   Name[64];
    uint32_t  NameLen;
    uint32_t  Attr;
    uint32_t  FirstCluster;
    uint32_t  FileSize;
    uint32_t  EntryOffset;
} KERNEL_FAT_DIR_ENTRY;

typedef struct {
    uint8_t   Buffer[64];
    uint32_t  Active;
} KERNEL_FAT_LFN_STATE;

_Static_assert(sizeof(KERNEL_FAT_GEOMETRY) == 80, "FAT geometry ABI");
_Static_assert(sizeof(KERNEL_FAT_DIR_ENTRY) == 84, "FAT dir entry ABI");
_Static_assert(sizeof(KERNEL_FAT_LFN_STATE) == 68, "FAT LFN state ABI");

typedef struct {
    char      Name[KERNEL_VFS_NAME_CAPACITY];
    uint32_t  NameLen;
    uint32_t  IsDirectory;
    uint32_t  Size;
    uint32_t  FirstCluster;
} KERNEL_VFS_STAT;

typedef struct {
    char                 Prefix[32];
    uint32_t             PrefixLen;
    uint32_t             Active;
    KERNEL_BLOCK_CACHE  *Cache;
    KERNEL_FAT_GEOMETRY  Geometry;
} KERNEL_VFS_MOUNT;

typedef struct {
    uint32_t  InUse;
    uint32_t  MountIndex;
    uint32_t  Flags;
    uint32_t  Position;
    uint64_t  OwnerPid;
    char      Path[KERNEL_VFS_PATH_CAPACITY];
} KERNEL_VFS_OPEN_FILE;

typedef struct {
    uintptr_t              Self;
    KERNEL_HEAP           *Heap;
    KERNEL_VFS_MOUNT       Mounts[KERNEL_VFS_MAX_MOUNTS];
    uint32_t               MountCount;
    uint32_t               Ready;
    KERNEL_VFS_OPEN_FILE  *OpenFiles;
    uint32_t               OpenCapacity;
    uint32_t               OpenCount;
    KERNEL_LOCK            Lock;
} KERNEL_VFS;

uint32_t kfat_detect_partition_lba(const uint8_t *sector0, uint64_t disk_sectors, uint64_t *lba_out);
uint32_t kfat_parse_bpb(
    const uint8_t *bpb_sector,
    uint64_t partition_lba,
    uint64_t disk_sectors,
    KERNEL_FAT_GEOMETRY *out_geom
);
uint64_t kfat_cluster_to_lba(const KERNEL_FAT_GEOMETRY *geom, uint32_t cluster);
uint32_t kfat_fat_slot(
    const KERNEL_FAT_GEOMETRY *geom,
    uint32_t cluster,
    uint64_t *sector_lba_out,
    uint32_t *byte_offset_out
);
uint32_t kfat_decode_next_cluster(const KERNEL_FAT_GEOMETRY *geom, uint32_t raw_value);
uint32_t kfat_dir_step(
    const uint8_t *entry_32,
    uint32_t entry_offset,
    KERNEL_FAT_LFN_STATE *lfn,
    KERNEL_FAT_DIR_ENTRY *out_entry
);
uint32_t kfat_entry_matches_name(
    const uint8_t *entry_32,
    const KERNEL_FAT_DIR_ENTRY *parsed,
    const uint8_t *target,
    uint32_t target_len
);
uint32_t kfat_encode_83_name(const uint8_t *name, uint32_t name_len, uint8_t *out_11);

int  kernel_vfs_init(KERNEL_VFS *vfs, KERNEL_HEAP *heap);
void kernel_vfs_destroy(KERNEL_VFS *vfs);
int  kernel_vfs_mount_fat(KERNEL_VFS *vfs, const char *prefix, KERNEL_BLOCK_CACHE *cache);
int  kernel_vfs_stat(KERNEL_VFS *vfs, const char *path, KERNEL_VFS_STAT *out);
int  kernel_vfs_readdir(KERNEL_VFS *vfs, const char *dir_path, uint32_t index, KERNEL_VFS_STAT *out);
int  kernel_vfs_read_file(
    KERNEL_VFS *vfs,
    const char *path,
    uint32_t offset,
    void *buffer,
    uint32_t length,
    uint32_t *bytes_read_out
);
int  kernel_vfs_sync(KERNEL_VFS *vfs);

KERNEL_VFS *kernel_vfs(void);
int kernel_vfs_system_start(
    KERNEL_HEAP *heap,
    KERNEL_BLOCK_CACHE *root_cache,
    KERNEL_BLOCK_CACHE *boot_cache
);

#endif

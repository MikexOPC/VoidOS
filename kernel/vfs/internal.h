#ifndef OS_KERNEL_VFS_INTERNAL_H
#define OS_KERNEL_VFS_INTERNAL_H

#include "vfs.h"
#include "kernel/sync/internal.h"

typedef struct {
    KERNEL_VFS  *Vfs;
} VFS_ACCESS;

#define FAT_MAX_LFN_SLOTS 20U

typedef struct {
    uint64_t  DirEntryLba;
    uint32_t  DirEntryByteOffset;
    uint32_t  FirstCluster;
    uint32_t  FileSize;
    uint32_t  Attr;
    char      Name[KERNEL_VFS_NAME_CAPACITY];
    uint32_t  NameLen;
    uint32_t  LfnCount;
    uint64_t  LfnLba[FAT_MAX_LFN_SLOTS];
    uint32_t  LfnByteOffset[FAT_MAX_LFN_SLOTS];
} FAT_NODE_LOC;

int  vfs_enter_private(KERNEL_VFS *vfs, VFS_ACCESS *access);
void vfs_leave_private(VFS_ACCESS *access);

const KERNEL_VFS_MOUNT *vfs_resolve_mount_private(
    const KERNEL_VFS *vfs,
    const char *path,
    const char **subpath_out
);

int vfs_read_next_cluster_private(
    const KERNEL_VFS_MOUNT *mount,
    uint32_t cluster,
    uint32_t *next_out
);

int vfs_resolve_path_private(
    const KERNEL_VFS_MOUNT *mount,
    const char *subpath,
    FAT_NODE_LOC *out_node
);

#endif

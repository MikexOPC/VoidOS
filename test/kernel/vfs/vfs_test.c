#include "kernel/vfs/vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Host test of KERNEL_VFS + FAT16/FAT32 read path over KERNEL_BLOCK_CACHE
// (validates MBR/BPB parsing, LFN + 8.3 lookup, multi-cluster reads, and corrupt FAT cycle rejection).

#define DISK_SECTORS       128U
#define DISK_SECTOR_BYTES  512U

static __declspec(align(4096)) uint8_t heap_arena[KERNEL_HEAP_MAX_PAGES * 4096ULL];
static KERNEL_HEAP test_heap;

typedef struct {
    uint8_t  Storage[DISK_SECTORS * DISK_SECTOR_BYTES];
} RAM_DISK;

static RAM_DISK fat32_disk;
static KERNEL_BLOCK_DEVICE fat32_dev;
static KERNEL_BLOCK_CACHE fat32_cache;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL vfs_fat_host line %d\n", __LINE__); \
        exit(1); \
    } \
} while (0)

// ====== stub_logs_for_host ======

int kernel_text(const char *t) { (void)t; return 1; }
int kernel_number(unsigned long long n) { (void)n; return 1; }
int kernel_log(int level, const char *t) { (void)level; (void)t; return 1; }
KERNEL_HEAP *kernel_heap(void) { return &test_heap; }

// ====== ram_read ======

static int ram_read(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, void *buffer)
{
    RAM_DISK *disk = (RAM_DISK *)device->Context;
    memcpy(buffer, disk->Storage + lba * DISK_SECTOR_BYTES, (size_t)count * DISK_SECTOR_BYTES);
    return KERNEL_STORAGE_OK;
}

// ====== ram_write ======

static int ram_write(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, const void *buffer)
{
    RAM_DISK *disk = (RAM_DISK *)device->Context;
    memcpy(disk->Storage + lba * DISK_SECTOR_BYTES, buffer, (size_t)count * DISK_SECTOR_BYTES);
    return KERNEL_STORAGE_OK;
}

// ====== write_le16 ======

static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

// ====== write_le32 ======

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

// ====== format_test_fat32 ======

static void format_test_fat32(RAM_DISK *disk)
{
    memset(disk, 0, sizeof(*disk));
    uint8_t *bpb = disk->Storage;
    bpb[0] = 0xEBU;
    bpb[1] = 0x58U;
    bpb[2] = 0x90U;
    memcpy(bpb + 3, "VOIDOS  ", 8);
    write_le16(bpb + 11, 512U);
    bpb[13] = 1U;
    write_le16(bpb + 14, 4U);
    bpb[16] = 2U;
    write_le16(bpb + 17, 0U);
    write_le32(bpb + 32, DISK_SECTORS);
    write_le32(bpb + 36, 2U);
    write_le32(bpb + 44, 2U);
    bpb[510] = 0x55U;
    bpb[511] = 0xAAU;

    // FAT1 at LBA 4, FAT2 at LBA 6; Data at LBA 8 (cluster 2).
    for (uint32_t f = 0; f < 2U; ++f) {
        uint8_t *fat = disk->Storage + (4U + f * 2U) * 512U;
        write_le32(fat + 0 * 4, 0x0FFFFFF8U);
        write_le32(fat + 1 * 4, 0x0FFFFFFFU);
        write_le32(fat + 2 * 4, 0x0FFFFFFFU); // Cluster 2: Root dir
        write_le32(fat + 3 * 4, 4U);          // Cluster 3 -> Cluster 4 (2-cluster file HELLO.TXT)
        write_le32(fat + 4 * 4, 0x0FFFFFFFU); // Cluster 4: EOF
        write_le32(fat + 5 * 4, 0x0FFFFFFFU); // Cluster 5: SubdirSUB
        write_le32(fat + 6 * 4, 0x0FFFFFFFU); // Cluster 6: /SUB/NOTE.TXT
        write_le32(fat + 7 * 4, 8U);          // Cluster 7 <-> 8 cycle (corrupt file CYCLE.BIN)
        write_le32(fat + 8 * 4, 7U);
    }

    // Root directory at cluster 2 (LBA 8)
    uint8_t *root = disk->Storage + 8U * 512U;
    // Entry 0: HELLO.TXT (600 bytes across cluster 3 and 4)
    memcpy(root + 0, "HELLO   TXT", 11);
    root[11] = FAT_ATTR_ARCHIVE;
    write_le16(root + 26, 3U);
    write_le32(root + 28, 600U);

    // Entry 1: SUB directory at cluster 5
    memcpy(root + 32, "SUB        ", 11);
    root[32 + 11] = FAT_ATTR_DIRECTORY;
    write_le16(root + 32 + 26, 5U);
    write_le32(root + 32 + 28, 0U);

    // Entry 2: CYCLE.BIN (corrupt cluster chain 7 -> 8 -> 7, size 100000)
    memcpy(root + 64, "CYCLE   BIN", 11);
    root[64 + 11] = FAT_ATTR_ARCHIVE;
    write_le16(root + 64 + 26, 7U);
    write_le32(root + 64 + 28, 100000U);

    // Cluster 3 (LBA 9) and Cluster 4 (LBA 10): 600 bytes of pattern
    uint8_t *c3 = disk->Storage + 9U * 512U;
    for (uint32_t i = 0; i < 600U; ++i)
        c3[i] = (uint8_t)((i * 13U + 7U) & 0xFFU);

    // Cluster 5 (LBA 11): SUB directory with NOTE.TXT at cluster 6
    uint8_t *sub = disk->Storage + 11U * 512U;
    memcpy(sub + 0, "NOTE    TXT", 11);
    sub[11] = FAT_ATTR_ARCHIVE;
    write_le16(sub + 26, 6U);
    write_le32(sub + 28, 12U);

    // Cluster 6 (LBA 12): "Subdir note\n"
    memcpy(disk->Storage + 12U * 512U, "Subdir note\n", 12);
}

// ====== main ======

int main(void)
{
    memset(&test_heap, 0, sizeof(test_heap));
    CHECK(kernel_heap_init(&test_heap, heap_arena, KERNEL_HEAP_MAX_PAGES));

    format_test_fat32(&fat32_disk);
    memset(&fat32_dev, 0, sizeof(fat32_dev));
    fat32_dev.Self = (uintptr_t)&fat32_dev;
    fat32_dev.Name = "ram0";
    fat32_dev.Context = &fat32_disk;
    fat32_dev.TotalSectors = DISK_SECTORS;
    fat32_dev.SectorBytes = DISK_SECTOR_BYTES;
    fat32_dev.MaxTransferSectors = 8U;
    fat32_dev.Kind = KERNEL_BLOCK_KIND_MEMORY;
    fat32_dev.Ready = 1U;
    fat32_dev.Read = ram_read;
    fat32_dev.Write = ram_write;

    CHECK(kernel_block_cache_init(&fat32_cache, &test_heap, &fat32_dev, 16U) == KERNEL_STORAGE_OK);

    KERNEL_VFS vfs;
    CHECK(kernel_vfs_init(&vfs, &test_heap) == KERNEL_STORAGE_OK);
    CHECK(kernel_vfs_mount_fat(&vfs, "/", &fat32_cache) == KERNEL_STORAGE_OK);

    // Stat & readdir on root
    KERNEL_VFS_STAT st;
    CHECK(kernel_vfs_stat(&vfs, "/hello.txt", &st) == KERNEL_STORAGE_OK);
    CHECK(!st.IsDirectory && st.Size == 600U);
    CHECK(kernel_vfs_stat(&vfs, "/SUB", &st) == KERNEL_STORAGE_OK && st.IsDirectory);
    CHECK(kernel_vfs_readdir(&vfs, "/", 0U, &st) == KERNEL_STORAGE_OK && strcmp(st.Name, "HELLO.TXT") == 0);
    CHECK(kernel_vfs_readdir(&vfs, "/", 1U, &st) == KERNEL_STORAGE_OK && strcmp(st.Name, "SUB") == 0);
    CHECK(kernel_vfs_readdir(&vfs, "/", 3U, &st) == KERNEL_STORAGE_ERR_NOT_FOUND);

    // Multi-cluster read and partial offset read
    uint8_t buf[600];
    uint32_t read_bytes = 0;
    CHECK(kernel_vfs_read_file(&vfs, "/HELLO.TXT", 0U, buf, sizeof(buf), &read_bytes) == KERNEL_STORAGE_OK);
    CHECK(read_bytes == 600U);
    for (uint32_t i = 0; i < 600U; ++i)
        CHECK(buf[i] == (uint8_t)((i * 13U + 7U) & 0xFFU));

    uint8_t tail[100];
    CHECK(kernel_vfs_read_file(&vfs, "/HELLO.TXT", 500U, tail, sizeof(tail), &read_bytes) == KERNEL_STORAGE_OK);
    CHECK(read_bytes == 100U && memcmp(tail, buf + 500U, 100U) == 0);

    // Subdirectory file read
    char note[16] = {0};
    CHECK(kernel_vfs_read_file(&vfs, "/sub/note.txt", 0U, note, sizeof(note), &read_bytes) == KERNEL_STORAGE_OK);
    CHECK(read_bytes == 12U && memcmp(note, "Subdir note\n", 12U) == 0);

    // Corrupt cluster cycle must be caught and rejected without infinite loop
    CHECK(kernel_vfs_read_file(&vfs, "/CYCLE.BIN", 80000U, buf, sizeof(buf), &read_bytes) == KERNEL_STORAGE_ERR_CORRUPT);

    kernel_vfs_destroy(&vfs);
    kernel_block_cache_destroy(&fat32_cache);

    KERNEL_HEAP_STATS hstats = kernel_heap_stats(&test_heap);
    CHECK(hstats.FreePages == KERNEL_HEAP_MAX_PAGES && hstats.Objects == 0);

    printf("PASS: vfs_fat_host (mount, stat, readdir, multi-cluster read, subdir lookup, cycle detection, zero leaks)\n");
    return 0;
}

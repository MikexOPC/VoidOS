#include "kernel/vfs/vfs.h"
#include "kernel/sync/internal.h"
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
    uint8_t   Storage[DISK_SECTORS * DISK_SECTOR_BYTES];
    int       FailWriteActive;
    uint64_t  FailWriteLba;
    uint32_t  IoWithIrqEnabled;
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
    CHECK(ks_irq_state() != 0);
    ++disk->IoWithIrqEnabled;
    memcpy(buffer, disk->Storage + lba * DISK_SECTOR_BYTES, (size_t)count * DISK_SECTOR_BYTES);
    return KERNEL_STORAGE_OK;
}

// ====== ram_write ======

static int ram_write(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, const void *buffer)
{
    RAM_DISK *disk = (RAM_DISK *)device->Context;
    CHECK(ks_irq_state() != 0);
    ++disk->IoWithIrqEnabled;
    if (disk->FailWriteActive && disk->FailWriteLba >= lba && disk->FailWriteLba < lba + count) {
        return KERNEL_STORAGE_ERR_IO;
    }
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

    // Create, multi-cluster write (1200 bytes = 3 clusters), cache invalidate, readback, truncate, and unlink
    uint8_t wbuf[1200];
    uint8_t rbuf[1200];
    for (uint32_t i = 0; i < sizeof(wbuf); ++i)
        wbuf[i] = (uint8_t)((i * 29U + 0x41U) & 0xFFU);

    uint32_t written = 0;
    CHECK(kernel_vfs_write_file(&vfs, "/DATA.BIN", 0U, wbuf, sizeof(wbuf), 1, 1, &written) == KERNEL_STORAGE_OK);
    CHECK(written == sizeof(wbuf));
    CHECK(kernel_vfs_sync(&vfs) == KERNEL_STORAGE_OK);
    CHECK(kernel_block_cache_invalidate(&fat32_cache) == KERNEL_STORAGE_OK);

    memset(rbuf, 0, sizeof(rbuf));
    CHECK(kernel_vfs_read_file(&vfs, "/data.bin", 0U, rbuf, sizeof(rbuf), &read_bytes) == KERNEL_STORAGE_OK);
    CHECK(read_bytes == sizeof(wbuf) && memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    // Truncate and overwrite with smaller payload (25 bytes), freeing trailing clusters
    CHECK(kernel_vfs_write_file(&vfs, "/DATA.BIN", 0U, "Short replacement payload", 25U, 0, 1, &written) == KERNEL_STORAGE_OK);
    CHECK(written == 25U);
    CHECK(kernel_vfs_stat(&vfs, "/DATA.BIN", &st) == KERNEL_STORAGE_OK && st.Size == 25U);

    // Open file descriptor table dynamic growth (> 16 open files), read/write via fd, and owner cleanup
    uint32_t fds[24];
    for (uint32_t i = 0; i < 24U; ++i) {
        CHECK(kernel_vfs_open(
            &vfs,
            100ULL,
            "/SUB/FDTEST.TXT",
            KERNEL_VFS_O_RDWR | (i == 0U ? (KERNEL_VFS_O_CREAT | KERNEL_VFS_O_TRUNC) : 0U),
            &fds[i]
        ) == KERNEL_STORAGE_OK);
    }
    CHECK(kernel_vfs_write(&vfs, 100ULL, fds[0], "Hello via FD!", 13U, &written) == KERNEL_STORAGE_OK && written == 13U);
    char fd_read[16] = {0};
    CHECK(kernel_vfs_read(&vfs, 100ULL, fds[1], fd_read, sizeof(fd_read), &read_bytes) == KERNEL_STORAGE_OK);
    CHECK(read_bytes == 13U && memcmp(fd_read, "Hello via FD!", 13U) == 0);
    // Wrong PID must be rejected
    CHECK(kernel_vfs_read(&vfs, 999ULL, fds[1], fd_read, sizeof(fd_read), &read_bytes) == KERNEL_STORAGE_ERR_ARG);
    CHECK(kernel_vfs_close(&vfs, 100ULL, fds[0]) == KERNEL_STORAGE_OK);
    kernel_vfs_close_owned(&vfs, 100ULL);
    CHECK(vfs.OpenCount == 0U && vfs.OpenCapacity == 32U);

    // Unlink both created files
    CHECK(kernel_vfs_unlink(&vfs, "/DATA.BIN") == KERNEL_STORAGE_OK);
    CHECK(kernel_vfs_stat(&vfs, "/DATA.BIN", &st) == KERNEL_STORAGE_ERR_NOT_FOUND);
    CHECK(kernel_vfs_unlink(&vfs, "/SUB/FDTEST.TXT") == KERNEL_STORAGE_OK);
    CHECK(kernel_vfs_stat(&vfs, "/SUB/FDTEST.TXT", &st) == KERNEL_STORAGE_ERR_NOT_FOUND);

    // Fault-injection: directory write failure during truncate and unlink must NOT free FAT clusters.
    uint8_t fault_payload[600];
    for (uint32_t i = 0; i < sizeof(fault_payload); ++i) {
        fault_payload[i] = (uint8_t)('A' + (i % 26U));
    }
    CHECK(kernel_vfs_write_file(&vfs, "/FAULT.TXT", 0U, fault_payload, sizeof(fault_payload), 1, 1, &written) == KERNEL_STORAGE_OK);
    CHECK(written == sizeof(fault_payload));
    CHECK(kernel_vfs_stat(&vfs, "/FAULT.TXT", &st) == KERNEL_STORAGE_OK);
    CHECK(st.Size == sizeof(fault_payload) && st.FirstCluster >= 2U);
    uint32_t first_cluster = st.FirstCluster;

    // 1) Inject write error on root dir sector (LBA 8) during truncate.
    fat32_disk.FailWriteActive = 1;
    fat32_disk.FailWriteLba = 8U;
    CHECK(kernel_vfs_write_file(&vfs, "/FAULT.TXT", 0U, "x", 1U, 0, 1, &written) == KERNEL_STORAGE_ERR_IO);
    fat32_disk.FailWriteActive = 0;
    CHECK(kernel_block_cache_invalidate(&fat32_cache) == KERNEL_STORAGE_OK);

    // FAT cluster must still be allocated and file content intact.
    uint32_t fat_word = 0;
    memcpy(&fat_word, fat32_disk.Storage + 4U * 512U + first_cluster * 4U, 4U);
    CHECK((fat_word & 0x0FFFFFFFU) != 0U);
    uint8_t verify_buf[600];
    memset(verify_buf, 0, sizeof(verify_buf));
    CHECK(kernel_vfs_read_file(&vfs, "/FAULT.TXT", 0U, verify_buf, sizeof(verify_buf), &read_bytes) == KERNEL_STORAGE_OK);
    CHECK(read_bytes == sizeof(fault_payload) && memcmp(verify_buf, fault_payload, sizeof(fault_payload)) == 0);

    // 2) Inject write error on root dir sector (LBA 8) during unlink.
    fat32_disk.FailWriteActive = 1;
    fat32_disk.FailWriteLba = 8U;
    CHECK(kernel_vfs_unlink(&vfs, "/FAULT.TXT") == KERNEL_STORAGE_ERR_IO);
    fat32_disk.FailWriteActive = 0;
    CHECK(kernel_block_cache_invalidate(&fat32_cache) == KERNEL_STORAGE_OK);

    memcpy(&fat_word, fat32_disk.Storage + 4U * 512U + first_cluster * 4U, 4U);
    CHECK((fat_word & 0x0FFFFFFFU) != 0U);
    memset(verify_buf, 0, sizeof(verify_buf));
    CHECK(kernel_vfs_read_file(&vfs, "/FAULT.TXT", 0U, verify_buf, sizeof(verify_buf), &read_bytes) == KERNEL_STORAGE_OK);
    CHECK(read_bytes == sizeof(fault_payload) && memcmp(verify_buf, fault_payload, sizeof(fault_payload)) == 0);

    // 3) Clean unlink succeeds and frees the FAT cluster.
    CHECK(kernel_vfs_unlink(&vfs, "/FAULT.TXT") == KERNEL_STORAGE_OK);
    CHECK(kernel_vfs_stat(&vfs, "/FAULT.TXT", &st) == KERNEL_STORAGE_ERR_NOT_FOUND);
    memcpy(&fat_word, fat32_disk.Storage + 4U * 512U + first_cluster * 4U, 4U);
    CHECK((fat_word & 0x0FFFFFFFU) == 0U);
    CHECK(fat32_disk.IoWithIrqEnabled > 0U);

    // 4) Unmount + remount slot reuse (simulates USB stick hotplug on /usb).
    for (uint32_t cycle = 0; cycle < 10U; ++cycle) {
        CHECK(kernel_vfs_mount_fat(&vfs, "/usb", &fat32_cache) == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_stat(&vfs, "/usb/HELLO.TXT", &st) == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_unmount(&vfs, "/usb") == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_unmount(&vfs, "/usb") == KERNEL_STORAGE_ERR_NOT_FOUND);
        CHECK(kernel_vfs_stat(&vfs, "/usb/HELLO.TXT", &st) == KERNEL_STORAGE_ERR_NOT_FOUND);
    }

    // 7. mkdir, nested LFN subdirectories, LFN files (including alias collisions and 63-char max), unlink & rmdir
    {
        CHECK(kernel_vfs_mkdir(&vfs, "/Documenti") == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_mkdir(&vfs, "/Documenti") == KERNEL_STORAGE_ERR_ARG);
        CHECK(kernel_vfs_stat(&vfs, "/Documenti", &st) == KERNEL_STORAGE_OK);
        CHECK(st.IsDirectory == 1U && strcmp(st.Name, "Documenti") == 0);

        CHECK(kernel_vfs_mkdir(&vfs, "/Documenti/Sotto Cartella") == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_stat(&vfs, "/Documenti/Sotto Cartella", &st) == KERNEL_STORAGE_OK);
        CHECK(st.IsDirectory == 1U && strcmp(st.Name, "Sotto Cartella") == 0);

        const char *p1 = "/Documenti/Sotto Cartella/my_long_config_alpha.json";
        const char *p2 = "/Documenti/Sotto Cartella/my_long_config_beta.json";
        const char *p3 = "/Documenti/Sotto Cartella/file_with_63_chars_abcdefghijklmnopqrstuvwxyz_0123456789_end123";
        uint32_t lfn_written = 0;
        CHECK(kernel_vfs_write_file(&vfs, p1, 0, "{\"a\":1}", 7, 1, 1, &lfn_written) == KERNEL_STORAGE_OK && lfn_written == 7);
        CHECK(kernel_vfs_write_file(&vfs, p2, 0, "{\"b\":2}", 7, 1, 1, &lfn_written) == KERNEL_STORAGE_OK && lfn_written == 7);
        CHECK(kernel_vfs_write_file(&vfs, p3, 0, "sixtythree", 10, 1, 1, &lfn_written) == KERNEL_STORAGE_OK && lfn_written == 10);

        char lfn_rbuf[32];
        uint32_t lfn_rlen = 0;
        CHECK(kernel_vfs_read_file(&vfs, p1, 0, lfn_rbuf, sizeof(lfn_rbuf), &lfn_rlen) == KERNEL_STORAGE_OK && lfn_rlen == 7);
        CHECK(memcmp(lfn_rbuf, "{\"a\":1}", 7) == 0);
        CHECK(kernel_vfs_read_file(&vfs, p2, 0, lfn_rbuf, sizeof(lfn_rbuf), &lfn_rlen) == KERNEL_STORAGE_OK && lfn_rlen == 7);
        CHECK(memcmp(lfn_rbuf, "{\"b\":2}", 7) == 0);
        CHECK(kernel_vfs_read_file(&vfs, p3, 0, lfn_rbuf, sizeof(lfn_rbuf), &lfn_rlen) == KERNEL_STORAGE_OK && lfn_rlen == 10);
        CHECK(memcmp(lfn_rbuf, "sixtythree", 10) == 0);

        CHECK(kernel_vfs_readdir(&vfs, "/Documenti/Sotto Cartella", 0, &st) == KERNEL_STORAGE_OK);
        CHECK(strcmp(st.Name, "my_long_config_alpha.json") == 0);
        CHECK(kernel_vfs_readdir(&vfs, "/Documenti/Sotto Cartella", 1, &st) == KERNEL_STORAGE_OK);
        CHECK(strcmp(st.Name, "my_long_config_beta.json") == 0);
        CHECK(kernel_vfs_readdir(&vfs, "/Documenti/Sotto Cartella", 2, &st) == KERNEL_STORAGE_OK);
        CHECK(strcmp(st.Name, "file_with_63_chars_abcdefghijklmnopqrstuvwxyz_0123456789_end123") == 0);

        // Non-empty rmdir must fail
        CHECK(kernel_vfs_rmdir(&vfs, "/Documenti/Sotto Cartella") == KERNEL_STORAGE_ERR_ARG);
        CHECK(kernel_vfs_rmdir(&vfs, "/") == KERNEL_STORAGE_ERR_ARG);

        // Unlink all LFN files (must clean all LFN slots) then rmdir nested dirs
        CHECK(kernel_vfs_unlink(&vfs, p1) == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_unlink(&vfs, p2) == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_unlink(&vfs, p3) == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_stat(&vfs, p1, &st) == KERNEL_STORAGE_ERR_NOT_FOUND);
        CHECK(kernel_vfs_readdir(&vfs, "/Documenti/Sotto Cartella", 0, &st) == KERNEL_STORAGE_ERR_NOT_FOUND);
        CHECK(kernel_vfs_rmdir(&vfs, "/Documenti/Sotto Cartella") == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_rmdir(&vfs, "/Documenti") == KERNEL_STORAGE_OK);
        CHECK(kernel_vfs_stat(&vfs, "/Documenti", &st) == KERNEL_STORAGE_ERR_NOT_FOUND);
        CHECK(kernel_vfs_sync(&vfs) == KERNEL_STORAGE_OK);
    }

    kernel_vfs_destroy(&vfs);
    kernel_block_cache_destroy(&fat32_cache);

    KERNEL_HEAP_STATS hstats = kernel_heap_stats(&test_heap);
    CHECK(hstats.FreePages == KERNEL_HEAP_MAX_PAGES && hstats.Objects == 0);

    printf("PASS: vfs_fat_host (mount, stat, readdir, multi-cluster read, subdir lookup, mkdir, LFN, rmdir, cycle detection, zero leaks)\n");
    return 0;
}

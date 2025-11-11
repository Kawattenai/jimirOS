#include <kernel/fs.h>
#include <kernel/bootinfo.h>
#include <kernel/stdio.h>
#include <kernel/block.h>
#include <string.h>
#include <stdint.h>
#include <kernel/ext2.h>

/* 
 * Simplified filesystem layer: ext2 ONLY.
 * All operations delegate directly to ext2 driver.
 * Read and write operations are supported.
 */

static int copy_module_to_disk(const uint8_t* data, uint32_t size) {
    if (!data || !size) return -1;
    uint32_t sectors = (size + 511u) / 512u;
    uint8_t scratch[512];
    for (uint32_t i = 0; i < sectors; ++i) {
        uint32_t offset = i * 512u;
        const uint8_t* src = data + offset;
        uint32_t remaining = (offset + 512u <= size) ? 512u : (size - offset);
        const void* chunk = src;
        if (remaining < 512u) {
            memset(scratch, 0, sizeof(scratch));
            memcpy(scratch, src, remaining);
            chunk = scratch;
        }
        if (block_write(i, 1, chunk) != 0) {
            printf("fs: disk write failed at LBA %u\n", i);
            return -1;
        }
    }
    return 0;
}

void fs_init(void) {
    int n = bootinfo_module_count();
    int mounted = 0;

    if (block_is_ready()) {
        if (ext2_mount_from_disk() == 0) {
            printf("fs: ext2 mounted from ATA/SATA disk (persistent)\n");
            mounted = 1;
        }
    }

    const uint8_t* module_data = 0;
    uint32_t module_size = 0;
    const char* module_name = 0;

    /* Search all modules for an ext2 filesystem */
    for (int i = 0; i < n && !mounted; ++i) {
        void* start = 0;
        uint32_t size = 0;
        const char* name = 0;

        if (bootinfo_get_module(i, &start, &size, &name) == 0) {
            if (start && size > 2048) {
                if (ext2_mount_from_module(start, size) == 0) {
                    module_data = (const uint8_t*)start;
                    module_size = size;
                    module_name = name;
                    printf("fs: ext2 mounted from '%s' (read/write enabled)\n",
                           module_name ? module_name : "module");
                    mounted = 1;
                }
            }
        }
    }

    if (mounted && block_is_ready() && module_data && module_size) {
        if (ext2_mount_from_disk() != 0) {
            printf("fs: syncing '%s' to disk for persistence...\n",
                   module_name ? module_name : "ext2");
            if (copy_module_to_disk(module_data, module_size) == 0) {
                if (ext2_mount_from_disk() == 0) {
                    printf("fs: ext2 now running from disk (persistent)\n");
                } else {
                    printf("fs: disk mount failed after sync, continuing from module\n");
                    ext2_mount_from_module(module_data, module_size);
                }
            } else {
                printf("fs: disk sync failed, continuing from module image\n");
            }
        }
    }

    if (!mounted) {
        printf("fs: WARNING - no ext2 filesystem found!\n");
    }
}

void fs_list_print(void) {
    if (!ext2_is_mounted()) {
        printf("(no filesystem mounted)\n");
        return;
    }
    char buf[512];
    int m = ext2_list(buf, sizeof(buf)-1);
    if (m < 0) m = 0;
    buf[m] = 0;
    printf("%s", buf);
}

int fs_dump_list(char* buf, unsigned len) {
    if (!ext2_is_mounted()) return 0;
    return ext2_list(buf, len);
}

int fs_open(const char* name) {
    if (!ext2_is_mounted()) return -1;
    return ext2_open(name);
}

int fs_read(int fd, void* buf, unsigned len) {
    if (!ext2_is_mounted()) return -1;
    return ext2_read(fd, buf, len);
}

int fs_write(int fd, const void* buf, unsigned len) {
    if (!ext2_is_mounted()) return -1;
    return ext2_write(fd, buf, len);
}

int fs_close(int fd) {
    if (!ext2_is_mounted()) return -1;
    return ext2_close(fd);
}

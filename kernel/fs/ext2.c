#include <kernel/ext2.h>
#include <kernel/stdio.h>
#include <kernel/block.h>
#include <kernel/kmalloc.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* Read-only ext2 driver backed by a memory buffer (Multiboot module).
   Supports: mount, list root directory, open/read/close regular files.
   Limitations: direct blocks only (no indirect), paths limited to /name (no subdirs). */

#define MIN(a,b) ((a)<(b)?(a):(b))

#pragma pack(push,1)
struct ext2_super_block {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
};

struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
};

struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15]; /* 0-11 direct, 12 singly, 13 doubly, 14 triply */
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
};

struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type; /* if feature_compat has FT, else name only */
    char     name[];
};
#pragma pack(pop)

/* Globals for mounted image */
static const uint8_t* g_img = 0;
static uint32_t g_img_size = 0;
static struct ext2_super_block sb;
static uint32_t g_block_size = 0;
static uint32_t g_inodes_per_group = 0;
static uint32_t g_inode_size = 128;
static uint32_t g_groups = 0;
static struct ext2_group_desc* g_gdt = 0; /* heap copy */
static uint32_t g_sectors_per_block = 0;
static int g_use_disk = 0;

#define EXT2_MAX_BLOCK_SIZE 4096
static uint8_t g_block_cache[EXT2_MAX_BLOCK_SIZE];
static uint32_t g_block_cache_num = 0xFFFFFFFFu;

/* Simple fd table: map to inode number and file position */
#define EXT2_MAX_FD 16
struct ext2_fd { int used; uint32_t ino; uint32_t pos; };
static struct ext2_fd fds[EXT2_MAX_FD];

static inline uint32_t block_offset_bytes(uint32_t blk) {
    return blk * g_block_size;
}

static const uint8_t* get_block(uint32_t blk) {
    if (!blk) return 0;
    if (!g_use_disk) {
        uint32_t off = block_offset_bytes(blk);
        if (!g_img || off + g_block_size > g_img_size) return 0;
        return g_img + off;
    }
    if (g_block_size > EXT2_MAX_BLOCK_SIZE) return 0;
    if (g_block_cache_num != blk) {
        uint32_t lba = blk * g_sectors_per_block;
        if (block_read(lba, (uint8_t)g_sectors_per_block, g_block_cache) != 0) {
            printf("ext2: failed to read block %u\n", blk);
            return 0;
        }
        g_block_cache_num = blk;
    }
    return g_block_cache;
}

static int write_block(uint32_t blk, const uint8_t* data) {
    if (!blk) return -1;
    if (!g_use_disk) {
        if (!g_img) return -1;
        uint32_t off = block_offset_bytes(blk);
        if (off + g_block_size > g_img_size) return -1;
        memcpy((uint8_t*)g_img + off, data, g_block_size);
        return 0;
    }
    if (g_block_size > EXT2_MAX_BLOCK_SIZE) return -1;
    uint32_t lba = blk * g_sectors_per_block;
    if (block_write(lba, (uint8_t)g_sectors_per_block, data) != 0) {
        printf("ext2: failed to write block %u\n", blk);
        return -1;
    }
    g_block_cache_num = 0xFFFFFFFFu;
    return 0;
}

static int read_block_into(uint32_t blk, uint8_t* out) {
    const uint8_t* data = get_block(blk);
    if (!data) return -1;
    memcpy(out, data, g_block_size);
    return 0;
}

static int copy_from_block(uint32_t blk, uint32_t offset, void* dst, uint32_t len) {
    const uint8_t* data = get_block(blk);
    if (!data) return -1;
    if (offset + len > g_block_size) return -1;
    memcpy(dst, data + offset, len);
    return 0;
}

static int copy_to_block(uint32_t blk, uint32_t offset, const void* src, uint32_t len) {
    if (offset + len > g_block_size) return -1;
    if (!g_use_disk) {
        if (!g_img) return -1;
        uint32_t off = block_offset_bytes(blk);
        if (off + offset + len > g_img_size) return -1;
        memcpy((uint8_t*)g_img + off + offset, src, len);
        return 0;
    }
    if (g_block_size > EXT2_MAX_BLOCK_SIZE) return -1;
    if (read_block_into(blk, g_block_cache) != 0) return -1;
    memcpy(g_block_cache + offset, src, len);
    return write_block(blk, g_block_cache);
}

static int read_inode(uint32_t ino, struct ext2_inode* out) {
    if (ino == 0) return -1;
    uint32_t idx = ino - 1;
    uint32_t group = idx / g_inodes_per_group;
    uint32_t index = idx % g_inodes_per_group;
    if (group >= g_groups) return -1;
    const struct ext2_group_desc* gd = &g_gdt[group];
    uint32_t table_blk = gd->bg_inode_table;
    uint32_t off = index * g_inode_size;
    uint32_t blk_offset = off / g_block_size;
    uint32_t within = off % g_block_size;
    uint32_t remaining = g_inode_size;
    uint8_t tmp[EXT2_MAX_BLOCK_SIZE];
    uint8_t* dst = tmp;
    uint32_t cur_blk = table_blk + blk_offset;
    while (remaining) {
        uint32_t chunk = g_block_size - within;
        if (chunk > remaining) chunk = remaining;
        if (copy_from_block(cur_blk, within, dst, chunk) != 0) return -1;
        dst += chunk;
        remaining -= chunk;
        within = 0;
        ++cur_blk;
    }
    memcpy(out, tmp, sizeof(struct ext2_inode) <= g_inode_size ? sizeof(struct ext2_inode) : g_inode_size);
    return 0;
}

static int read_file_direct(const struct ext2_inode* ino, uint32_t offset, void* buf, unsigned len) {
    unsigned copied = 0;
    while (len && offset < ino->i_size) {
        uint32_t blk_index = (offset / g_block_size);
        if (blk_index >= 12) break; /* only direct blocks */
        uint32_t blk = ino->i_block[blk_index];
        const uint8_t* data = get_block(blk);
        if (!data) break;
        uint32_t blk_off = offset % g_block_size;
        unsigned n = MIN(len, g_block_size - blk_off);
        unsigned remaining = ino->i_size - offset;
        if (n > remaining) n = remaining;
        memcpy((uint8_t*)buf + copied, data + blk_off, n);
        copied += n;
        offset += n;
        len -= n;
    }
    return (int)copied;
}

int ext2_mount_from_module(const void* start, uint32_t size) {
    if (!start || size < 2048) return -1;
    g_img = (const uint8_t*)start;
    g_img_size = size;
    g_use_disk = 0;
    g_block_cache_num = 0xFFFFFFFFu;

    /* Superblock at offset 1024 */
    memcpy(&sb, g_img + 1024, sizeof(sb));
    if (sb.s_magic != 0xEF53) {
        printf("ext2: bad magic 0x%x\n", sb.s_magic);
        g_img = 0; g_img_size = 0; g_block_size = 0; g_gdt = 0; return -1;
    }
    g_block_size = 1024u << sb.s_log_block_size;
    if (g_block_size == 0 || g_block_size > EXT2_MAX_BLOCK_SIZE) {
        printf("ext2: unsupported block size %u\n", g_block_size);
        g_img = 0; g_img_size = 0; g_block_size = 0; g_gdt = 0; return -1;
    }
    g_sectors_per_block = g_block_size / 512;
    if (g_sectors_per_block == 0) g_sectors_per_block = 1;
    g_inodes_per_group = sb.s_inodes_per_group;
    g_inode_size = sb.s_inode_size ? sb.s_inode_size : 128;
    g_groups = (sb.s_inodes_count + sb.s_inodes_per_group - 1) / sb.s_inodes_per_group;

    /* Group descriptor table follows the superblock; for 1KiB block, it starts at block 2 */
    uint32_t gdt_off = (g_block_size == 1024) ? (2 * 1024) : g_block_size;
    size_t gdt_bytes = g_groups * sizeof(struct ext2_group_desc);
    if (gdt_off + gdt_bytes > g_img_size) {
        printf("ext2: truncated group descriptor table\n");
        g_img = 0; g_img_size = 0; g_block_size = 0; g_gdt = 0; return -1;
    }
    if (g_gdt) { kfree(g_gdt); g_gdt = 0; }
    g_gdt = (struct ext2_group_desc*)kmalloc(gdt_bytes);
    if (!g_gdt) {
        printf("ext2: failed to allocate GDT\n");
        g_img = 0; g_img_size = 0; g_block_size = 0; g_gdt = 0; return -1;
    }
    memcpy(g_gdt, g_img + gdt_off, gdt_bytes);

    for (int i=0;i<EXT2_MAX_FD;i++) fds[i].used=0;
    printf("ext2: mounted (module) block_size=%u inodes=%u groups=%u\n", g_block_size, sb.s_inodes_count, g_groups);
    return 0;
}

int ext2_mount_from_disk(void) {
    g_use_disk = 0;
    g_block_cache_num = 0xFFFFFFFFu;
    uint8_t super_buf[1024];
    if (block_read(2, 2, super_buf) != 0) {
        printf("ext2: failed to read superblock from disk\n");
        return -1;
    }
    memcpy(&sb, super_buf, sizeof(sb));
    if (sb.s_magic != 0xEF53) {
        printf("ext2: disk magic mismatch 0x%x\n", sb.s_magic);
        g_block_size = 0;
        return -1;
    }
    g_block_size = 1024u << sb.s_log_block_size;
    if (g_block_size == 0 || g_block_size > EXT2_MAX_BLOCK_SIZE) {
        printf("ext2: disk block size %u unsupported\n", g_block_size);
        g_block_size = 0;
        return -1;
    }
    g_sectors_per_block = g_block_size / 512;
    if (g_sectors_per_block == 0) g_sectors_per_block = 1;
    g_inodes_per_group = sb.s_inodes_per_group;
    g_inode_size = sb.s_inode_size ? sb.s_inode_size : 128;
    g_groups = (sb.s_inodes_count + sb.s_inodes_per_group - 1) / sb.s_inodes_per_group;

    size_t gdt_bytes = g_groups * sizeof(struct ext2_group_desc);
    if (g_gdt) { kfree(g_gdt); g_gdt = 0; }
    g_gdt = (struct ext2_group_desc*)kmalloc(gdt_bytes);
    if (!g_gdt) {
        printf("ext2: failed to allocate GDT\n");
        g_block_size = 0;
        return -1;
    }

    uint32_t gdt_block = (g_block_size == 1024) ? 2 : 1;
    uint8_t temp[EXT2_MAX_BLOCK_SIZE];
    uint8_t* dst = (uint8_t*)g_gdt;
    size_t remaining = gdt_bytes;
    while (remaining) {
        size_t chunk = remaining < g_block_size ? remaining : g_block_size;
        if (block_read(gdt_block * g_sectors_per_block, (uint8_t)g_sectors_per_block, temp) != 0) {
            printf("ext2: failed to read GDT block %u\n", gdt_block);
            kfree(g_gdt);
            g_gdt = 0;
            g_block_size = 0;
            g_use_disk = 0;
            g_block_cache_num = 0xFFFFFFFFu;
            return -1;
        }
        memcpy(dst, temp, chunk);
        dst += chunk;
        remaining -= chunk;
        ++gdt_block;
    }

    g_img = 0;
    g_img_size = 0;
    g_use_disk = 1;
    g_block_cache_num = 0xFFFFFFFFu;
    for (int i=0;i<EXT2_MAX_FD;i++) fds[i].used=0;
    printf("ext2: mounted (disk) block_size=%u inodes=%u groups=%u\n", g_block_size, sb.s_inodes_count, g_groups);
    return 0;
}

int ext2_is_mounted(void){ return g_gdt != 0 && g_block_size != 0; }

static int path_basename(const char* path, const char** base){
    if (!path || !*path) return -1;
    const char* p = path; const char* last = path;
    while (*p){ if (*p=='/'||*p=='\\') last = p+1; p++; }
    *base = last; return 0;
}

int ext2_list(char* out, unsigned len){
    if (!ext2_is_mounted()) return -1;
    struct ext2_inode root;
    if (read_inode(2, &root) < 0) return -1;
    printf("[ext2_list] root inode: mode=0x%x size=%u blocks=%u\n", root.i_mode, root.i_size, root.i_blocks);
    printf("[ext2_list] root i_block[0]=0x%x i_block[1]=0x%x\n", root.i_block[0], root.i_block[1]);
    unsigned n=0;
    for (int i=0;i<12;i++){
        uint32_t blk = root.i_block[i]; if (!blk) continue;
        const uint8_t* data = get_block(blk); if (!data) continue;
        printf("[ext2_list] scanning block %d: blk_num=%u\n", i, blk);
        unsigned off=0;
        while (off + 8 <= g_block_size){
            const struct ext2_dir_entry* de = (const struct ext2_dir_entry*)(data + off);
            printf("[ext2_list] off=%u: inode=%u rec_len=%u name_len=%u\n", 
                   off, de->inode, de->rec_len, de->name_len);
            if (de->rec_len < 8 || de->rec_len > g_block_size - off) break;
            if (de->inode && de->name_len){
                printf("[ext2_list] found entry: ino=%u name_len=%u\n", de->inode, de->name_len);
                const char* name = (const char*)de->name;
                for (int j=0;j<de->name_len && n+1<len; j++) out[n++]=name[j];
                if (n<len) out[n++]='\n';
            }
            if (de->rec_len==0) break;
            off += de->rec_len;
        }
        printf("[ext2_list] returning %u bytes\n", n);
    }
    return (int)n;
}

static int lookup_in_root(const char* name, uint32_t* out_ino){
    struct ext2_inode root; if (read_inode(2,&root)<0) return -1;
    for (int i=0;i<12;i++){
        uint32_t blk = root.i_block[i]; if (!blk) continue;
        const uint8_t* data = get_block(blk); if (!data) continue;
        unsigned off=0;
        while (off + 8 <= g_block_size){
            const struct ext2_dir_entry* de = (const struct ext2_dir_entry*)(data + off);
            if (de->rec_len < 8 || de->rec_len > g_block_size - off) break;
            if (de->inode && de->name_len){
                if (de->name_len == strlen(name) && memcmp(de->name, name, de->name_len)==0) { *out_ino = de->inode; return 0; }
            }
            if (de->rec_len==0) break;
            off += de->rec_len;
        }
    }
    return -1;
}

int ext2_open(const char* path){
    if (!ext2_is_mounted()) return -1;
    const char* base; if (path_basename(path,&base)<0) return -1;
    uint32_t ino; if (lookup_in_root(base,&ino)<0) return -1;
    /* ensure it's a regular file or dir; for now allow regular files only */
    struct ext2_inode ino_rec; if (read_inode(ino,&ino_rec)<0) return -1;
    if ((ino_rec.i_mode & 0xF000) == 0x4000) { /* directory */ return -1; }
    for (int fd=3; fd<EXT2_MAX_FD; ++fd){ if (!fds[fd].used){ fds[fd].used=1; fds[fd].ino=ino; fds[fd].pos=0; return fd; } }
    return -1;
}

int ext2_read(int fd, void* buf, unsigned len){
    if (fd < 0 || fd >= EXT2_MAX_FD || !fds[fd].used) return -1;
    struct ext2_inode ino; if (read_inode(fds[fd].ino,&ino)<0) return -1;
    int r = read_file_direct(&ino, fds[fd].pos, buf, len);
    if (r > 0) fds[fd].pos += (unsigned)r;
    return r;
}

int ext2_close(int fd){ if (fd<0||fd>=EXT2_MAX_FD||!fds[fd].used) return -1; fds[fd].used=0; return 0; }

/* Minimal write support: overwrite existing bytes only (no growth, no allocation). */
static int write_file_direct(const struct ext2_inode* ino, uint32_t offset, const void* buf, unsigned len){
    unsigned written = 0;
    while (len && offset < ino->i_size) {
        uint32_t blk_index = (offset / g_block_size);
        if (blk_index >= 12) break; /* direct only */
        uint32_t blk = ino->i_block[blk_index];
        if (!blk) break;
        uint32_t blk_off = offset % g_block_size;
        unsigned n = MIN(len, g_block_size - blk_off);
        unsigned remaining = ino->i_size - offset;
        if (n > remaining) n = remaining;
        if (copy_to_block(blk, blk_off, (const uint8_t*)buf + written, n) != 0) break;
        written += n;
        offset += n;
        len -= n;
    }
    return (int)written;
}

int ext2_write(int fd, const void* buf, unsigned len){
    if (fd < 0 || fd >= EXT2_MAX_FD || !fds[fd].used) return -1;
    struct ext2_inode ino; if (read_inode(fds[fd].ino,&ino)<0) return -1;
    int r = write_file_direct(&ino, fds[fd].pos, buf, len);
    if (r > 0) fds[fd].pos += (unsigned)r;
    return r;
}

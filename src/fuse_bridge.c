#include "fuse_bridge.h"
#include "stripe_engine.h"
#include "ram_cache.h"
#include "common.h"

#ifdef USE_WINFSP
#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <process.h>
#include <winioctl.h>

#define MAX_CHUNK (64ULL * 1024 * 1024)

typedef struct {
    STRIPE_VOLUME* vol;
    char mount_point[4];
    volatile LONG64 highest_byte_written;
    volatile LONG64 total_bytes_freed;
    volatile LONG64 total_bytes_consumed;
} MOUNT_CTX;

static MOUNT_CTX g_ctx;
static HANDLE g_fuse_thread_handle = NULL;
static volatile LONG g_flush_in_progress = 0;

/* ---- Directory-aware file table ---- */

typedef struct {
    wchar_t full_path[256];  /* e.g. "file.txt", "dir1", "dir1/sub.txt" */
    bool is_dir;
    uint64_t size;
} FILE_ENTRY;

static FILE_ENTRY g_open_files[64];
static uint32_t g_open_count = 0;
static CRITICAL_SECTION g_file_table_lock;
static bool g_file_table_lock_init = false;

static void file_table_lock_init(void) {
    if (!g_file_table_lock_init) {
        InitializeCriticalSection(&g_file_table_lock);
        g_file_table_lock_init = true;
    }
}

/* Strip leading '/' */
static const char* strip_path(const char* path) {
    return (*path == '/') ? path + 1 : path;
}

static int find_open_file_locked(const char* name) {
    wchar_t wname[256];
    if (mbstowcs(wname, name, 256) == (size_t)-1) return -1;
    for (uint32_t i = 0; i < g_open_count; i++) {
        if (wcscmp(g_open_files[i].full_path, wname) == 0) return (int)i;
    }
    return -1;
}

static int add_open_file_locked(const char* name, bool is_dir) {
    /* Assumes g_file_table_lock is held */
    if (g_open_count >= 64) return -1;
    if (mbstowcs(g_open_files[g_open_count].full_path, name, 256) == (size_t)-1) return -1;
    g_open_files[g_open_count].is_dir = is_dir;
    g_open_files[g_open_count].size = 0;
    int idx = (int)g_open_count;
    g_open_count++;
    return idx;
}

/* Atomically find an existing entry, or add if not found. Returns index or -1. */
static int find_or_add_file_locked(const char* name, bool is_dir) {
    int idx = find_open_file_locked(name);
    if (idx >= 0) return idx;
    return add_open_file_locked(name, is_dir);
}

static uint64_t remove_open_file_get_size(const char* name) {
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    int idx = find_open_file_locked(name);
    if (idx < 0) { LeaveCriticalSection(&g_file_table_lock); return 0; }
    uint64_t sz = g_open_files[idx].size;
    g_open_files[idx] = g_open_files[--g_open_count];
    LeaveCriticalSection(&g_file_table_lock);
    return sz;
}

/* Check if path refers to an existing directory */
static bool is_dir_by_path(const char* name) {
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    int idx = find_open_file_locked(name);
    bool result = (idx >= 0 && g_open_files[idx].is_dir);
    LeaveCriticalSection(&g_file_table_lock);
    return result;
}

/* Check parent directory of a path exists */
static bool parent_dir_exists(const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash) return true; /* at root → parent is root, always exists */
    size_t plen = (size_t)(slash - path);
    if (plen == 0) return true; /* "/file" → parent is root */
    if (plen >= 256) return false; /* parent too long */
    char parent[256];
    strncpy(parent, path, plen);
    parent[plen] = 0;
    return is_dir_by_path(parent);
}

/* Is 'full_path' a direct child of 'parent'?  parent="" means root. */
static bool is_direct_child(const wchar_t* full_path, const wchar_t* parent) {
    size_t plen = wcslen(parent);
    size_t flen = wcslen(full_path);
    if (plen == 0)
        return wcschr(full_path, L'/') == NULL;
    if (flen <= plen + 1) return false;
    if (wcsncmp(full_path, parent, plen) != 0) return false;
    if (full_path[plen] != L'/') return false;
    return wcschr(full_path + plen + 1, L'/') == NULL;
}

/* ---- FUSE operations ---- */

static int raid_getattr(const char* path, struct fuse_stat* stbuf) {
    gs_lock();
    memset(stbuf, 0, sizeof(struct fuse_stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        gs_unlock();
        return 0;
    }
    const char* p = strip_path(path);
    if (*p) {
        file_table_lock_init();
        EnterCriticalSection(&g_file_table_lock);
        int idx = find_open_file_locked(p);
        if (idx >= 0) {
            stbuf->st_mode = g_open_files[idx].is_dir ? (S_IFDIR | 0777) : (S_IFREG | 0666);
            stbuf->st_nlink = 1;
            stbuf->st_size = (fuse_off_t)g_open_files[idx].size;
            LeaveCriticalSection(&g_file_table_lock);
            gs_unlock();
            return 0;
        }
        LeaveCriticalSection(&g_file_table_lock);
    }
    gs_unlock();
    return -ENOENT;
}

static int raid_fgetattr(const char* path, struct fuse_stat* stbuf, struct fuse_file_info* fi) {
    (void)fi;
    return raid_getattr(path, stbuf);
}

static int raid_readdir(const char* path, void* buf, fuse_fill_dir_t filler, fuse_off_t offset, struct fuse_file_info* fi) {
    (void)offset; (void)fi;
    gs_lock();
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    const char* p = strip_path(path);
    wchar_t wparent[256] = {0};
    if (mbstowcs(wparent, p, 256) == (size_t)-1) { gs_unlock(); return 0; }
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    for (uint32_t i = 0; i < g_open_count; i++) {
        if (is_direct_child(g_open_files[i].full_path, wparent)) {
            const wchar_t* display = g_open_files[i].full_path;
            if (wparent[0])
                display = g_open_files[i].full_path + wcslen(wparent) + 1;
            char name[256];
            wcstombs(name, display, 256);
            filler(buf, name, NULL, 0);
        }
    }
    LeaveCriticalSection(&g_file_table_lock);
    gs_unlock();
    return 0;
}

static int raid_open(const char* path, struct fuse_file_info* fi) {
    gs_lock();
    const char* p = strip_path(path);
    if (!*p) { gs_unlock(); return -ENOENT; }
    if (strlen(p) > 255) { gs_unlock(); return -ENAMETOOLONG; }
    if (fi->flags & O_CREAT) {
        if (!parent_dir_exists(p)) { gs_unlock(); return -ENOENT; }
        file_table_lock_init();
        EnterCriticalSection(&g_file_table_lock);
        int idx = find_or_add_file_locked(p, false);
        LeaveCriticalSection(&g_file_table_lock);
        if (idx < 0) { gs_unlock(); return -ENOSPC; }
    } else {
        file_table_lock_init();
        EnterCriticalSection(&g_file_table_lock);
        int idx = find_open_file_locked(p);
        LeaveCriticalSection(&g_file_table_lock);
        if (idx < 0) { gs_unlock(); return -ENOENT; }
    }
    if ((fi->flags & 3) != O_RDONLY) fi->flags |= O_RDWR;
    fi->fh = (uint64_t)(uintptr_t)strdup(p);
    gs_unlock();
    return 0;
}

static int raid_create(const char* path, fuse_mode_t mode, struct fuse_file_info* fi) {
    (void)mode; (void)fi;
    const char* p = strip_path(path);
    if (!*p) return -ENOENT;
    if (strlen(p) > 255) return -ENAMETOOLONG;
    if (!parent_dir_exists(p)) return -ENOENT;
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    if (find_open_file_locked(p) >= 0) {
        LeaveCriticalSection(&g_file_table_lock);
        return -EEXIST;
    }
    int idx = add_open_file_locked(p, false);
    LeaveCriticalSection(&g_file_table_lock);
    return (idx >= 0) ? 0 : -ENOSPC;
}

static int raid_mkdir(const char* path, fuse_mode_t mode) {
    (void)mode;
    const char* p = strip_path(path);
    if (!*p) return -EEXIST;
    if (strlen(p) > 255) return -ENAMETOOLONG;
    if (!parent_dir_exists(p)) return -ENOENT;
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    if (find_open_file_locked(p) >= 0) {
        LeaveCriticalSection(&g_file_table_lock);
        return -EEXIST;
    }
    int idx = add_open_file_locked(p, true);
    LeaveCriticalSection(&g_file_table_lock);
    return (idx >= 0) ? 0 : -ENOSPC;
}

/* ---- helpers for rmdir / rename ---- */
static bool is_dir_empty_locked(const wchar_t* dir_path) {
    size_t dlen = wcslen(dir_path);
    for (uint32_t i = 0; i < g_open_count; i++) {
        if (wcsncmp(g_open_files[i].full_path, dir_path, dlen) != 0) continue;
        if (g_open_files[i].full_path[dlen] == L'/') return false;
    }
    return true;
}

static int raid_rmdir(const char* path) {
    const char* p = strip_path(path);
    if (!*p) return -EBUSY;
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    int idx = find_open_file_locked(p);
    if (idx < 0) { LeaveCriticalSection(&g_file_table_lock); return -ENOENT; }
    if (!g_open_files[idx].is_dir) { LeaveCriticalSection(&g_file_table_lock); return -ENOTDIR; }
    if (!is_dir_empty_locked(g_open_files[idx].full_path)) { LeaveCriticalSection(&g_file_table_lock); return -ENOTEMPTY; }
    g_open_files[idx] = g_open_files[--g_open_count];
    LeaveCriticalSection(&g_file_table_lock);
    return 0;
}

static int raid_rename(const char* from, const char* to) {
    const char* src = strip_path(from);
    const char* dst = strip_path(to);
    if (!*src || !*dst) return -ENOENT;
    if (strlen(src) > 254 || strlen(dst) > 254) return -ENAMETOOLONG;
    if (!parent_dir_exists(dst)) return -ENOENT;
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    int idx = find_open_file_locked(src);
    if (idx < 0) { LeaveCriticalSection(&g_file_table_lock); return -ENOENT; }
    if (find_open_file_locked(dst) >= 0) { LeaveCriticalSection(&g_file_table_lock); return -EEXIST; }
    bool src_is_dir = g_open_files[idx].is_dir;
    wchar_t wsrc[256], wdst[256];
    if (mbstowcs(wsrc, src, 256) == (size_t)-1 ||
        mbstowcs(wdst, dst, 256) == (size_t)-1) {
        LeaveCriticalSection(&g_file_table_lock); return -EINVAL;
    }
    size_t slen = wcslen(wsrc);
    wcscpy(g_open_files[idx].full_path, wdst);
    if (src_is_dir) {
        if (slen >= 255) { LeaveCriticalSection(&g_file_table_lock); return -ENAMETOOLONG; }
        wcscat(wsrc, L"/");
        for (uint32_t i = 0; i < g_open_count; i++) {
            if (i == (uint32_t)idx) continue;
            if (wcsncmp(g_open_files[i].full_path, wsrc, slen + 1) == 0) {
                const wchar_t* suffix = g_open_files[i].full_path + slen + 1;
                size_t wdlen = wcslen(wdst);
                size_t suflen = wcslen(suffix);
                if (wdlen + 1 + suflen >= 256) continue;
                wchar_t newpath[256];
                wcscpy_s(newpath, 256, wdst);
                wcscat_s(newpath, 256, L"/");
                wcscat_s(newpath, 256, suffix);
                wcscpy_s(g_open_files[i].full_path, 256, newpath);
            }
        }
    }
    LeaveCriticalSection(&g_file_table_lock);
    return 0;
}

static int raid_unlink(const char* path) {
    const char* p = strip_path(path);
    if (!*p) return -ENOENT;
    uint64_t sz = remove_open_file_get_size(p);
    if (sz > 0) InterlockedExchangeAdd64(&g_ctx.total_bytes_freed, (LONG64)sz);
    return 0;
}

static int raid_truncate(const char* path, fuse_off_t size) {
    const char* p = strip_path(path);
    if (!*p) return -ENOENT;
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    int idx = find_open_file_locked(p);
    if (idx >= 0) {
        uint64_t old_size = g_open_files[idx].size;
        g_open_files[idx].size = (uint64_t)size;
        if ((uint64_t)size < old_size) {
            InterlockedExchangeAdd64(&g_ctx.total_bytes_freed, (LONG64)(old_size - (uint64_t)size));
        } else if ((uint64_t)size > old_size) {
            InterlockedExchangeAdd64(&g_ctx.total_bytes_consumed, (LONG64)((uint64_t)size - old_size));
            LONG64 sz = (LONG64)size;
            LONG64 old = g_ctx.highest_byte_written;
            while (sz > old) {
                LONG64 prev = InterlockedCompareExchange64(&g_ctx.highest_byte_written, sz, old);
                if (prev == old) break;
                old = prev;
            }
        }
    }
    LeaveCriticalSection(&g_file_table_lock);
    return 0;
}

static int raid_read(const char* path, char* buf, size_t size, fuse_off_t offset, struct fuse_file_info* fi) {
    (void)path; (void)fi;
    STRIPE_VOLUME* vol = g_ctx.vol;
    if (!vol) return -EIO;

    const char* p = strip_path(path);
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    int idx = find_open_file_locked(p);
    uint64_t file_size = (idx >= 0) ? g_open_files[idx].size : 0;
    LeaveCriticalSection(&g_file_table_lock);

    if ((uint64_t)offset >= file_size) return 0;

    uint64_t end = min_u64((uint64_t)offset + (uint64_t)size, file_size);
    uint64_t remaining = end - (uint64_t)offset;
    uint64_t total_read = 0;
    uint64_t curr_off = (uint64_t)offset;

    while (remaining > 0) {
        uint64_t chunk = min_u64(remaining, MAX_CHUNK);
        if (!stripe_volume_read(vol, buf + total_read, curr_off, (uint32_t)chunk))
            return (int)total_read > 0 ? (int)total_read : -EIO;
        total_read += chunk;
        remaining -= chunk;
        curr_off += chunk;
    }

    return (int)total_read;
}

/* Helper: update highest_byte_written atomically */
static void update_highest_written(LONG64 we) {
    LONG64 old = g_ctx.highest_byte_written;
    while (we > old) {
        LONG64 prev = InterlockedCompareExchange64(&g_ctx.highest_byte_written, we, old);
        if (prev == old) break;
        old = prev;
    }
}

/* Helper: check cache dirty ratio and force flush if >90% */
static void cache_backpressure(STRIPE_VOLUME* vol) {
    if (!vol || !vol->cache_enabled || vol->cache.block_count == 0) return;
    uint32_t dc = 0;
    EnterCriticalSection(&vol->cache.lock);
    for (uint32_t cb = 0; cb < vol->cache.block_count; cb++)
        if (vol->cache.dirty_map[cb / 8] & (1 << (cb % 8))) dc++;
    LeaveCriticalSection(&vol->cache.lock);
    if (dc > vol->cache.block_count * 9 / 10) {
        LOG_WARN("Cache back-pressure: %u/%u dirty, forcing flush", dc, vol->cache.block_count);
        if (InterlockedExchange(&g_flush_in_progress, 1) == 0) {
            cache_flush_all(&vol->cache, vol);
            InterlockedExchange(&g_flush_in_progress, 0);
        }
    }
}

static int raid_write(const char* path, const char* buf, size_t size, fuse_off_t offset, struct fuse_file_info* fi) {
    (void)fi;
    STRIPE_VOLUME* vol = g_ctx.vol;
    if (!vol) return -EIO;
    if ((uint64_t)offset >= vol->virtual_total_bytes) return 0;

    uint64_t end = min_u64((uint64_t)offset + (uint64_t)size, vol->virtual_total_bytes);
    uint64_t remaining = end - (uint64_t)offset;
    uint64_t total_written = 0;
    uint64_t curr_off = (uint64_t)offset;

    while (remaining > 0) {
        bool ok = false;

        if (vol->cache_enabled && curr_off < vol->cache.cache_size_bytes) {
            /* Cache path */
            cache_backpressure(vol);
            uint64_t cache_len = min_u64(end - curr_off, min_u64(MAX_CHUNK, vol->cache.cache_size_bytes - curr_off));
            if (cache_len > 0) {
                ok = cache_write(&vol->cache, buf + total_written, curr_off, (uint32_t)cache_len);
                if (ok) {
                    InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_written, (LONG64)cache_len);
                    InterlockedExchangeAdd64(&g_ctx.total_bytes_consumed, (LONG64)cache_len);
                    update_highest_written((LONG64)(curr_off + cache_len));
                    total_written += cache_len;
                    remaining -= cache_len;
                    curr_off += cache_len;
                    continue;
                }
            }
            /* Cache write failed or exhausted — fall through to direct write */
        }

        /* Direct write path (cache disabled, beyond cache, or cache_write failed) */
        uint64_t to_write = min_u64(remaining, MAX_CHUNK);
        ok = stripe_volume_write(vol, buf + total_written, curr_off, (uint32_t)to_write);
        if (ok) {
            InterlockedExchangeAdd64(&g_ctx.total_bytes_consumed, (LONG64)to_write);
            update_highest_written((LONG64)(curr_off + to_write));
            total_written += to_write;
            remaining -= to_write;
            curr_off += to_write;
        }

        if (!ok) return (int)total_written > 0 ? (int)total_written : -EIO;
    }

    const char* p = strip_path(path);
    if (*p && total_written > 0) {
        uint64_t write_end = (uint64_t)offset + total_written;
        file_table_lock_init();
        EnterCriticalSection(&g_file_table_lock);
        int idx = find_open_file_locked(p);
        if (idx >= 0 && write_end > g_open_files[idx].size)
            g_open_files[idx].size = write_end;
        LeaveCriticalSection(&g_file_table_lock);
    }

    return (int)total_written;
}

static int raid_release(const char* path, struct fuse_file_info* fi) {
    (void)path;
    if (fi->fh) free((void*)(uintptr_t)fi->fh);
    fi->fh = 0;
    return 0;
}

static int raid_flush(const char* path, struct fuse_file_info* fi) {
    (void)path; (void)fi;
    STRIPE_VOLUME* vol = g_ctx.vol;
    if (vol && vol->cache_enabled) {
        if (InterlockedExchange(&g_flush_in_progress, 1) == 0) {
            cache_flush_all(&vol->cache, vol);
            InterlockedExchange(&g_flush_in_progress, 0);
        }
    }
    return 0;
}

static int raid_fsync(const char* path, int isdatasync, struct fuse_file_info* fi) {
    (void)path; (void)isdatasync; (void)fi;
    STRIPE_VOLUME* vol = g_ctx.vol;
    if (vol && vol->cache_enabled) {
        if (InterlockedExchange(&g_flush_in_progress, 1) == 0) {
            cache_flush_all(&vol->cache, vol);
            InterlockedExchange(&g_flush_in_progress, 0);
        }
    }
    return 0;
}

static int raid_ioctl(const char* path, int cmd, void* arg, struct fuse_file_info* fi,
    unsigned int flags, void* data) {
    (void)path; (void)fi; (void)flags;
    STRIPE_VOLUME* vol = g_ctx.vol;
    if (!vol) return -ENODEV;

    uint32_t func = ((uint32_t)cmd >> 2) & 0xFFF;

    switch (func) {
    case 0x17: { /* IOCTL_DISK_GET_LENGTH_INFO */
        if (!data) return -EFAULT;
        GET_LENGTH_INFORMATION gli;
        gli.Length.QuadPart = vol->virtual_total_bytes;
        memcpy(data, &gli, sizeof(gli));
        return 0;
    }
    case 0x500: { /* IOCTL_STORAGE_QUERY_PROPERTY */
        if (!arg || !data) return -EFAULT;
        STORAGE_PROPERTY_QUERY* spq = (STORAGE_PROPERTY_QUERY*)arg;
        if (spq->PropertyId != StorageDeviceProperty) return -ENOSYS;

        uint32_t out_size = ((uint32_t)cmd >> 16) & 0x3FFF;
        uint32_t sdd_size = sizeof(STORAGE_DEVICE_DESCRIPTOR);
        if (out_size < sdd_size) sdd_size = out_size;
        memset(data, 0, sdd_size);
        STORAGE_DEVICE_DESCRIPTOR* sdd = (STORAGE_DEVICE_DESCRIPTOR*)data;
        sdd->Version = sdd_size;
        sdd->Size = sdd_size;
        sdd->DeviceType = 0x22;           /* FILE_DEVICE_UNKNOWN */
        sdd->RemovableMedia = FALSE;
        sdd->CommandQueueing = TRUE;
        sdd->BusType = 0x09;              /* BusTypeVirtual */
        return 0;
    }
    default:
        return -ENOSYS;
    }
}

static int raid_statfs(const char* path, struct fuse_statvfs* st) {
    (void)path;
    STRIPE_VOLUME* vol = g_ctx.vol;
    if (!vol) return -ENOENT;
    memset(st, 0, sizeof(struct fuse_statvfs));
    st->f_bsize = SECTOR_SIZE;
    st->f_frsize = SECTOR_SIZE;
    st->f_blocks = (fuse_fsblkcnt_t)(vol->virtual_total_bytes / SECTOR_SIZE);

    uint64_t consumed = (uint64_t)InterlockedExchangeAdd64(&g_ctx.total_bytes_consumed, 0);
    uint64_t freed = (uint64_t)InterlockedExchangeAdd64(&g_ctx.total_bytes_freed, 0);
    uint64_t used_bytes = (consumed > freed) ? (consumed - freed) : 0;
    if (used_bytes > vol->virtual_total_bytes) used_bytes = vol->virtual_total_bytes;

    uint64_t used_blocks = used_bytes / SECTOR_SIZE;
    if (used_bytes % SECTOR_SIZE) used_blocks++;
    st->f_bfree = st->f_blocks - (used_blocks > (uint64_t)st->f_blocks ? (uint64_t)st->f_blocks : used_blocks);
    st->f_bavail = st->f_bfree;
    st->f_files = 64;
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    uint32_t oc = g_open_count;
    LeaveCriticalSection(&g_file_table_lock);
    st->f_ffree = 64 - oc;
    st->f_namemax = 255;
    return 0;
}

static struct fuse_operations raid_ops = {
    .getattr  = raid_getattr,
    .fgetattr = raid_fgetattr,
    .readdir  = raid_readdir,
    .open     = raid_open,
    .create   = raid_create,
    .mkdir    = raid_mkdir,
    .rmdir    = raid_rmdir,
    .rename   = raid_rename,
    .unlink   = raid_unlink,
    .truncate = raid_truncate,
    .read     = raid_read,
    .write    = raid_write,
    .release  = raid_release,
    .flush    = raid_flush,
    .fsync    = raid_fsync,
    .ioctl    = raid_ioctl,
    .statfs   = raid_statfs,
};

static unsigned int __stdcall fuse_thread_func(void* arg) {
    fuse_loop_mt((struct fuse*)arg);
    return 0;
}
#endif

bool fuse_mount_volume(STRIPE_VOLUME* vol, char drive_letter) {
#ifdef USE_WINFSP
    if (!vol) return false;
    g_ctx.vol = vol;
    g_ctx.highest_byte_written = 0;
    g_ctx.total_bytes_freed = 0;
    g_ctx.total_bytes_consumed = 0;
    g_ctx.mount_point[0] = drive_letter;
    g_ctx.mount_point[1] = ':';
    g_ctx.mount_point[2] = 0;
    vol->mount_point[0] = drive_letter;
    vol->mount_point[1] = ':';
    vol->mount_point[2] = 0;

    char mount_path[8];
    StringCchPrintfA(mount_path, 8, "%c:", drive_letter);

    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    const char* fuse_argv[] = {
        "raidtest",
        "-o", "volname=RAIDTEST",
        "-o", "FileSystemName=RAIDTEST",
        "-o", "local",
        "-o", "umask=000",
        NULL
    };
    args.argc = 9;
    args.argv = (char**)fuse_argv;
    args.allocated = 0;

    struct fuse_chan* ch = fuse_mount(mount_path, &args);
    if (!ch) { LOG_ERROR("fuse_mount failed"); return false; }

    struct fuse* f = fuse_new(ch, &args, &raid_ops, sizeof(raid_ops), NULL);
    if (!f) {
        fuse_unmount(mount_path, ch);
        LOG_ERROR("fuse_new failed");
        return false;
    }

    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, fuse_thread_func, f, 0, NULL);
    if (!thread) {
        fuse_destroy(f);
        fuse_unmount(mount_path, ch);
        LOG_ERROR("Failed to create FUSE thread");
        return false;
    }
    g_fuse_thread_handle = thread;
    LOG_OK("RAIDTEST volume mounted at %c:", drive_letter);
    return true;
#else
    (void)vol; (void)drive_letter;
    LOG_WARN("WinFsp not linked. Rebuild with -DUSE_WINFSP");
    return false;
#endif
}

bool fuse_unmount_volume(char drive_letter) {
#ifdef USE_WINFSP
    char mount_path[8];
    StringCchPrintfA(mount_path, 8, "%c:", drive_letter);
    fuse_unmount(mount_path, NULL);
    if (g_fuse_thread_handle) {
        WaitForSingleObject(g_fuse_thread_handle, 5000);
        CloseHandle(g_fuse_thread_handle);
        g_fuse_thread_handle = NULL;
    }
    if (g_file_table_lock_init) {
        /* Do NOT delete the CS here — WinFsp worker threads may still be
           active (they outlive fuse_loop_mt). The OS reclaims the CRITICAL_SECTION
           when the process exits. See P0-14/H6 for details. */
        g_file_table_lock_init = false; /* mark as no longer valid */
    }
    LOG_OK("Unmounted %c:", drive_letter);
    return true;
#else
    (void)drive_letter;
    return false;
#endif
}
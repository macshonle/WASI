/**
 * WASI Filesystem Implementation
 *
 * This file implements the wasi:filesystem interfaces for UNIX/Linux/macOS.
 *
 * Interfaces implemented:
 *   - wasi:filesystem/types@0.2.0     - Filesystem types and descriptor operations
 *   - wasi:filesystem/preopens@0.2.0  - Preopened directories
 */

/* Feature test macros must come first */
#ifdef __linux__
    #define _GNU_SOURCE  /* Enable GNU extensions on Linux */
#endif
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>   /* renameat is declared here in POSIX */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#include "platform/platform.h"

/* Include the generated bindings header */
#include "../../build/c-bindings/filesystem/imports.h"

/* External declarations from io.c */
extern int32_t wasi_io_streams_create_input_stream(int fd, bool owns_fd);
extern int32_t wasi_io_streams_create_output_stream(int fd, bool owns_fd);

/* ============================================================================
 * Helper: cabi_realloc
 * ============================================================================
 */
__attribute__((__weak__))
void *cabi_realloc(void *ptr, size_t old_size, size_t align, size_t new_size) {
    (void)old_size;
    (void)align;
    if (new_size == 0) return (void*)align;
    void *ret = realloc(ptr, new_size);
    if (!ret) abort();
    return ret;
}

/* ============================================================================
 * Handle Management for Descriptors
 * ============================================================================
 */

#define MAX_DESCRIPTOR_HANDLES 1024

typedef struct {
    int fd;                 /* OS file descriptor */
    bool owns_fd;           /* Whether we should close on drop */
    uint8_t flags;          /* Descriptor flags */
    char *path;             /* Path (for preopened dirs) */
} wasi_descriptor_resource_t;

typedef struct {
    DIR *dir;               /* Directory stream */
    int fd;                 /* Associated descriptor fd */
} wasi_dir_stream_resource_t;

static wasi_descriptor_resource_t *descriptor_table[MAX_DESCRIPTOR_HANDLES];
static wasi_dir_stream_resource_t *dir_stream_table[MAX_DESCRIPTOR_HANDLES];
static int32_t next_descriptor_handle = 1;
static int32_t next_dir_stream_handle = 1;

static int32_t wasi_descriptor_alloc(int fd, bool owns_fd, uint8_t flags, const char *path) {
    if (next_descriptor_handle >= MAX_DESCRIPTOR_HANDLES) return -1;

    wasi_descriptor_resource_t *desc = (wasi_descriptor_resource_t *)malloc(sizeof(wasi_descriptor_resource_t));
    if (!desc) return -1;

    desc->fd = fd;
    desc->owns_fd = owns_fd;
    desc->flags = flags;
    desc->path = path ? strdup(path) : NULL;

    int32_t handle = next_descriptor_handle++;
    descriptor_table[handle] = desc;
    return handle;
}

static wasi_descriptor_resource_t *wasi_descriptor_get(int32_t handle) {
    if (handle <= 0 || handle >= MAX_DESCRIPTOR_HANDLES) return NULL;
    return descriptor_table[handle];
}

static int32_t wasi_dir_stream_alloc(DIR *dir, int fd) {
    if (next_dir_stream_handle >= MAX_DESCRIPTOR_HANDLES) return -1;

    wasi_dir_stream_resource_t *stream = (wasi_dir_stream_resource_t *)malloc(sizeof(wasi_dir_stream_resource_t));
    if (!stream) return -1;

    stream->dir = dir;
    stream->fd = fd;

    int32_t handle = next_dir_stream_handle++;
    dir_stream_table[handle] = stream;
    return handle;
}

static wasi_dir_stream_resource_t *wasi_dir_stream_get(int32_t handle) {
    if (handle <= 0 || handle >= MAX_DESCRIPTOR_HANDLES) return NULL;
    return dir_stream_table[handle];
}

/* ============================================================================
 * Error Code Conversion
 * ============================================================================
 */

static wasi_filesystem_types_error_code_t errno_to_error_code(int err) {
    switch (err) {
        case EACCES: return WASI_FILESYSTEM_TYPES_ERROR_CODE_ACCESS;
        case EAGAIN: return WASI_FILESYSTEM_TYPES_ERROR_CODE_WOULD_BLOCK;
        case EBADF: return WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        case EBUSY: return WASI_FILESYSTEM_TYPES_ERROR_CODE_BUSY;
        case EDQUOT: return WASI_FILESYSTEM_TYPES_ERROR_CODE_QUOTA;
        case EEXIST: return WASI_FILESYSTEM_TYPES_ERROR_CODE_EXIST;
        case EFBIG: return WASI_FILESYSTEM_TYPES_ERROR_CODE_FILE_TOO_LARGE;
        case EINVAL: return WASI_FILESYSTEM_TYPES_ERROR_CODE_INVALID;
        case EIO: return WASI_FILESYSTEM_TYPES_ERROR_CODE_IO;
        case EISDIR: return WASI_FILESYSTEM_TYPES_ERROR_CODE_IS_DIRECTORY;
        case ELOOP: return WASI_FILESYSTEM_TYPES_ERROR_CODE_LOOP;
        case EMLINK: return WASI_FILESYSTEM_TYPES_ERROR_CODE_TOO_MANY_LINKS;
        case ENAMETOOLONG: return WASI_FILESYSTEM_TYPES_ERROR_CODE_NAME_TOO_LONG;
        case ENOENT: return WASI_FILESYSTEM_TYPES_ERROR_CODE_NO_ENTRY;
        case ENOMEM: return WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        case ENOSPC: return WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_SPACE;
        case ENOTDIR: return WASI_FILESYSTEM_TYPES_ERROR_CODE_NOT_DIRECTORY;
        case ENOTEMPTY: return WASI_FILESYSTEM_TYPES_ERROR_CODE_NOT_EMPTY;
        case ENOTRECOVERABLE: return WASI_FILESYSTEM_TYPES_ERROR_CODE_NOT_RECOVERABLE;
        case EPERM: return WASI_FILESYSTEM_TYPES_ERROR_CODE_NOT_PERMITTED;
        case EROFS: return WASI_FILESYSTEM_TYPES_ERROR_CODE_READ_ONLY;
        case ESPIPE: return WASI_FILESYSTEM_TYPES_ERROR_CODE_INVALID_SEEK;
        case ETXTBSY: return WASI_FILESYSTEM_TYPES_ERROR_CODE_TEXT_FILE_BUSY;
        case EXDEV: return WASI_FILESYSTEM_TYPES_ERROR_CODE_CROSS_DEVICE;
        default: return WASI_FILESYSTEM_TYPES_ERROR_CODE_IO;
    }
}

/* ============================================================================
 * String Helpers
 * ============================================================================
 */

static char *wasi_string_to_cstr(imports_string_t *str) {
    char *cstr = (char *)malloc(str->len + 1);
    if (cstr) {
        memcpy(cstr, str->ptr, str->len);
        cstr[str->len] = '\0';
    }
    return cstr;
}

__attribute__((__weak__))
void imports_string_free(imports_string_t *ret) {
    if (ret->len > 0 && ret->ptr) {
        free(ret->ptr);
    }
    ret->ptr = NULL;
    ret->len = 0;
}

__attribute__((__weak__))
void imports_list_u8_free(imports_list_u8_t *ptr) {
    if (ptr->len > 0 && ptr->ptr) {
        free(ptr->ptr);
    }
    ptr->ptr = NULL;
    ptr->len = 0;
}

/* ============================================================================
 * Descriptor Resource Management
 * ============================================================================
 */

void wasi_filesystem_types_descriptor_drop_own(wasi_filesystem_types_own_descriptor_t handle) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(handle.__handle);
    if (desc) {
        if (desc->owns_fd && desc->fd >= 0) {
            close(desc->fd);
        }
        if (desc->path) free(desc->path);
        free(desc);
        descriptor_table[handle.__handle] = NULL;
    }
}

void wasi_filesystem_types_descriptor_drop_borrow(wasi_filesystem_types_borrow_descriptor_t handle) {
    (void)handle;
}

wasi_filesystem_types_borrow_descriptor_t wasi_filesystem_types_borrow_descriptor(
    wasi_filesystem_types_own_descriptor_t handle
) {
    return (wasi_filesystem_types_borrow_descriptor_t){ handle.__handle };
}

void wasi_filesystem_types_directory_entry_stream_drop_own(
    wasi_filesystem_types_own_directory_entry_stream_t handle
) {
    wasi_dir_stream_resource_t *stream = wasi_dir_stream_get(handle.__handle);
    if (stream) {
        if (stream->dir) closedir(stream->dir);
        free(stream);
        dir_stream_table[handle.__handle] = NULL;
    }
}

void wasi_filesystem_types_directory_entry_stream_drop_borrow(
    wasi_filesystem_types_borrow_directory_entry_stream_t handle
) {
    (void)handle;
}

wasi_filesystem_types_borrow_directory_entry_stream_t wasi_filesystem_types_borrow_directory_entry_stream(
    wasi_filesystem_types_own_directory_entry_stream_t handle
) {
    return (wasi_filesystem_types_borrow_directory_entry_stream_t){ handle.__handle };
}

/* ============================================================================
 * Stat Helpers
 * ============================================================================
 */

static wasi_filesystem_types_descriptor_type_t mode_to_descriptor_type(mode_t mode) {
    if (S_ISREG(mode)) return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_REGULAR_FILE;
    if (S_ISDIR(mode)) return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_DIRECTORY;
    if (S_ISLNK(mode)) return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_SYMBOLIC_LINK;
    if (S_ISBLK(mode)) return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_BLOCK_DEVICE;
    if (S_ISCHR(mode)) return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_CHARACTER_DEVICE;
    if (S_ISFIFO(mode)) return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_FIFO;
    if (S_ISSOCK(mode)) return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_SOCKET;
    return WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_UNKNOWN;
}

static void timespec_to_datetime(struct timespec *ts, wasi_filesystem_types_datetime_t *dt) {
    dt->seconds = (uint64_t)ts->tv_sec;
    dt->nanoseconds = (uint32_t)ts->tv_nsec;
}

static void stat_to_descriptor_stat(struct stat *st, wasi_filesystem_types_descriptor_stat_t *ret) {
    ret->type = mode_to_descriptor_type(st->st_mode);
    ret->link_count = (uint64_t)st->st_nlink;
    ret->size = (uint64_t)st->st_size;

    /* Timestamps are optional - set is_some=true since POSIX always provides them */
    ret->data_access_timestamp.is_some = true;
    ret->data_modification_timestamp.is_some = true;
    ret->status_change_timestamp.is_some = true;

#ifdef __APPLE__
    timespec_to_datetime(&st->st_atimespec, &ret->data_access_timestamp.val);
    timespec_to_datetime(&st->st_mtimespec, &ret->data_modification_timestamp.val);
    timespec_to_datetime(&st->st_ctimespec, &ret->status_change_timestamp.val);
#else
    timespec_to_datetime(&st->st_atim, &ret->data_access_timestamp.val);
    timespec_to_datetime(&st->st_mtim, &ret->data_modification_timestamp.val);
    timespec_to_datetime(&st->st_ctim, &ret->status_change_timestamp.val);
#endif
}

/* ============================================================================
 * Descriptor Methods
 * ============================================================================
 */

bool wasi_filesystem_types_method_descriptor_read_via_stream(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_filesize_t offset,
    wasi_filesystem_types_own_input_stream_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    /* Duplicate fd for the stream */
    int new_fd = dup(desc->fd);
    if (new_fd < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    /* Seek to offset */
    if (lseek(new_fd, (off_t)offset, SEEK_SET) < 0) {
        close(new_fd);
        *err = errno_to_error_code(errno);
        return false;
    }

    int32_t handle = wasi_io_streams_create_input_stream(new_fd, true);
    if (handle < 0) {
        close(new_fd);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    ret->__handle = handle;
    return true;
}

bool wasi_filesystem_types_method_descriptor_write_via_stream(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_filesize_t offset,
    wasi_filesystem_types_own_output_stream_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    int new_fd = dup(desc->fd);
    if (new_fd < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    if (lseek(new_fd, (off_t)offset, SEEK_SET) < 0) {
        close(new_fd);
        *err = errno_to_error_code(errno);
        return false;
    }

    int32_t handle = wasi_io_streams_create_output_stream(new_fd, true);
    if (handle < 0) {
        close(new_fd);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    ret->__handle = handle;
    return true;
}

bool wasi_filesystem_types_method_descriptor_append_via_stream(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_own_output_stream_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    int new_fd = dup(desc->fd);
    if (new_fd < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    /* Seek to end */
    if (lseek(new_fd, 0, SEEK_END) < 0) {
        close(new_fd);
        *err = errno_to_error_code(errno);
        return false;
    }

    int32_t handle = wasi_io_streams_create_output_stream(new_fd, true);
    if (handle < 0) {
        close(new_fd);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    ret->__handle = handle;
    return true;
}

bool wasi_filesystem_types_method_descriptor_advise(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_filesize_t offset,
    wasi_filesystem_types_filesize_t length,
    wasi_filesystem_types_advice_t advice,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

#ifdef __linux__
    int posix_advice;
    switch (advice) {
        case WASI_FILESYSTEM_TYPES_ADVICE_NORMAL: posix_advice = POSIX_FADV_NORMAL; break;
        case WASI_FILESYSTEM_TYPES_ADVICE_SEQUENTIAL: posix_advice = POSIX_FADV_SEQUENTIAL; break;
        case WASI_FILESYSTEM_TYPES_ADVICE_RANDOM: posix_advice = POSIX_FADV_RANDOM; break;
        case WASI_FILESYSTEM_TYPES_ADVICE_WILL_NEED: posix_advice = POSIX_FADV_WILLNEED; break;
        case WASI_FILESYSTEM_TYPES_ADVICE_DONT_NEED: posix_advice = POSIX_FADV_DONTNEED; break;
        case WASI_FILESYSTEM_TYPES_ADVICE_NO_REUSE: posix_advice = POSIX_FADV_NOREUSE; break;
        default: posix_advice = POSIX_FADV_NORMAL; break;
    }
    if (posix_fadvise(desc->fd, (off_t)offset, (off_t)length, posix_advice) != 0) {
        *err = errno_to_error_code(errno);
        return false;
    }
#else
    (void)offset;
    (void)length;
    (void)advice;
#endif

    return true;
}

bool wasi_filesystem_types_method_descriptor_sync_data(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    if (fdatasync(desc->fd) < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }
    return true;
}

bool wasi_filesystem_types_method_descriptor_get_flags(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_descriptor_flags_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    int flags = fcntl(desc->fd, F_GETFL);
    if (flags < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    *ret = 0;
    if ((flags & O_ACCMODE) == O_RDONLY || (flags & O_ACCMODE) == O_RDWR) {
        *ret |= WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ;
    }
    if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
        *ret |= WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE;
    }
    if (flags & O_SYNC) {
        *ret |= WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_FILE_INTEGRITY_SYNC;
    }
    if (flags & O_DSYNC) {
        *ret |= WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_DATA_INTEGRITY_SYNC;
    }

    return true;
}

bool wasi_filesystem_types_method_descriptor_get_type(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_descriptor_type_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    struct stat st;
    if (fstat(desc->fd, &st) < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    *ret = mode_to_descriptor_type(st.st_mode);
    return true;
}

bool wasi_filesystem_types_method_descriptor_set_size(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_filesize_t size,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    if (ftruncate(desc->fd, (off_t)size) < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }
    return true;
}

bool wasi_filesystem_types_method_descriptor_set_times(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_new_timestamp_t *data_access_timestamp,
    wasi_filesystem_types_new_timestamp_t *data_modification_timestamp,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    struct timespec times[2];

    /* Access time */
    if (data_access_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NO_CHANGE) {
        times[0].tv_nsec = UTIME_OMIT;
    } else if (data_access_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NOW) {
        times[0].tv_nsec = UTIME_NOW;
    } else {
        times[0].tv_sec = (time_t)data_access_timestamp->val.timestamp.seconds;
        times[0].tv_nsec = (long)data_access_timestamp->val.timestamp.nanoseconds;
    }

    /* Modification time */
    if (data_modification_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NO_CHANGE) {
        times[1].tv_nsec = UTIME_OMIT;
    } else if (data_modification_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NOW) {
        times[1].tv_nsec = UTIME_NOW;
    } else {
        times[1].tv_sec = (time_t)data_modification_timestamp->val.timestamp.seconds;
        times[1].tv_nsec = (long)data_modification_timestamp->val.timestamp.nanoseconds;
    }

    if (futimens(desc->fd, times) < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }
    return true;
}

bool wasi_filesystem_types_method_descriptor_read(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_filesize_t length,
    wasi_filesystem_types_filesize_t offset,
    imports_tuple2_list_u8_bool_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    size_t to_read = (length > 65536) ? 65536 : (size_t)length;
    uint8_t *buf = (uint8_t *)malloc(to_read);
    if (!buf) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    ssize_t nread = pread(desc->fd, buf, to_read, (off_t)offset);
    if (nread < 0) {
        free(buf);
        *err = errno_to_error_code(errno);
        return false;
    }

    ret->f0.ptr = buf;
    ret->f0.len = (size_t)nread;
    ret->f1 = (nread == 0);  /* EOF if 0 bytes read */
    return true;
}

bool wasi_filesystem_types_method_descriptor_write(
    wasi_filesystem_types_borrow_descriptor_t self,
    imports_list_u8_t *buffer,
    wasi_filesystem_types_filesize_t offset,
    wasi_filesystem_types_filesize_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    ssize_t nwritten = pwrite(desc->fd, buffer->ptr, buffer->len, (off_t)offset);
    if (nwritten < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    *ret = (uint64_t)nwritten;
    return true;
}

bool wasi_filesystem_types_method_descriptor_read_directory(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_own_directory_entry_stream_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    int new_fd = dup(desc->fd);
    if (new_fd < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    DIR *dir = fdopendir(new_fd);
    if (!dir) {
        close(new_fd);
        *err = errno_to_error_code(errno);
        return false;
    }

    int32_t handle = wasi_dir_stream_alloc(dir, new_fd);
    if (handle < 0) {
        closedir(dir);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    ret->__handle = handle;
    return true;
}

bool wasi_filesystem_types_method_descriptor_sync(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    if (fsync(desc->fd) < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }
    return true;
}

bool wasi_filesystem_types_method_descriptor_create_directory_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    imports_string_t *path,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    if (mkdirat(desc->fd, cpath, 0755) < 0) {
        free(cpath);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cpath);
    return true;
}

bool wasi_filesystem_types_method_descriptor_stat(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_descriptor_stat_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    struct stat st;
    if (fstat(desc->fd, &st) < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    stat_to_descriptor_stat(&st, ret);
    return true;
}

bool wasi_filesystem_types_method_descriptor_stat_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_path_flags_t path_flags,
    imports_string_t *path,
    wasi_filesystem_types_descriptor_stat_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    int flags = (path_flags & WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW) ? 0 : AT_SYMLINK_NOFOLLOW;

    struct stat st;
    if (fstatat(desc->fd, cpath, &st, flags) < 0) {
        free(cpath);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cpath);
    stat_to_descriptor_stat(&st, ret);
    return true;
}

bool wasi_filesystem_types_method_descriptor_set_times_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_path_flags_t path_flags,
    imports_string_t *path,
    wasi_filesystem_types_new_timestamp_t *data_access_timestamp,
    wasi_filesystem_types_new_timestamp_t *data_modification_timestamp,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    struct timespec times[2];

    if (data_access_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NO_CHANGE) {
        times[0].tv_nsec = UTIME_OMIT;
    } else if (data_access_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NOW) {
        times[0].tv_nsec = UTIME_NOW;
    } else {
        times[0].tv_sec = (time_t)data_access_timestamp->val.timestamp.seconds;
        times[0].tv_nsec = (long)data_access_timestamp->val.timestamp.nanoseconds;
    }

    if (data_modification_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NO_CHANGE) {
        times[1].tv_nsec = UTIME_OMIT;
    } else if (data_modification_timestamp->tag == WASI_FILESYSTEM_TYPES_NEW_TIMESTAMP_NOW) {
        times[1].tv_nsec = UTIME_NOW;
    } else {
        times[1].tv_sec = (time_t)data_modification_timestamp->val.timestamp.seconds;
        times[1].tv_nsec = (long)data_modification_timestamp->val.timestamp.nanoseconds;
    }

    int flags = (path_flags & WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW) ? 0 : AT_SYMLINK_NOFOLLOW;

    if (utimensat(desc->fd, cpath, times, flags) < 0) {
        free(cpath);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cpath);
    return true;
}

bool wasi_filesystem_types_method_descriptor_link_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_path_flags_t old_path_flags,
    imports_string_t *old_path,
    wasi_filesystem_types_borrow_descriptor_t new_descriptor,
    imports_string_t *new_path,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *old_desc = wasi_descriptor_get(self.__handle);
    wasi_descriptor_resource_t *new_desc = wasi_descriptor_get(new_descriptor.__handle);
    if (!old_desc || !new_desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cold_path = wasi_string_to_cstr(old_path);
    char *cnew_path = wasi_string_to_cstr(new_path);
    if (!cold_path || !cnew_path) {
        free(cold_path);
        free(cnew_path);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    int flags = (old_path_flags & WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW) ? AT_SYMLINK_FOLLOW : 0;

    if (linkat(old_desc->fd, cold_path, new_desc->fd, cnew_path, flags) < 0) {
        free(cold_path);
        free(cnew_path);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cold_path);
    free(cnew_path);
    return true;
}

bool wasi_filesystem_types_method_descriptor_open_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_path_flags_t path_flags,
    imports_string_t *path,
    wasi_filesystem_types_open_flags_t open_flags,
    wasi_filesystem_types_descriptor_flags_t flags,
    wasi_filesystem_types_own_descriptor_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    int oflags = 0;

    /* Access mode */
    if ((flags & WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ) &&
        (flags & WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE)) {
        oflags |= O_RDWR;
    } else if (flags & WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_WRITE) {
        oflags |= O_WRONLY;
    } else {
        oflags |= O_RDONLY;
    }

    /* Open flags */
    if (open_flags & WASI_FILESYSTEM_TYPES_OPEN_FLAGS_CREATE) oflags |= O_CREAT;
    if (open_flags & WASI_FILESYSTEM_TYPES_OPEN_FLAGS_DIRECTORY) oflags |= O_DIRECTORY;
    if (open_flags & WASI_FILESYSTEM_TYPES_OPEN_FLAGS_EXCLUSIVE) oflags |= O_EXCL;
    if (open_flags & WASI_FILESYSTEM_TYPES_OPEN_FLAGS_TRUNCATE) oflags |= O_TRUNC;

    /* Symlink handling */
    if (!(path_flags & WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW)) {
        oflags |= O_NOFOLLOW;
    }

    /* Sync flags */
    if (flags & WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_FILE_INTEGRITY_SYNC) oflags |= O_SYNC;
    if (flags & WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_DATA_INTEGRITY_SYNC) oflags |= O_DSYNC;

    int new_fd = openat(desc->fd, cpath, oflags, 0666);
    if (new_fd < 0) {
        free(cpath);
        *err = errno_to_error_code(errno);
        return false;
    }

    int32_t handle = wasi_descriptor_alloc(new_fd, true, flags, NULL);
    if (handle < 0) {
        close(new_fd);
        free(cpath);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    free(cpath);
    ret->__handle = handle;
    return true;
}

bool wasi_filesystem_types_method_descriptor_readlink_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    imports_string_t *path,
    imports_string_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    char buf[4096];
    ssize_t len = readlinkat(desc->fd, cpath, buf, sizeof(buf) - 1);
    free(cpath);

    if (len < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    ret->ptr = (uint8_t *)malloc((size_t)len);
    if (!ret->ptr) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }
    memcpy(ret->ptr, buf, (size_t)len);
    ret->len = (size_t)len;
    return true;
}

bool wasi_filesystem_types_method_descriptor_remove_directory_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    imports_string_t *path,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    if (unlinkat(desc->fd, cpath, AT_REMOVEDIR) < 0) {
        free(cpath);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cpath);
    return true;
}

bool wasi_filesystem_types_method_descriptor_rename_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    imports_string_t *old_path,
    wasi_filesystem_types_borrow_descriptor_t new_descriptor,
    imports_string_t *new_path,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *old_desc = wasi_descriptor_get(self.__handle);
    wasi_descriptor_resource_t *new_desc = wasi_descriptor_get(new_descriptor.__handle);
    if (!old_desc || !new_desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cold_path = wasi_string_to_cstr(old_path);
    char *cnew_path = wasi_string_to_cstr(new_path);
    if (!cold_path || !cnew_path) {
        free(cold_path);
        free(cnew_path);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    if (renameat(old_desc->fd, cold_path, new_desc->fd, cnew_path) < 0) {
        free(cold_path);
        free(cnew_path);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cold_path);
    free(cnew_path);
    return true;
}

bool wasi_filesystem_types_method_descriptor_symlink_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    imports_string_t *old_path,
    imports_string_t *new_path,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cold_path = wasi_string_to_cstr(old_path);
    char *cnew_path = wasi_string_to_cstr(new_path);
    if (!cold_path || !cnew_path) {
        free(cold_path);
        free(cnew_path);
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    if (symlinkat(cold_path, desc->fd, cnew_path) < 0) {
        free(cold_path);
        free(cnew_path);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cold_path);
    free(cnew_path);
    return true;
}

bool wasi_filesystem_types_method_descriptor_unlink_file_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    imports_string_t *path,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    if (unlinkat(desc->fd, cpath, 0) < 0) {
        free(cpath);
        *err = errno_to_error_code(errno);
        return false;
    }

    free(cpath);
    return true;
}

bool wasi_filesystem_types_method_descriptor_is_same_object(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_borrow_descriptor_t other
) {
    wasi_descriptor_resource_t *desc1 = wasi_descriptor_get(self.__handle);
    wasi_descriptor_resource_t *desc2 = wasi_descriptor_get(other.__handle);
    if (!desc1 || !desc2) return false;

    struct stat st1, st2;
    if (fstat(desc1->fd, &st1) < 0 || fstat(desc2->fd, &st2) < 0) {
        return false;
    }

    return (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
}

bool wasi_filesystem_types_method_descriptor_metadata_hash(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_metadata_hash_value_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    struct stat st;
    if (fstat(desc->fd, &st) < 0) {
        *err = errno_to_error_code(errno);
        return false;
    }

    /* Create a simple hash from inode and device */
    ret->lower = ((uint64_t)st.st_ino << 32) | (uint64_t)st.st_dev;
#ifdef __APPLE__
    ret->upper = ((uint64_t)st.st_mtimespec.tv_sec << 32) | (uint64_t)st.st_mtimespec.tv_nsec;
#else
    ret->upper = ((uint64_t)st.st_mtim.tv_sec << 32) | (uint64_t)st.st_mtim.tv_nsec;
#endif
    return true;
}

bool wasi_filesystem_types_method_descriptor_metadata_hash_at(
    wasi_filesystem_types_borrow_descriptor_t self,
    wasi_filesystem_types_path_flags_t path_flags,
    imports_string_t *path,
    wasi_filesystem_types_metadata_hash_value_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_descriptor_resource_t *desc = wasi_descriptor_get(self.__handle);
    if (!desc) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    char *cpath = wasi_string_to_cstr(path);
    if (!cpath) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }

    int flags = (path_flags & WASI_FILESYSTEM_TYPES_PATH_FLAGS_SYMLINK_FOLLOW) ? 0 : AT_SYMLINK_NOFOLLOW;

    struct stat st;
    if (fstatat(desc->fd, cpath, &st, flags) < 0) {
        free(cpath);
        *err = errno_to_error_code(errno);
        return false;
    }
    free(cpath);

    ret->lower = ((uint64_t)st.st_ino << 32) | (uint64_t)st.st_dev;
#ifdef __APPLE__
    ret->upper = ((uint64_t)st.st_mtimespec.tv_sec << 32) | (uint64_t)st.st_mtimespec.tv_nsec;
#else
    ret->upper = ((uint64_t)st.st_mtim.tv_sec << 32) | (uint64_t)st.st_mtim.tv_nsec;
#endif
    return true;
}

/* ============================================================================
 * Directory Entry Stream Methods
 * ============================================================================
 */

bool wasi_filesystem_types_method_directory_entry_stream_read_directory_entry(
    wasi_filesystem_types_borrow_directory_entry_stream_t self,
    wasi_filesystem_types_option_directory_entry_t *ret,
    wasi_filesystem_types_error_code_t *err
) {
    wasi_dir_stream_resource_t *stream = wasi_dir_stream_get(self.__handle);
    if (!stream || !stream->dir) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_BAD_DESCRIPTOR;
        return false;
    }

    errno = 0;
    struct dirent *entry = readdir(stream->dir);
    if (!entry) {
        if (errno != 0) {
            *err = errno_to_error_code(errno);
            return false;
        }
        /* End of directory */
        ret->is_some = false;
        return true;
    }

    ret->is_some = true;
    ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_UNKNOWN;

    /* Set type based on d_type if available */
#ifdef DT_REG
    switch (entry->d_type) {
        case DT_REG: ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_REGULAR_FILE; break;
        case DT_DIR: ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_DIRECTORY; break;
        case DT_LNK: ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_SYMBOLIC_LINK; break;
        case DT_BLK: ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_BLOCK_DEVICE; break;
        case DT_CHR: ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_CHARACTER_DEVICE; break;
        case DT_FIFO: ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_FIFO; break;
        case DT_SOCK: ret->val.type = WASI_FILESYSTEM_TYPES_DESCRIPTOR_TYPE_SOCKET; break;
        default: break;
    }
#endif

    /* Copy name */
    size_t name_len = strlen(entry->d_name);
    ret->val.name.ptr = (uint8_t *)malloc(name_len);
    if (!ret->val.name.ptr) {
        *err = WASI_FILESYSTEM_TYPES_ERROR_CODE_INSUFFICIENT_MEMORY;
        return false;
    }
    memcpy(ret->val.name.ptr, entry->d_name, name_len);
    ret->val.name.len = name_len;

    return true;
}

/* ============================================================================
 * Filesystem Error Code
 * ============================================================================
 */

bool wasi_filesystem_types_filesystem_error_code(
    wasi_filesystem_types_borrow_error_t err_,
    wasi_filesystem_types_error_code_t *ret
) {
    (void)err_;
    /* For now, just return a generic error */
    *ret = WASI_FILESYSTEM_TYPES_ERROR_CODE_IO;
    return true;
}

/* ============================================================================
 * Preopens
 * ============================================================================
 */

static struct {
    const char *path;
    int fd;
} preopens[] = {
    { "/", -1 },
    { ".", -1 },
};
static size_t num_preopens = 2;
static bool preopens_initialized = false;

static void init_preopens(void) {
    if (preopens_initialized) return;
    preopens_initialized = true;

    preopens[0].fd = open("/", O_RDONLY | O_DIRECTORY);
    preopens[1].fd = open(".", O_RDONLY | O_DIRECTORY);
}

void wasi_filesystem_preopens_get_directories(
    wasi_filesystem_preopens_list_tuple2_own_descriptor_string_t *ret
) {
    init_preopens();

    size_t count = 0;
    for (size_t i = 0; i < num_preopens; i++) {
        if (preopens[i].fd >= 0) count++;
    }

    if (count == 0) {
        ret->ptr = NULL;
        ret->len = 0;
        return;
    }

    ret->ptr = (wasi_filesystem_preopens_tuple2_own_descriptor_string_t *)malloc(
        count * sizeof(wasi_filesystem_preopens_tuple2_own_descriptor_string_t));
    if (!ret->ptr) {
        ret->len = 0;
        return;
    }

    size_t idx = 0;
    for (size_t i = 0; i < num_preopens && idx < count; i++) {
        if (preopens[i].fd < 0) continue;

        /* Duplicate fd for the descriptor */
        int new_fd = dup(preopens[i].fd);
        if (new_fd < 0) continue;

        int32_t handle = wasi_descriptor_alloc(new_fd, true,
            WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_READ | WASI_FILESYSTEM_TYPES_DESCRIPTOR_FLAGS_MUTATE_DIRECTORY,
            preopens[i].path);
        if (handle < 0) {
            close(new_fd);
            continue;
        }

        ret->ptr[idx].f0.__handle = handle;

        size_t path_len = strlen(preopens[i].path);
        ret->ptr[idx].f1.ptr = (uint8_t *)malloc(path_len);
        if (ret->ptr[idx].f1.ptr) {
            memcpy(ret->ptr[idx].f1.ptr, preopens[i].path, path_len);
            ret->ptr[idx].f1.len = path_len;
        } else {
            ret->ptr[idx].f1.len = 0;
        }

        idx++;
    }

    ret->len = idx;
}

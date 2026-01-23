/**
 * WASI Comparison Tests - Filesystem
 *
 * These tests use standard C APIs that map to WASI filesystem interfaces
 * when compiled for WebAssembly.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (errno=%d)\n", msg, errno); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    printf("  %s: PASS\n", name); \
    tests_passed++; \
} while(0)

/* Test: Current working directory exists */
static void test_cwd_exists(void) {
    char buf[4096];
    char *cwd = getcwd(buf, sizeof(buf));
    TEST_ASSERT(cwd != NULL, "getcwd failed");
    printf("    cwd: %s\n", cwd);
    TEST_PASS("cwd_exists");
}

/* Test: Can stat current directory */
static void test_stat_cwd(void) {
    struct stat st;
    int ret = stat(".", &st);
    TEST_ASSERT(ret == 0, "stat(.) failed");
    TEST_ASSERT(S_ISDIR(st.st_mode), ". is not a directory");
    printf("    . inode: %lu, mode: %o\n", (unsigned long)st.st_ino, st.st_mode & 0777);
    TEST_PASS("stat_cwd");
}

/* Test: Can read directory entries */
static void test_read_directory(void) {
    DIR *dir = opendir(".");
    TEST_ASSERT(dir != NULL, "opendir(.) failed");

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        count++;
        if (count <= 5) {  /* Print first 5 entries */
            printf("    entry: %s (type=%d)\n", entry->d_name, entry->d_type);
        }
    }

    closedir(dir);
    TEST_ASSERT(count >= 2, "directory has fewer than 2 entries (. and ..)");
    printf("    total entries: %d\n", count);
    TEST_PASS("read_directory");
}

/* Test: Can create and remove directory */
static void test_create_remove_directory(void) {
    const char *dirname = "test_wasi_temp_dir";

    /* Remove if exists */
    rmdir(dirname);

    /* Create directory */
    int ret = mkdir(dirname, 0755);
    TEST_ASSERT(ret == 0, "mkdir failed");

    /* Verify it exists */
    struct stat st;
    ret = stat(dirname, &st);
    TEST_ASSERT(ret == 0, "stat on new directory failed");
    TEST_ASSERT(S_ISDIR(st.st_mode), "created entry is not a directory");

    /* Remove directory */
    ret = rmdir(dirname);
    TEST_ASSERT(ret == 0, "rmdir failed");

    /* Verify it's gone */
    ret = stat(dirname, &st);
    TEST_ASSERT(ret == -1 && errno == ENOENT, "directory still exists after rmdir");

    TEST_PASS("create_remove_directory");
}

/* Test: Can create, write, read, and delete file */
static void test_file_operations(void) {
    const char *filename = "test_wasi_temp_file.txt";
    const char *content = "Hello from WASI test!\n";

    /* Remove if exists */
    unlink(filename);

    /* Create and write file */
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "open for write failed");

    ssize_t written = write(fd, content, strlen(content));
    TEST_ASSERT(written == (ssize_t)strlen(content), "write failed");
    close(fd);

    /* Read file back */
    fd = open(filename, O_RDONLY);
    TEST_ASSERT(fd >= 0, "open for read failed");

    char buf[256];
    ssize_t nread = read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread == (ssize_t)strlen(content), "read wrong size");
    buf[nread] = '\0';
    close(fd);

    TEST_ASSERT(strcmp(buf, content) == 0, "file content mismatch");

    /* Check file stats */
    struct stat st;
    int ret = stat(filename, &st);
    TEST_ASSERT(ret == 0, "stat on file failed");
    TEST_ASSERT(S_ISREG(st.st_mode), "file is not regular");
    TEST_ASSERT(st.st_size == (off_t)strlen(content), "file size mismatch");

    /* Delete file */
    ret = unlink(filename);
    TEST_ASSERT(ret == 0, "unlink failed");

    TEST_PASS("file_operations");
}

/* Test: lseek works correctly */
static void test_lseek(void) {
    const char *filename = "test_wasi_seek.txt";
    const char *content = "0123456789";

    unlink(filename);

    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "open failed");

    write(fd, content, strlen(content));

    /* Seek to beginning */
    off_t pos = lseek(fd, 0, SEEK_SET);
    TEST_ASSERT(pos == 0, "SEEK_SET to 0 failed");

    /* Seek to middle */
    pos = lseek(fd, 5, SEEK_SET);
    TEST_ASSERT(pos == 5, "SEEK_SET to 5 failed");

    /* Read from position 5 */
    char buf[10];
    ssize_t n = read(fd, buf, 5);
    TEST_ASSERT(n == 5, "read after seek failed");
    buf[5] = '\0';
    TEST_ASSERT(strcmp(buf, "56789") == 0, "wrong data after seek");

    /* Seek from end */
    pos = lseek(fd, -3, SEEK_END);
    TEST_ASSERT(pos == 7, "SEEK_END -3 failed");

    /* Seek from current position */
    pos = lseek(fd, -2, SEEK_CUR);
    TEST_ASSERT(pos == 5, "SEEK_CUR -2 failed");

    close(fd);
    unlink(filename);

    TEST_PASS("lseek");
}

/* Test: File truncation */
static void test_truncate(void) {
    const char *filename = "test_wasi_trunc.txt";
    const char *content = "0123456789ABCDEF";

    unlink(filename);

    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "open failed");

    write(fd, content, strlen(content));

    /* Truncate to 8 bytes */
    int ret = ftruncate(fd, 8);
    TEST_ASSERT(ret == 0, "ftruncate failed");

    /* Verify size */
    struct stat st;
    fstat(fd, &st);
    TEST_ASSERT(st.st_size == 8, "size after truncate wrong");

    /* Verify content */
    lseek(fd, 0, SEEK_SET);
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf));
    TEST_ASSERT(n == 8, "read after truncate wrong size");
    buf[n] = '\0';
    TEST_ASSERT(strcmp(buf, "01234567") == 0, "content after truncate wrong");

    close(fd);
    unlink(filename);

    TEST_PASS("truncate");
}

int main(void) {
    printf("=== Filesystem Comparison Tests ===\n");
#ifdef __wasi__
    printf("Platform: WASI (WebAssembly)\n");
#else
    printf("Platform: Native Linux\n");
#endif
    printf("\n");

    test_cwd_exists();
    test_stat_cwd();
    test_read_directory();
    test_create_remove_directory();
    test_file_operations();
    test_lseek();
    test_truncate();

    printf("\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

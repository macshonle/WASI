/**
 * WASI Detailed Investigation Tests
 *
 * This file investigates specific differences between native Linux
 * and Wasmtime's WASI Preview 2 implementation.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* Investigation: Directory entry type constants */
static void investigate_dirent_types(void) {
    printf("=== Directory Entry Type Constants ===\n");
#ifdef DT_UNKNOWN
    printf("DT_UNKNOWN = %d\n", DT_UNKNOWN);
#else
    printf("DT_UNKNOWN not defined\n");
#endif
#ifdef DT_FIFO
    printf("DT_FIFO    = %d\n", DT_FIFO);
#endif
#ifdef DT_CHR
    printf("DT_CHR     = %d\n", DT_CHR);
#endif
#ifdef DT_DIR
    printf("DT_DIR     = %d\n", DT_DIR);
#else
    printf("DT_DIR not defined\n");
#endif
#ifdef DT_BLK
    printf("DT_BLK     = %d\n", DT_BLK);
#endif
#ifdef DT_REG
    printf("DT_REG     = %d\n", DT_REG);
#else
    printf("DT_REG not defined\n");
#endif
#ifdef DT_LNK
    printf("DT_LNK     = %d\n", DT_LNK);
#endif
#ifdef DT_SOCK
    printf("DT_SOCK    = %d\n", DT_SOCK);
#endif
    printf("\n");
}

/* Investigation: stat mode bits */
static void investigate_stat_mode(void) {
    printf("=== Stat Mode Investigation ===\n");

    struct stat st;

    /* Current directory */
    if (stat(".", &st) == 0) {
        printf("stat(\".\")\n");
        printf("  st_mode (raw)  : 0x%x (%o octal)\n", st.st_mode, st.st_mode);
        printf("  st_mode & 0777 : %03o\n", st.st_mode & 0777);
        printf("  S_ISDIR        : %s\n", S_ISDIR(st.st_mode) ? "true" : "false");
        printf("  S_ISREG        : %s\n", S_ISREG(st.st_mode) ? "true" : "false");
        printf("  st_ino         : %lu\n", (unsigned long)st.st_ino);
        printf("  st_dev         : %lu\n", (unsigned long)st.st_dev);
        printf("  st_nlink       : %lu\n", (unsigned long)st.st_nlink);
        printf("  st_size        : %ld\n", (long)st.st_size);
    } else {
        printf("stat(\".\") failed: %s\n", strerror(errno));
    }
    printf("\n");

    /* Create a test file and stat it */
    const char *testfile = "investigate_test.txt";
    unlink(testfile);
    int fd = open(testfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "test", 4);
        close(fd);

        if (stat(testfile, &st) == 0) {
            printf("stat(\"%s\")\n", testfile);
            printf("  st_mode (raw)  : 0x%x (%o octal)\n", st.st_mode, st.st_mode);
            printf("  st_mode & 0777 : %03o\n", st.st_mode & 0777);
            printf("  S_ISDIR        : %s\n", S_ISDIR(st.st_mode) ? "true" : "false");
            printf("  S_ISREG        : %s\n", S_ISREG(st.st_mode) ? "true" : "false");
            printf("  st_ino         : %lu\n", (unsigned long)st.st_ino);
        } else {
            printf("stat(\"%s\") failed: %s\n", testfile, strerror(errno));
        }

        unlink(testfile);
    }
    printf("\n");
}

/* Investigation: readdir d_type values */
static void investigate_readdir(void) {
    printf("=== Directory Entry Investigation ===\n");

    DIR *dir = opendir(".");
    if (!dir) {
        printf("opendir(\".\") failed: %s\n", strerror(errno));
        return;
    }

    printf("Reading directory entries:\n");
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < 10) {
        printf("  [%d] d_name=\"%s\", d_type=%d", count, entry->d_name, entry->d_type);

        /* Interpret d_type based on platform */
#ifdef __wasi__
        /* WASI uses WASI-specific type values */
        switch (entry->d_type) {
            case 0: printf(" (unknown)"); break;
            case 1: printf(" (block-device)"); break;
            case 2: printf(" (character-device)"); break;
            case 3: printf(" (directory)"); break;
            case 4: printf(" (regular-file)"); break;
            case 5: printf(" (socket-dgram)"); break;
            case 6: printf(" (socket-stream)"); break;
            case 7: printf(" (symlink)"); break;
            case 14: printf(" (fifo)"); break;
            default: printf(" (?)"); break;
        }
#else
        /* Linux uses DT_* constants */
        switch (entry->d_type) {
            case DT_UNKNOWN: printf(" (DT_UNKNOWN)"); break;
            case DT_FIFO: printf(" (DT_FIFO)"); break;
            case DT_CHR: printf(" (DT_CHR)"); break;
            case DT_DIR: printf(" (DT_DIR)"); break;
            case DT_BLK: printf(" (DT_BLK)"); break;
            case DT_REG: printf(" (DT_REG)"); break;
            case DT_LNK: printf(" (DT_LNK)"); break;
            case DT_SOCK: printf(" (DT_SOCK)"); break;
            default: printf(" (?)"); break;
        }
#endif
        printf(", d_ino=%lu\n", (unsigned long)entry->d_ino);
        count++;
    }

    closedir(dir);
    printf("\n");
}

/* Investigation: file access flags */
static void investigate_file_flags(void) {
    printf("=== File Flag Investigation ===\n");

    const char *testfile = "flag_test.txt";
    unlink(testfile);

    /* Create file with specific mode */
    int fd = open(testfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("open() failed: %s\n", strerror(errno));
        return;
    }

    printf("Created file with mode 0644\n");

    /* Get file flags */
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) {
        printf("fcntl(F_GETFL) = 0x%x\n", flags);
        printf("  O_RDONLY: %s\n", (flags & O_ACCMODE) == O_RDONLY ? "yes" : "no");
        printf("  O_WRONLY: %s\n", (flags & O_ACCMODE) == O_WRONLY ? "yes" : "no");
        printf("  O_RDWR  : %s\n", (flags & O_ACCMODE) == O_RDWR ? "yes" : "no");
        printf("  O_APPEND: %s\n", (flags & O_APPEND) ? "yes" : "no");
    } else {
        printf("fcntl(F_GETFL) failed: %s\n", strerror(errno));
    }

    close(fd);

    /* Check actual permissions via stat */
    struct stat st;
    if (stat(testfile, &st) == 0) {
        printf("After stat: mode & 0777 = %03o\n", st.st_mode & 0777);
    }

    unlink(testfile);
    printf("\n");
}

/* Investigation: clock constants */
static void investigate_clocks(void) {
    printf("=== Clock Constants ===\n");
#ifdef CLOCK_REALTIME
    printf("CLOCK_REALTIME  = %d\n", CLOCK_REALTIME);
#else
    printf("CLOCK_REALTIME not defined\n");
#endif
#ifdef CLOCK_MONOTONIC
    printf("CLOCK_MONOTONIC = %d\n", CLOCK_MONOTONIC);
#else
    printf("CLOCK_MONOTONIC not defined\n");
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
    printf("CLOCK_PROCESS_CPUTIME_ID = %d\n", CLOCK_PROCESS_CPUTIME_ID);
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
    printf("CLOCK_THREAD_CPUTIME_ID = %d\n", CLOCK_THREAD_CPUTIME_ID);
#endif
    printf("\n");
}

int main(void) {
    printf("========================================\n");
    printf("WASI Investigation Report\n");
    printf("========================================\n");
#ifdef __wasi__
    printf("Platform: WASI (WebAssembly)\n");
#else
    printf("Platform: Native Linux\n");
#endif
    printf("\n");

    investigate_dirent_types();
    investigate_stat_mode();
    investigate_readdir();
    investigate_file_flags();
    investigate_clocks();

    printf("========================================\n");
    printf("Investigation Complete\n");
    printf("========================================\n");

    return 0;
}

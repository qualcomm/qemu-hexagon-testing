/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "strutils.h"

#define ERRNO_SENTINEL 1000 /* An invalid error number */

#define ERR(msg) do { \
        printf("fatal: %s: (%d) %s\n", msg, errno, strerror(errno)); \
        exit(1); \
    } while (0)

#define MAX 10

#define HEX_SYS_OPENDIR         0x180
#define HEX_SYS_CLOSEDIR        0x181
#define HEX_SYS_READDIR         0x182

#define DO_SWI(CODE, ARG0, ARG1, RET, ERR) \
    do { \
        asm volatile( \
                "r0 = %2\n" \
                "r1 = %3\n" \
                "r2 = %4\n" \
                "trap0(#0)\n" \
                "%0 = r0\n" \
                "%1 = r1\n" \
                : "=r"(RET), "=r"(ERR) \
                : "r"(CODE), "r"(ARG0), "r"(ARG1) \
                : "r0", "r1", "r2", "memory" \
                ); \
    } while (0)

static void cmp_with_direct_swi(const char *dname, char *expected_files[MAX])
{
    uint32_t dir_index, ret, err;
    /* OPENDIR */
    DO_SWI(HEX_SYS_OPENDIR, dname, 0, dir_index, err);
    assert(dir_index);

    /* READDIR */
    char found_files_buffer[4][256];
    char *found_files[4];
    for (int i = 0; 1; i++) {
        struct __attribute__((__packed__)) { int32_t _; char d_name[256]; } dirent;
        DO_SWI(HEX_SYS_READDIR, dir_index, &dirent, ret, err);
        if (!ret) {
            break;
        }
        assert(i < 4);
        found_files[i] = found_files_buffer[i];
        strcpy(found_files[i], dirent.d_name);
    }

    sort_str_arr(found_files, 4);
    for (int i = 0; i < 4; i++) {
        assert(!strcmp(found_files[i], expected_files[i]));
    }
    /* CLOSEDIR */
    DO_SWI(HEX_SYS_CLOSEDIR, dir_index, 0, ret, err);
    assert(!ret);
}

int main(int argc, char **argv)
{
    char *found_files[MAX];
    int n = 0;
    assert(argc == 2);
    DIR *dirp = opendir(argv[1]);
    if (!dirp) {
        ERR("couldn't open dir");
    }

    while (1) {
        errno = ERRNO_SENTINEL;
        struct dirent *dp = readdir(dirp);
        if (!dp) {
            /* FIXME: this check is currently disabled due
             * to a bug in hexagon-libc (see QTOOL-106440).
             */
            /*
            if (errno != ERRNO_SENTINEL) {
                ERR("error reading directory");
            }
            */
            break;
        }
        if (n < MAX) {
            found_files[n] = strdup(dp->d_name);
            assert(found_files[n]);
        } else {
            printf("fatal: cannot list more than %d files\n", MAX);
            return 1;
        }
        n++;
    }

    sort_str_arr(found_files, n);
    cmp_with_direct_swi(argv[1], found_files);
    bool first = true;
    for (int i = 0; i < n; i++) {
        printf("%s%s", first ? "" : " ", found_files[i]);
        free(found_files[i]);
        first = false;
    }

    if (closedir(dirp)) {
        ERR("closedir error");
    }
    return 0;
}

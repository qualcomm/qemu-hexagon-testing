/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* Minimal test helpers for QuRT user-process programs. */

#ifndef QT_H
#define QT_H 1

#include <qurt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QT_STACK_BYTES 16384
#define QT_MAX_TASKS 64

static volatile int qt_failures;

#define QT_CHECK(cond, fmt, ...)                                               \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s: " fmt "\n", __FILE__, __LINE__, #cond,     \
                   ##__VA_ARGS__);                                             \
            __atomic_fetch_add(&qt_failures, 1, __ATOMIC_SEQ_CST);             \
        }                                                                      \
    } while (0)

static inline int qt_finish(const char *name)
{
    if (qt_failures) {
        printf("FAIL %s (%d failures)\n", name, qt_failures);
        return 1;
    }
    printf("PASS %s\n", name);
    return 0;
}

static char qt_stacks[QT_MAX_TASKS][QT_STACK_BYTES] __attribute__((aligned(8)));
static qurt_thread_t qt_tids[QT_MAX_TASKS];

/* Start task i (< QT_MAX_TASKS) running fn(arg) at the given QuRT priority
 * (lower number == higher priority).  Returns the QuRT status code. */
static inline int qt_spawn(int i, void (*fn)(void *), void *arg, int prio)
{
    qurt_thread_attr_t attr;
    qurt_thread_attr_init(&attr);
    qurt_thread_attr_set_name(&attr, "qt");
    qurt_thread_attr_set_stack_addr(&attr, qt_stacks[i]);
    qurt_thread_attr_set_stack_size(&attr, QT_STACK_BYTES);
    qurt_thread_attr_set_priority(&attr, prio);
    int rc = qurt_thread_create(&qt_tids[i], &attr, fn, arg);
    QT_CHECK(rc == QURT_EOK, "qurt_thread_create(%d) -> %d", i, rc);
    return rc;
}

/* Wait for tasks [0, n). Each task must end by returning or qurt_thread_exit(). */
static inline void qt_join_all(int n)
{
    for (int i = 0; i < n; i++) {
        int status;
        qurt_thread_join(qt_tids[i], &status);
    }
}

/* QuRT has no yield call; re-setting the current priority moves the thread to
 * the back of its priority's ready queue. */
static inline void qt_yield(void)
{
    qurt_thread_t me = qurt_thread_get_id();
    qurt_thread_set_priority(me, qurt_thread_get_priority(me));
}

static inline uint32_t qt_lcg(uint32_t x)
{
    return x * 1664525u + 1013904223u;
}

#endif

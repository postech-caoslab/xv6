/* q2_harness.c — Quiz Q2 incidental code (instructor supplied; do not edit). */
#include "instrument.h"
#include "q2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void initlock(struct spinlock *lk, char *name)
{
    lk->name = name;
    lk->locked = 0;
}

const char *diag_role_of(const struct sem *s); /* diagnosis.c */

void sem_init(struct sem *s, int initial_value)
{
    if (initial_value < 0) {
        diagnosis_violation("in bb_init(): sem_init(&bb->%s, %d) with a negative value — a "
                            "semaphore's value counts available units (treated as 0)",
                            diag_role_of(s), initial_value);
        initial_value = 0;
    }
    /* A semaphore with a declared role belongs to a live buffer: re-initializing it from
     * put()/get() resets the count or frees a mutex another thread holds. */
    if (strcmp(diag_role_of(s), "?") != 0 && !diag_in_init_phase()) {
        char where[160];

        diagnosis_violation("%s: sem_init(&bb->%s, %d) outside bb_init() — re-initializing a "
                            "running buffer's semaphore resets its count (or frees a mutex "
                            "another thread holds)", diag_where(where, sizeof where, NULL),
                            diag_role_of(s), initial_value);
    }
    initlock(&s->lock, "sem-internal");
    s->value = initial_value;
}

/* bb_create pre-loads this sentinel into every semaphore's value; a semaphore still holding
 * it after bb_init() returned was never sem_init'ed by the student. */
#define SEM_UNINITIALIZED (-77777777) /* impossible for a real count */

void bb_create(struct bounded_buffer *bb, size_t capacity)
{
    bb->buffer = malloc(capacity * sizeof *bb->buffer);
    if (!bb->buffer || capacity == 0) {
        fprintf(stderr, "FATAL: cannot allocate a buffer of capacity %zu\n", capacity);
        exit(COURSE_EXIT_FRAMEWORK);
    }
    bb->capacity = capacity;
    bb->fill = bb->use = 0;

    /* Safe defaults + role declarations first, so bb_init's sem_init calls land on live
     * locks and every later diagnostic can say which semaphore was misused. */
    struct sem *sems[] = {&bb->empty, &bb->full, &bb->mutex};

    for (size_t i = 0; i < 3; i++) {
        initlock(&sems[i]->lock, "sem-internal");
        sems[i]->value = SEM_UNINITIALIZED; /* not via sem_init: that path rejects negatives */
    }
    diag_declare(&bb->empty, "empty", false);
    diag_declare(&bb->full, "full", false);
    diag_declare(&bb->mutex, "mutex", true);

    diag_init_phase(true);
    bb_init(bb); /* the student's three sem_init calls */
    diag_init_phase(false);

    const struct {
        struct sem *s;
        const char *role;
        int want;
    } expect[] = {
        {&bb->empty, "empty", (int)capacity},
        {&bb->full, "full", 0},
        {&bb->mutex, "mutex", 1},
    };

    for (size_t i = 0; i < sizeof expect / sizeof expect[0]; i++) {
        if (expect[i].s->value == SEM_UNINITIALIZED)
            diagnosis_violation("bb_init() returned without calling sem_init(&bb->%s, ...)",
                                expect[i].role);
        else if (expect[i].s->value != expect[i].want)
            diagnosis_violation("in bb_init(): sem_init(&bb->%s, %d) does not match what the "
                                "%s semaphore counts in a new, empty buffer of capacity %zu",
                                expect[i].role, expect[i].s->value, expect[i].role, capacity);
    }
}

void bb_destroy(struct bounded_buffer *bb)
{
    free(bb->buffer);
    bb->buffer = NULL;
}

bool bb_quiescent_ok(struct bounded_buffer *bb, size_t filled)
{
    bool ok = true;

    if (bb->mutex.value != 1) {
        course_line_break_if_open();
        printf("    INVARIANT: the mutex semaphore's value is %d while no put()/get() is "
               "running (expected 1: free)\n",
               bb->mutex.value);
        ok = false;
    }
    if (bb->empty.value != (int)(bb->capacity - filled) || bb->full.value != (int)filled) {
        course_line_break_if_open();
        printf("    INVARIANT: semaphore values (empty=%d, full=%d) do not match the buffer "
               "occupancy (%zu of %zu slots filled) while no put()/get() is running\n",
               bb->empty.value, bb->full.value, filled, bb->capacity);
        ok = false;
    }
    if (bb->empty.lock.locked || bb->full.lock.locked || bb->mutex.lock.locked) {
        course_line_break_if_open();
        printf("    INVARIANT: a semaphore's internal lock word is still 1 while no "
               "put()/get() is running (empty=%u full=%u mutex=%u)\n", bb->empty.lock.locked, bb->full.lock.locked,
               bb->mutex.lock.locked);
        ok = false;
    }
    if (bb->fill >= bb->capacity || bb->use >= bb->capacity) {
        course_line_break_if_open();
        printf("    INVARIANT: fill (%zu) or use (%zu) is outside the slot range 0..%zu "
               "(capacity %zu)\n", bb->fill, bb->use, bb->capacity - 1, bb->capacity);
        ok = false;
    } else if (bb->fill != (bb->use + filled) % bb->capacity) {
        course_line_break_if_open();
        printf("    INVARIANT: fill (%zu) != (use (%zu) + %zu filled) %% capacity (%zu) — "
               "the indexes no longer describe one consistent ring\n",
               bb->fill, bb->use, filled, bb->capacity);
        ok = false;
    }
    return ok;
}

void *q2_producer_worker(void *arg)
{
    q2_producer_args_t *w = arg;

    for (unsigned i = 0; i < w->count; i++) {
        diag_call_begin("put");
        put(w->bb, item_make(w->producer_id, i));
        diag_call_end();
    }
    return NULL;
}

void *q2_consumer_worker(void *arg)
{
    q2_consumer_args_t *w = arg;

    for (;;) {
        diag_call_begin("get");
        item_t it = get(w->bb);

        diag_call_end();
        if (item_is_poison(it))
            return NULL;
        item_log_append(&w->log, it);
    }
}

void q2_send_poison(struct bounded_buffer *bb, unsigned nconsumers)
{
    for (unsigned i = 0; i < nconsumers; i++) {
        diag_call_begin("put");
        put(bb, item_poison());
        diag_call_end();
    }
}

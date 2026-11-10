/* bb_harness.c — Homework B incidental code (instructor supplied; do not edit). */
#include "bounded_buffer.h"
#include "instrument.h"

#include <stdio.h>
#include <stdlib.h>

void bb_create(struct bounded_buffer *bb, size_t capacity)
{
    bb->buffer = malloc(capacity * sizeof *bb->buffer);
    if (!bb->buffer || capacity == 0) {
        fprintf(stderr, "FATAL: cannot allocate a buffer of capacity %zu\n", capacity);
        exit(COURSE_EXIT_FRAMEWORK);
    }
    bb->capacity = capacity;
    bb->fill = bb->use = 0;

    /* Declare the roles first, so bb_init's sem_init calls land on live, named semaphores
     * and every later diagnostic can say which one was misused. */
    sem_prepare(&bb->empty, "empty", false);
    sem_prepare(&bb->full, "full", false);
    sem_prepare(&bb->mutex, "mutex", true);

    semstat_init_phase(true);
    bb_init(bb); /* the student's three sem_init calls */
    semstat_init_phase(false);

    const struct {
        struct sem *s;
        int want;
        const char *why;
    } expect[] = {
        {&bb->empty, (int)capacity, "empty counts EMPTY slots — before anything runs, ALL "
                                    "slots are empty, so it starts at the capacity"},
        {&bb->full, 0, "full counts FILLED slots — nothing has been put yet, so it starts "
                       "at 0"},
        {&bb->mutex, 1, "mutex is the lock: a binary semaphore starts at 1 (free), so the "
                        "first sem_wait takes it and does not sleep"},
    };

    for (size_t i = 0; i < sizeof expect / sizeof expect[0]; i++) {
        if (!expect[i].s->student_inited)
            semstat_violation("bb_init() never called sem_init(&bb->%s, ...) — every "
                              "semaphore needs a defined starting value (lecture 9)",
                              expect[i].s->role);
        else if (semstat_value(expect[i].s) != expect[i].want)
            semstat_violation("sem_init(&bb->%s, %d) is the wrong starting value: %s "
                              "(capacity %zu)", expect[i].s->role,
                              semstat_value(expect[i].s), expect[i].why, capacity);
    }
}

void bb_destroy(struct bounded_buffer *bb)
{
    free(bb->buffer);
    bb->buffer = NULL;
    pthread_mutex_destroy(&bb->empty.m);
    pthread_cond_destroy(&bb->empty.cv);
    pthread_mutex_destroy(&bb->full.m);
    pthread_cond_destroy(&bb->full.cv);
    pthread_mutex_destroy(&bb->mutex.m);
    pthread_cond_destroy(&bb->mutex.cv);
}

bool bb_quiescent_ok(struct bounded_buffer *bb, size_t filled)
{
    int e = semstat_value(&bb->empty);
    int f = semstat_value(&bb->full);
    int m = semstat_value(&bb->mutex);
    bool ok = true;

    if (m != 1) {
        course_line_break_if_open();
        printf("    INVARIANT: the mutex semaphore's value is %d at a quiescent point — it "
               "must be 1 (free): was a sem_post(&mutex) skipped, or posted twice?\n", m);
        ok = false;
    }
    if (e != (int)(bb->capacity - filled) || f != (int)filled) {
        course_line_break_if_open();
        printf("    INVARIANT: semaphore values (empty=%d, full=%d) do not match the buffer "
               "occupancy (%zu of %zu slots filled) — every put consumes one 'empty' and "
               "posts one 'full'; every get does the reverse\n",
               e, f, filled, bb->capacity);
        ok = false;
    }
    if (bb->fill >= bb->capacity || bb->use >= bb->capacity) {
        course_line_break_if_open();
        printf("    INVARIANT: fill (%zu) or use (%zu) is outside the ring (capacity %zu) — "
               "indexes must wrap with %% capacity\n", bb->fill, bb->use, bb->capacity);
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

void *bb_producer_worker(void *arg)
{
    bb_producer_args_t *w = arg;

    for (unsigned i = 0; i < w->count; i++) {
        semstat_call_begin("put");
        put(w->bb, item_make(w->producer_id, i));
        semstat_call_end();
    }
    return NULL;
}

void *bb_consumer_worker(void *arg)
{
    bb_consumer_args_t *w = arg;

    for (;;) {
        semstat_call_begin("get");
        item_t it = get(w->bb);

        semstat_call_end();
        if (item_is_poison(it))
            return NULL;
        item_log_append(&w->log, it);
    }
}

void bb_send_poison(struct bounded_buffer *bb, unsigned nconsumers)
{
    for (unsigned i = 0; i < nconsumers; i++) {
        semstat_call_begin("put");
        put(bb, item_poison());
        semstat_call_end();
    }
}

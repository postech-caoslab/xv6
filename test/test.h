/* test.h -- shared by the test files in test/. DO NOT MODIFY. */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "sim.h"

#define PG(n)   ((uint)(n) << 12)           /* address of page n */

extern int verbose;                         /* one test: print the details */
void say(const char *fmt, ...);             /* add to the test's message */

/* fail the test with a message if cond is false (or the simulator saw an error) */
#define CHECK(cond, ...) do {                                   \
    int ok_ = (cond);                                           \
    if(sim.error){ say("%s\n", sim.error); return 1; }          \
    if(!ok_){ say(__VA_ARGS__); say("\n"); return 1; }          \
  } while(0)

/* the test file (test_swap.c) defines these */
struct test { const char *id, *name; int pts; int (*fn)(void); };
extern const struct test tests[];
extern const int   ntests;
extern const char *title;
extern const char *trace_id;

#endif

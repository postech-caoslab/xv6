#!/bin/sh
# run-all-tests.sh — build, then run the three test binaries in the order the grader does.
# Nothing in here is magic: every line is a command you can type yourself.
#
#   bounded_ring_buffer_basic-test    the plain build — your fast edit-build-run loop (~4 s)
#   bounded_ring_buffer_addr-test     the same tests compiled with AddressSanitizer + UndefinedBehaviorSanitizer:
#                                     memory errors and undefined behaviour get an exact report instead of a crash
#   bounded_ring_buffer_thread-test   the same tests compiled with ThreadSanitizer: finds data races that happen
#                                     to produce the right answer — the plain build cannot see those
#
# Any argument is passed on to every binary: a test name runs only that test, --list lists them
# (./bounded_ring_buffer_basic-test --list).
set -e                           # stop at the first failing step: fix that before the later builds matter
cd "$(dirname "$0")"             # the tests open trace/*.trace relative to the module directory

make                             # (re)builds whatever changed — all three binaries, same sources

# The watchdog: a test still running after COURSE_TEST_TIMEOUT_S seconds is killed and reported as a
# probable deadlock (exit 124). The built-in default is 30; uncomment to change it for this run.
# export COURSE_TEST_TIMEOUT_S=30
# HW_STRESS_SCALE multiplies the iteration counts of the stress tests (default 1).
# export HW_STRESS_SCALE=1

./bounded_ring_buffer_basic-test "$@"
./bounded_ring_buffer_addr-test "$@"

# ThreadSanitizer is not available on every toolchain. If the binary cannot even list its tests,
# say so and stop here: phases 1 and 2 passed, and the grader runs the TSan build on its machine.
if ! ./bounded_ring_buffer_thread-test --list >/dev/null 2>&1; then
    echo "### bounded_ring_buffer_thread-test cannot start on this machine (no ThreadSanitizer runtime);"
    echo "### the grader runs it for you — the first two builds passed."
    exit 0
fi
./bounded_ring_buffer_thread-test "$@"

echo "### all three builds passed — this is what the grader checks"

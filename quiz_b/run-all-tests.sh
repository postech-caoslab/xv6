#!/bin/sh
# Full Linux preflight: each case runs in its own process, and every phase is required.
# macOS/Windows local workflow: sh ./run-all-tests.sh --basic [test-name ...]
# Test names run only those cases (a partial run); --list or --help prints the test list/usage.
set -eu
cd "$(dirname "$0")"
export COURSE_RUN_ALL=1 # the basic binary skips its "also run the other builds" reminder
basic_only=0
if [ "${1:-}" = --basic ]; then
    basic_only=1
    shift
fi
if [ "$basic_only" -eq 1 ]; then
    make bounded_ring_buffer_basic-test
else
    make all
fi
for arg in "$@"; do
    case "$arg" in
        --list|--help|-h)
            ./bounded_ring_buffer_basic-test "$@"
            exit 0
            ;;
    esac
done
run_cases() {
    binary=$1
    shift
    if [ "$#" -gt 0 ]; then
        "$binary" "$@"
        return
    fi
    cases=$("$binary" --list)
    count=0
    while read -r name description; do
        case "$name" in ''|\#*) continue ;; esac
        count=$((count + 1))
        "$binary" "$name"
    done <<EOF
$cases
EOF
    [ "$count" -gt 0 ]
}
# Preserve a user's explicit timeout; sanitizer defaults allow instrumentation overhead.
COURSE_TEST_TIMEOUT_S=${COURSE_TEST_TIMEOUT_S:-20} run_cases ./bounded_ring_buffer_basic-test "$@"
if [ "$basic_only" -eq 1 ]; then
    if [ "$#" -gt 0 ]; then
        echo '### selected cases passed in the basic build (partial run)'
    else
        echo '### basic build passed; the AMD64 Linux judge also requires ASan/UBSan and TSan'
    fi
    exit 0
fi
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}halt_on_error=1:detect_leaks=1"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1:print_stacktrace=1"
export TSAN_OPTIONS="${TSAN_OPTIONS:+$TSAN_OPTIONS:}halt_on_error=1:exitcode=66"
COURSE_TEST_TIMEOUT_S=${COURSE_TEST_TIMEOUT_S:-60} run_cases ./bounded_ring_buffer_addr-test "$@"
COURSE_TEST_TIMEOUT_S=${COURSE_TEST_TIMEOUT_S:-60} run_cases ./bounded_ring_buffer_thread-test "$@"
if [ "$#" -gt 0 ]; then
    echo '### selected cases passed in all three builds (partial run; run with no test names for the full check)'
else
    echo '### all three builds passed'
fi

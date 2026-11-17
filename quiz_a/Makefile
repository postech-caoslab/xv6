# Quiz Q1 — atomic N-account transaction with ordered locks.
#
#   make                 build the three test binaries against YOUR src/transaction.c:
#                          bank_transfer_basic-test    plain build — the fast loop while you work
#                          bank_transfer_addr-test     the same tests under AddressSanitizer + UBSan
#                          bank_transfer_thread-test   the same tests under ThreadSanitizer (data races)
#                        then run them yourself — ./run-all-tests.sh runs all three in order
#   make IMPL=<file> TAG=<id>   build the three binaries from another transaction.c into build/
#                        (grading: build/bank_transfer_basic-test.<id>, _addr-test.<id>, _thread-test.<id>)
#   make clean
#
# Works with GNU make + gcc or clang on Linux, macOS, and WSL2.

CC     ?= cc
CFLAGS ?= -O2 -g
WARN   := -Wall -Wextra -Werror
# The test framework (check.{h,c}, course_runtime.{h,c}) is shared by every module and lives
# once in ../common; everything module-specific stays in this directory.
COMMON := ../common
BASE   := -std=c11 -pthread -D_POSIX_C_SOURCE=200809L -Iinclude -I$(COMMON)/include
NAME   := bank_transfer

IMPL ?= src/transaction.c
ifeq ($(IMPL),src/transaction.c)
TAG    ?= student
OUT    := .
SUFFIX :=
else
TAG    ?= $(notdir $(basename $(IMPL)))
OUT    := build
SUFFIX := .$(TAG)
endif

BASIC  := $(OUT)/$(NAME)_basic-test$(SUFFIX)
ADDR   := $(OUT)/$(NAME)_addr-test$(SUFFIX)
THREAD := $(OUT)/$(NAME)_thread-test$(SUFFIX)

HARNESS := src/q1_harness.c src/lock_check.c $(COMMON)/src/check.c $(COMMON)/src/course_runtime.c
SRCS    := src/main.c $(HARNESS) $(IMPL)
HDRS    := $(wildcard include/*.h) $(wildcard $(COMMON)/include/*.h)
# HW_IMPL_NAME labels the banner. HW_VARIANT names the build in the banner, and lets the basic
# build end with a reminder to run the other two (the grader runs all three).
# COURSE_TEST_TIMEOUT_DEFAULT_S: this quiz's watchdog default in seconds (the runner's built-in
# default is 30); the COURSE_TEST_TIMEOUT_S environment variable still overrides it.
DEFS    := -DHW_IMPL_NAME=\"$(TAG)\" -DCOURSE_TEST_TIMEOUT_DEFAULT_S=10

all: $(BASIC) $(ADDR) $(THREAD)

# If this directory is copied to another machine WITH its binaries, they were built for a
# different OS/arch and fail with "cannot execute: required file not found". A host-fingerprint
# stamp forces a clean rebuild the first time make runs on a new machine.
HOSTTAG   := $(shell uname -s)-$(shell uname -m)-$(subst /,_,$(shell $(CC) -dumpmachine 2>/dev/null))
HOSTSTAMP := build/host.$(HOSTTAG)

$(HOSTSTAMP):
	@rm -rf build $(NAME)_basic-test $(NAME)_addr-test $(NAME)_thread-test
	@mkdir -p build
	@touch $@

$(BASIC): $(SRCS) $(HDRS) Makefile $(HOSTSTAMP)
	@mkdir -p $(OUT)
	$(CC) $(BASE) $(CFLAGS) $(WARN) $(DEFS) -DHW_VARIANT=\"basic\" \
	    -DHW_SIBLINGS='"$(ADDR) and $(THREAD)"' -o $@ $(SRCS)

$(ADDR): $(SRCS) $(HDRS) Makefile $(HOSTSTAMP)
	@mkdir -p $(OUT)
	$(CC) $(BASE) -DCOURSE_SANITIZED=1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer $(CFLAGS) $(WARN) $(DEFS) \
	    -DHW_VARIANT=\"addr\" -o $@ $(SRCS)

$(THREAD): $(SRCS) $(HDRS) Makefile $(HOSTSTAMP)
	@mkdir -p $(OUT)
	$(CC) $(BASE) -DCOURSE_SANITIZED=1 -fsanitize=thread $(CFLAGS) $(WARN) $(DEFS) -DHW_VARIANT=\"thread\" -o $@ $(SRCS)

clean:
	rm -rf build $(NAME)_basic-test $(NAME)_addr-test $(NAME)_thread-test

.PHONY: all clean

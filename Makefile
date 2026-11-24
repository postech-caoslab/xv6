# make clean; make         build, then ./swap_sim      (./swap_sim 4: one test, with details)
#
# You edit only quiz/.  include/, sim/ and test/ must not be changed.

CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
         -Wno-unused-but-set-variable -O1 -g -Iinclude -Isim -Itest

QUIZ = quiz/kalloc.c quiz/swap.c
SIM  = sim/memory.c sim/disk.c sim/fifo.c sim/run.c sim/walkpgdir.c sim/testapi.c
HDRS = include/vm.h include/sim.h sim/internal.h test/test.h

.PHONY: all clean

all: swap_sim

swap_sim: $(QUIZ) $(SIM) test/main.c test/test_swap.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(QUIZ) $(SIM) test/main.c test/test_swap.c

clean:
	rm -f swap_sim

# make clean; make         build both, then ./rr_sched ; ./cfs_sched
# make clean; make rr      Quiz 1 only, then ./rr_sched
# make clean; make cfs     Quiz 2 only, then ./cfs_sched

CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g

.PHONY: all rr cfs clean

all: rr_sched cfs_sched

rr:  rr_sched
cfs: cfs_sched

rr_sched: sched.c main.c sched.h
	$(CC) $(CFLAGS) -DBUILD_RR -o $@ sched.c main.c

cfs_sched: sched.c main.c sched.h
	$(CC) $(CFLAGS) -DBUILD_CFS -o $@ sched.c main.c

clean:
	rm -f rr_sched cfs_sched

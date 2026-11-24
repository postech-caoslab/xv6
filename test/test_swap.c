/* test_swap.c -- the tests (./swap_sim) and the answer key of trace 4. DO NOT MODIFY.
 *   test 1 = Quiz 1 swap_out(), 2 = Quiz 2 kalloc(), 3 = Quiz 3 swap_in(), 4 = full trace */
#include "test.h"

/* [4] 6 pages; memory has 6 frames: the page directory, 1 page table, 4 for pages.
 * A(n): allocate page n (its first use), R(n) / W(n): read / write page n */
#define A(n) { ALLOC, PG(n) }
#define R(n) { READ,  PG(n) }
#define W(n) { WRITE, PG(n) }
static const struct op trace[] = {
  A(1), W(1), A(2), W(2), A(3), W(3), A(4), W(4),
  R(1), A(5), W(5), R(1), R(2),
  A(6), W(6), R(3), R(1), W(4),
  R(5), R(6), W(1), R(2),
  R(3), W(3), R(6), R(4),
  R(1), R(5), W(2), R(6),
};
static const uint want[] = {                /* pages in swap-out order */
  1, 2, 3, 4, 5, 1, 2, 6, 3, 4, 5, 1, 2, 3, 6, 4,
};
static const int nwant  = 16;
static const int wantin = 14;               /* swap-ins */

/* the FIFO queue is exactly the pages p[0..n-1], oldest (tail) first */
static int
queue_is(const int *p, int n)
{
  uint l[NPTENTRIES];
  int  i;

  if(sim_queue(l, NPTENTRIES) != n)
    return 0;
  for(i = 0; i < n; i++)
    if(l[i] != PG(p[i]))
      return 0;
  return 1;
}

/* memory: the page directory, 1 page table, 4 frames for pages */
#define NFRAME 6

/* swap_out tests: pages 1..npage in memory (page 1 in frame 1, the page table in
 * frame 2), and page 9 in slot 1, so page 1 goes to slot 2: the slot number and the
 * frame number differ */
static void
setup_out(int npage)
{
  int i;

  sim_reset(NFRAME);
  for(i = 1; i <= npage; i++)
    sim_map(PG(i));
  sim_put_in_swap(PG(9));
}

/* the first slot in use other than slot 1, or 0 */
static int
used_slot(void)
{
  int s;

  for(s = 2; s < NSLOT; s++)
    if(sim_slot_used(s))
      return s;
  return 0;
}

/* [1] swap_out: takes the tail (oldest) of the queue and updates its PTE */
static int
t_out_pte(void)
{
  static const int rest[] = { 2, 3 };
  uint p2, p3, pte;
  int   s;

  setup_out(3);
  p2 = *sim_pte(PG(2));
  p3 = *sim_pte(PG(3));

  swap_out();
  CHECK(*sim_pte(PG(2)) == p2 && *sim_pte(PG(3)) == p3,
        "swap_out() changed the PTE of page 2 or 3; only page 1 (the oldest) goes out");
  pte = *sim_pte(PG(1));
  CHECK(!(pte & PTE_P), "after swap_out(), page 1 (the oldest) still has PTE_P");
  CHECK(pte != 0 && PGOFF(pte) == 0,
        "the PTE of page 1 is 0x%08X; it should be slot << 12", pte);
  s = (int)PTE_SLOT(pte);
  CHECK(sim_slot_used(s) && !sim_slot_has(s, PG(9)),
        "the PTE of page 1 points to slot %d, which is not the slot from slotalloc()", s);
  CHECK(queue_is(rest, 2), "the FIFO queue (oldest first) should be page 2, 3: take "
        "page 1 off with dequeue()");
  return 0;
}

/* [1] swap_out: the data goes to the slot, the frame is freed */
static int
t_out_data(void)
{
  int f1, s;

  setup_out(3);
  f1 = (int)(PTE_ADDR(*sim_pte(PG(1))) >> 12);

  swap_out();
  CHECK(sim.nout == 1, "swap_out() wrote %d pages to swap; expected 1 (swapwrite())",
        sim.nout);
  CHECK(slots_in_use() == 2, "%d slots are in use; expected 2 (page 9 is in slot 1)",
        slots_in_use());
  s = used_slot();
  CHECK(sim_slot_has(s, PG(1)), "slot %d does not hold the data of page 1", s);
  CHECK(!sim_frame_used(f1), "the frame of page 1 (frame %d) was not freed (kfree())", f1);
  CHECK(pages_in_memory() == 2, "%d frames hold pages; expected 2", pages_in_memory());
  return 0;
}

/* [1] swap_out: oldest first */
static int
t_out_fifo(void)
{
  int i, k = 0;

  setup_out(4);
  for(i = 1; i <= 4; i++)
    swap_out();
  for(i = 0; i < sim.nevent; i++){
    if(sim.events[i].type != EV_OUT)
      continue;
    k++;
    CHECK(sim.events[i].va == PG(k), "swap-out %d took page %u; expected page %d",
          k, sim.events[i].va >> 12, k);
  }
  CHECK(k == 4, "4 calls of swap_out() wrote %d pages to swap; expected 4", k);
  for(i = 1; i <= 4; i++)
    CHECK(sim_slot_has((int)PTE_SLOT(*sim_pte(PG(i))), PG(i)),
          "the PTE of page %d does not point to the slot that holds its data", i);
  CHECK(pages_in_memory() == 0 && slots_in_use() == 5,
        "after 4 swap-outs: %d pages in memory, %d slots in use; expected 0 and 5",
        pages_in_memory(), slots_in_use());

  CHECK(sim_check() == 0, "-");
  return 0;
}

/* [2] kalloc: memory is full (uses swap_out) */
static int
t_kalloc_full(void)
{
  uint pa;

  sim_reset(NFRAME);
  sim_map(PG(1)); sim_map(PG(2)); sim_map(PG(3)); sim_map(PG(4));

  pa = kalloc();
  CHECK(pa != 0, "kalloc() returned 0 with memory full: call swap_out() and try again "
        "(this test needs a working swap_out())");
  CHECK(sim_frame(pa) >= 0, "kalloc() returned 0x%08X, which is not the address of a frame "
        "in use", pa);
  CHECK(sim.nout == 1, "kalloc() swapped out %d pages; expected 1", sim.nout);
  CHECK(!(*sim_pte(PG(1)) & PTE_P), "page 1 (the oldest) was not swapped out");
  CHECK(frames_in_use() == NFRAME - 1, "%d frames are in use; expected %d (memory is full)",
        frames_in_use(), NFRAME - 1);

  return 0;
}

/* swap page 3 back in; 2 frames are free */
static int
swap_in_page3(int *slot)
{
  uint *pte;

  sim_reset(NFRAME);
  sim_map(PG(1)); sim_map(PG(2));
  sim_put_in_swap(PG(3));
  pte   = sim_pte(PG(3));
  *slot = (int)PTE_SLOT(*pte);

  swap_in(pte);
  return 0;
}

/* [3] swap_in: the page comes back with its data */
static int
t_in_data(void)
{
  uint pte;
  int   f, s;

  if(swap_in_page3(&s))
    return 1;
  pte = *sim_pte(PG(3));
  CHECK(pte & PTE_P, "after swap_in(), the PTE of page 3 does not have PTE_P");
  f = (int)(PTE_ADDR(pte) >> 12);
  CHECK(sim_frame_used(f), "the PTE of page 3 points to frame %d, which is not in use", f);
  CHECK(sim_has_data(PG(3)), "page 3 does not hold its data after swap_in() (swapread())");
  CHECK(sim.nout == 0, "swap_in() swapped a page out although frames were free");
  return 0;
}

/* [3] swap_in: the slot is freed, the page goes to the head of the queue */
static int
t_in_queue(void)
{
  static const int all[] = { 1, 2, 3 };
  int s;

  if(swap_in_page3(&s))
    return 1;
  CHECK(!sim_slot_used(s), "swap_in() did not free slot %d (slotfree())", s);
  CHECK(queue_is(all, 3), "page 3 is not at the head (newest end) of the FIFO queue (enqueue())");
  CHECK(sim_check() == 0, "-");
  return 0;
}

/* [4] swap_in: memory is full (uses kalloc and swap_out) */
static int
t_in_full(void)
{
  static const int all[] = { 2, 3, 4, 5 };
  uint p1;

  sim_reset(NFRAME);
  sim_map(PG(1)); sim_map(PG(2)); sim_map(PG(3)); sim_map(PG(4));
  sim_put_in_swap(PG(5));

  swap_in(sim_pte(PG(5)));
  p1 = *sim_pte(PG(1));
  CHECK(!(p1 & PTE_P), "page 1 (the oldest) should be swapped out to make room");
  CHECK(sim_slot_has((int)PTE_SLOT(p1), PG(1)), "the data of page 1 is not in its slot");
  CHECK(sim_has_data(PG(5)), "page 5 does not hold its data after swap_in()");
  CHECK(queue_is(all, 4), "the FIFO queue (oldest first) should be page 2, 3, 4, 5");
  CHECK(sim_check() == 0, "-");
  return 0;
}

/* [4] the whole trace through vmrun() */
static int
t_trace(void)
{
  uint got[MAXEVENT];
  int  i, k, n = 0, diff = -1;

  sim_reset(NFRAME);
  sim.ops = trace;
  sim.nop = (int)(sizeof trace / sizeof trace[0]);
  vmrun();

  for(i = 0; i < sim.nevent; i++)
    if(sim.events[i].type == EV_OUT)
      got[n++] = sim.events[i].va >> 12;
  for(k = 0; k < nwant || k < n; k++)       /* first swap-out that differs */
    if(k >= nwant || k >= n || got[k] != want[k]){
      diff = k;
      break;
    }

  if(verbose || diff >= 0 || sim.nin != wantin || sim.error){
    say("swap-out order  expected:");
    for(k = 0; k < nwant; k++)
      say(" %u", want[k]);
    say("\n                yours   :");
    for(k = 0; k < n; k++)
      say(" %u", got[k]);
    say("\n");
    if(diff >= 0)
      say("%*s^ swap-out %d differs\n", 26 + 2 * diff, "", diff + 1);
    say("swap I/O        expected: %2d out, %2d in\n", nwant, wantin);
    say("                yours   : %2d out, %2d in%s\n", sim.nout, sim.nin,
        sim.nout == nwant && sim.nin == wantin? "" : "   <- different");
    if(sim.error)
      say("the trace stopped at this error:\n");
  }
  for(k = 0; k < n && k < nwant; k++)
    CHECK(got[k] == want[k], "swap-out %d took page %u; expected page %u",
          k + 1, got[k], want[k]);
  CHECK(sim.nout == nwant, "%d swap-outs; expected %d", sim.nout, nwant);
  CHECK(sim.nin == wantin, "%d swap-ins; expected %d", sim.nin, wantin);
  return 0;
}

/* one test per function: each part must pass */
static int t_swap_out(void) { return t_out_pte() || t_out_data() || t_out_fifo(); }
static int t_kalloc(void)   { return t_kalloc_full(); }
static int t_swap_in(void)  { return t_in_data() || t_in_queue(); }   /* a frame is free */
static int t_full(void)     { return t_trace() || t_in_full(); }

const char *title = "Pre-project quiz: swap (FIFO)";
const char *trace_id = "4";
const struct test tests[] = {
  { "1", "swap_out(): swap out the oldest page",       15, t_swap_out },
  { "2", "kalloc(): memory is full -> swap_out()",      5, t_kalloc   },
  { "3", "swap_in(): bring a swapped-out page back",   15, t_swap_in  },
  { "4", "full trace: 6 pages, 4 frames, 30 steps",    10, t_full     },
};
const int ntests = (int)(sizeof tests / sizeof tests[0]);

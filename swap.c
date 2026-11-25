// swap.c -- swap user pages out to disk when memory is full, and back in on a page fault.
//
// The swap area is the raw disk blocks right after the file system.
// One slot = 8 blocks = 4KB = one page.  Slot 0 is never used.
//
// A swapped-out page is recorded in its PTE (see mmu.h):
//   slot << 12 | flags, with PTE_P = 0   (never 0, because slot >= 1)
// so touching it causes a page fault, and trap() calls swap_in().
//
// Which page to swap out is decided by the use_list: every user page that
// comes into memory goes in at the front (newest), and swap_out() takes the
// back (oldest).  This is FIFO, as in the pre-project quiz.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define SWAPBASE   FSSIZE           // first block of the swap area
#define BLKPERPG   (PGSIZE / BSIZE) // 8 blocks per page
#define SWAPMINVA  0x100000         // pages below 1MB (code, stack) are never swapped
#define NFRAME     (PHYSTOP / PGSIZE)

pte_t* walkpgdir(pde_t *pgdir, const void *va, int alloc);   // vm.c

// ---------------------------------------------------------------- provided

// Slot states.  BUSY: being written; a reader waits until the write is done.
#define SLOT_FREE  0
#define SLOT_INUSE 1
#define SLOT_BUSY  2
#define SLOT_DYING 3    // freed while being written: free it when the write is done
#define SLOT_ZERO  4    // allocated slot whose page can be recreated without I/O

static struct spinlock swaplock;
static char swapmap[NSWAPSLOTS];

// Counters for swapstat().  Declared only: nobody increments them yet.
static uint nswapout, nswapin;

void
swapinit(void)
{
  initlock(&swaplock, "swap");
}

// Take a free swap slot (never 0).  Returns -1 if the swap area is full.
int
slotalloc(void)
{
  int i;

  acquire(&swaplock);
  for(i = 1; i < NSWAPSLOTS; i++){
    if(swapmap[i] == SLOT_FREE){
      swapmap[i] = SLOT_BUSY;
      release(&swaplock);
      return i;
    }
  }
  release(&swaplock);
  cprintf("slotalloc: swap area full\n");
  return -1;
}

// Give a slot back.
void
slotfree(int slot)
{
  if(slot <= 0 || slot >= NSWAPSLOTS)
    panic("slotfree");
  acquire(&swaplock);
  // Still being written: we cannot sleep here (exit holds ptable.lock),
  // so mark it and let the writer free it.
  if(swapmap[slot] == SLOT_BUSY)
    swapmap[slot] = SLOT_DYING;
  else
    swapmap[slot] = SLOT_FREE;
  release(&swaplock);
}

// Wait for the writer to publish the slot's contents.
static int
slotwait(int slot)
{
  int state;

  acquire(&swaplock);
  while(swapmap[slot] == SLOT_BUSY)
    sleep(&swapmap[slot], &swaplock);
  state = swapmap[slot];
  release(&swaplock);
  return state;
}

// The slot is ready: wake up anyone waiting to read it.
static void
slotdone(int slot, int state)
{
  acquire(&swaplock);
  swapmap[slot] = (swapmap[slot] == SLOT_DYING) ? SLOT_FREE : state;
  wakeup(&swapmap[slot]);
  release(&swaplock);
}

// Swapped frames are page-aligned and no longer mapped into user space.
static int
pageiszero(const char *v)
{
  const uint64 *words = (const uint64*)v;
  int i;

  for(i = 0; i < PGSIZE / sizeof(*words); i++)
    if(words[i] != 0)
      return 0;
  return 1;
}

// Copy the page at kernel address v to the slot (4KB).  May sleep.
void
swapwrite(char *v, int slot)
{
  int b;

  if(pageiszero(v)){
    slotdone(slot, SLOT_ZERO);
    return;
  }
  for(b = 0; b < BLKPERPG; b++)
    bwriteblock(ROOTDEV, SWAPBASE + slot*BLKPERPG + b, v + b*BSIZE);
  slotdone(slot, SLOT_INUSE);
}

// Copy the slot to the page at kernel address v (4KB).  May sleep.
void
swapread(char *v, int slot)
{
  struct buf *bp;
  int b;

  if(slotwait(slot) == SLOT_ZERO){
    memset(v, 0, PGSIZE);
    return;
  }
  for(b = 0; b < BLKPERPG; b++){
    bp = bread(ROOTDEV, SWAPBASE + slot*BLKPERPG + b);
    memmove(v + b*BSIZE, bp->data, BSIZE);
    brelse(bp);
  }
}

// fork: make a copy of a slot for the child.
// Returns the new slot, or -1 if the swap area is full.
int
swapcopyslot(int slot)
{
  struct buf *src;
  int nslot, b, state;

  state = slotwait(slot);
  if((nslot = slotalloc()) < 0)
    return -1;
  if(state == SLOT_ZERO){
    slotdone(nslot, SLOT_ZERO);
    return nslot;
  }
  for(b = 0; b < BLKPERPG; b++){
    src = bread(ROOTDEV, SWAPBASE + slot*BLKPERPG + b);
    bwriteblock(ROOTDEV, SWAPBASE + nslot*BLKPERPG + b, src->data);
    brelse(src);
  }
  slotdone(nslot, SLOT_INUSE);
  return nslot;
}

// Number of swap slots in use (for swaptest).
int
swapslots(void)
{
  int i, n = 0;

  acquire(&swaplock);
  for(i = 1; i < NSWAPSLOTS; i++)
    if(swapmap[i] != SLOT_FREE)
      n++;
  release(&swaplock);
  return n;
}

void
swapstat(int *nout, int *nin)
{
  int o, i;

  acquire(&swaplock);
  o = nswapout;
  i = nswapin;
  release(&swaplock);
  *nout = o;                            // user memory: may fault, so outside the lock
  *nin = i;
}

// The page array: one struct page per physical frame (Linux: mem_map).
// pages[i] tells which user page is in frame i.  pa2page(pa) finds it at once.
//
// The use_list: the user pages in memory, in the order they came in (FIFO),
// linked through the prev / next pointers in their struct page (Linux: LRU list).
// It is circular, with a head node:
//   use_list.next = the newest page,  use_list.prev = the oldest page,
//   use_list.next == &use_list        means the list is empty.
struct page {
  pde_t *pgdir;                 // page table of the process that uses this frame
  uint64 va;                    // the page's virtual address in that process
  struct page *prev;            // the newer page next to it (or the head node)
  struct page *next;            // the older page next to it (or the head node)
                                //   (0: not on the use_list)
};
struct page pages[NFRAME];
struct page use_list = { 0, 0, &use_list, &use_list };    // the head node

#define pa2page(pa) (&pages[(pa) >> PTXSHIFT])            // frame address -> its struct page
#define page2pa(p)  ((uint64)((p) - pages) << PTXSHIFT)     // struct page -> frame address

// ---------------------------------------------------------------- Part 1: use_list

// A user page came into memory: put it at the front of the use_list (newest).
// pgdir, va: the page.  Its PTE is already filled (call it after mapping the page).
void
list_add(pde_t *pgdir, uint64 va)
{
  struct page *p;

  if(va < SWAPMINVA)            // DO NOT REMOVE: code and stack pages are never swapped
    return;
  p = pa2page(PTE_ADDR(*walkpgdir(pgdir, (void*)va, 0)));   // DO NOT REMOVE: the struct page of its frame

  // TODO 1
  // (1) Record who uses this frame: p->pgdir, p->va.
  // (2) Link p right after the head node: its next is the old newest page
  //     (use_list.next), its prev is &use_list.  Then fix the old newest
  //     page's prev and use_list.next.
}

// The page in frame pa left memory (freed or swapped out): take it off the use_list.
void
list_remove(uint64 pa)
{
  struct page *p = pa2page(pa);

  if(pa >= PHYSTOP || p->next == 0)    // DO NOT REMOVE: not on the use_list
    return;

  // TODO 2
  // (1) Unlink p: connect the page before it and the page after it.
  // (2) Mark it as not on the list: p->prev = p->next = 0.
}

// Pick the page to swap out: the oldest (use_list.prev), and take it off the list.
// Returns its PTE, or 0 if the list is empty.
pte_t*
list_pick(void)
{
  struct page *p = use_list.prev;      // the oldest page

  // TODO 3
  // (1) The list is empty (p is the head node, &use_list): return 0.
  // (2) Take it off the list.                         Hint: list_remove(page2pa(p))
  // (3) Return its PTE (p->pgdir, p->va are still in p).
  //                                                   Hint: walkpgdir(p->pgdir, (void*)p->va, 0)
  return 0;
}

// ---------------------------------------------------------------- Part 2: swap out

// Swap one page out to disk and free its frame.
// Returns 0 on success, -1 if there is no page to swap out (or no free slot).
int
swap_out(void)
{
  pte_t *pte;
  int slot;
  char *v;

  pushcli();                    // DO NOT REMOVE: nothing else runs until the PTE is changed

  // TODO 5
  // (1) Pick the victim's PTE.                           Hint: list_pick()
  //     None -> popcli(), return -1.
  // (2) Get a free slot.                                 Hint: slotalloc()
  //     None -> popcli(), return -1.
  // (3) Find the victim's frame,                         Hint: v = P2V(PTE_ADDR(*pte))
  //     and change the PTE to "swapped out" (before writing to disk):
  //     slot << 12 | the old flags without PTE_P, PTE_A, PTE_D.

  if(myproc())                               // DO NOT REMOVE: make the CPU forget the
    lcr3(V2P(myproc()->pgdir));              //        old frame address (TLB)
  popcli();                                  // DO NOT REMOVE

  // TODO 5
  // (4) Write the frame to the slot.                     Hint: swapwrite()
  // (5) Free the frame, and return 0.                    Hint: kfree()
  return -1;
}

// ---------------------------------------------------------------- Part 3: swap in

// A page fault hit a swapped-out page: bring it back.
// pte is its PTE (slot << 12 | flags, PTE_P = 0), va its page address.
// trap() has already checked that the page is swapped out.
// Returns 0 on success, -1 if no frame can be found.
int
swap_in(pte_t *pte, uint64 va)
{
  uint flags;
  int slot;
  char *v;

  // TODO 7
  // (1) Save the slot number and the flags.              Hint: PTE_SLOT(), PTE_FLAGS()
  // (2) Get a frame with kalloc().  0 -> return -1.
  // (3) Read the slot into the frame.                    Hint: swapread()
  // (4) Update the PTE: physical address | flags | PTE_P. Hint: V2P()
  // (5) Free the slot, and put the page on the use_list.
  //                                                      Hint: slotfree(),
  //                                                      list_add(myproc()->pgdir, va)
  // (6) Return 0.
  return -1;
}

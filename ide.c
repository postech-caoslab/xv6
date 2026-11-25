// Legacy IDE driver with PCI bus-master DMA and a PIO fallback.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define SECTOR_SIZE   512
#define IDE_BSY       0x80
#define IDE_DRDY      0x40
#define IDE_DF        0x20
#define IDE_ERR       0x01

#define IDE_CMD_READ  0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_RDMUL 0xc4
#define IDE_CMD_WRMUL 0xc5
#define IDE_CMD_RDDMA 0xc8
#define IDE_CMD_WRDMA 0xca

#define PCI_CONFIG_ADDR 0xcf8
#define PCI_CONFIG_DATA 0xcfc
#define PCI_COMMAND     0x04
#define PCI_CLASS       0x08
#define PCI_BAR4        0x20
#define PCI_IO_MASTER   0x05

// Bus-master registers for the primary IDE channel.
#define BM_COMMAND 0
#define BM_STATUS  2
#define BM_PRDT    4
#define BM_START   0x01
#define BM_READ    0x08
#define BM_ACTIVE  0x01
#define BM_ERROR   0x02
#define BM_IRQ     0x04
#define PRD_END    0x80000000U

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;
static struct buf *idequeue;

static int havedisk1;
static ushort bmdma;
// The single IDE queue serializes access to the DMA buffer and descriptor.
// The aligned buffer cannot straddle a 64 KiB DMA boundary.
static uchar dmabuf[BSIZE] __attribute__((aligned(BSIZE)));
static struct {
  uint addr;
  uint count;
} prd __attribute__((aligned(8)));

static void idestart(struct buf*);

static uint
pciread(uint function, uint reg)
{
  outl(PCI_CONFIG_ADDR, function | reg);
  return inl(PCI_CONFIG_DATA);
}

static void
ideinitdma(void)
{
  uint function, class, bar, command;

  // Find the IDE function on the root PCI bus. The primary channel must
  // support bus mastering and use the legacy ports and IRQ.
  for(function = 0x80000000; function < 0x80010000; function += 0x100){
    class = pciread(function, PCI_CLASS);
    if((class & 0xffff8100) != 0x01018000)
      continue;
    bar = pciread(function, PCI_BAR4);
    if((bar & 1) == 0 || (bar & ~0xfU) == 0 || bar > 0xffff)
      continue;

    command = pciread(function, PCI_COMMAND);
    outw(PCI_CONFIG_DATA, command | PCI_IO_MASTER);
    bmdma = bar & ~0xfU;
    prd.addr = V2P(dmabuf);
    prd.count = PRD_END | BSIZE;
    outb(bmdma + BM_COMMAND, 0);
    outl(bmdma + BM_PRDT, V2P(&prd));
    return;
  }
}

// Wait for IDE disk to become ready.
static int
idewait(int checkerr)
{
  int r;

  while(((r = inb(0x1f7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
    ;
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
    return -1;
  return 0;
}

void
ideinit(void)
{
  int i;

  initlock(&idelock, "ide");
  ioapicenable(IRQ_IDE, ncpu - 1);
  idewait(0);

  // Check if disk 1 is present
  outb(0x1f6, 0xe0 | (1<<4));
  for(i=0; i<1000; i++){
    if(inb(0x1f7) != 0){
      havedisk1 = 1;
      break;
    }
  }

  // Switch back to disk 0.
  outb(0x1f6, 0xe0 | (0<<4));
  ideinitdma();
}

// Start the request for b.  Caller must hold idelock.
static void
idestart(struct buf *b)
{
  if(b == 0)
    panic("idestart");
  if(b->blockno >= FSSIZE)
    panic("incorrect blockno");
  int sector_per_block =  BSIZE/SECTOR_SIZE;
  int sector = b->blockno * sector_per_block;
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

  if (sector_per_block > 7) panic("idestart");

  idewait(0);
  outb(0x3f6, 0);  // generate interrupt
  outb(0x1f2, sector_per_block);  // number of sectors
  outb(0x1f3, sector & 0xff);
  outb(0x1f4, (sector >> 8) & 0xff);
  outb(0x1f5, (sector >> 16) & 0xff);
  outb(0x1f6, 0xe0 | ((b->dev&1)<<4) | ((sector>>24)&0x0f));
  if(bmdma){
    int direction = (b->flags & B_DIRTY) ? 0 : BM_READ;
    if(!direction)
      memmove(dmabuf, b->data, BSIZE);
    asm volatile("" ::: "memory");
    outb(bmdma + BM_COMMAND, direction);
    outb(bmdma + BM_STATUS, inb(bmdma + BM_STATUS) | BM_IRQ | BM_ERROR);
    outb(0x1f7, direction ? IDE_CMD_RDDMA : IDE_CMD_WRDMA);
    outb(bmdma + BM_COMMAND, direction | BM_START);
  } else if(b->flags & B_DIRTY){
    outb(0x1f7, write_cmd);
    outsl(0x1f0, b->data, BSIZE/4);
  } else {
    outb(0x1f7, read_cmd);
  }
}

// Interrupt handler.
void
ideintr(void)
{
  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);

  if((b = idequeue) == 0){
    release(&idelock);
    return;
  }

  if(bmdma){
    int status = inb(bmdma + BM_STATUS);
    if(!(status & BM_IRQ)){
      release(&idelock);
      return;
    }
    outb(bmdma + BM_COMMAND, 0);
    int diskstatus = inb(0x1f7);
    outb(bmdma + BM_STATUS, status | BM_IRQ | BM_ERROR);
    if((status & (BM_ERROR | BM_ACTIVE)) ||
       (diskstatus & (IDE_BSY | IDE_DF | IDE_ERR))){
      // Retry the same request using PIO before marking the buffer valid.
      cprintf("ide: DMA failed, falling back to PIO\n");
      bmdma = 0;
      idestart(b);
      release(&idelock);
      return;
    }
    asm volatile("" ::: "memory");
    if(!(b->flags & B_DIRTY))
      memmove(b->data, dmabuf, BSIZE);
  } else if(!(b->flags & B_DIRTY) && idewait(1) >= 0){
    insl(0x1f0, b->data, BSIZE/4);
  }
  idequeue = b->qnext;

  // Wake process waiting for this buf.
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);

  // Start disk on next buf in queue.
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{
  struct buf **pp;

  if(!holdingsleep(&b->lock))
    panic("iderw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)
    panic("iderw: ide disk 1 not present");

  acquire(&idelock);  //DOC:acquire-lock

  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;

  // Start disk if necessary.
  if(idequeue == b)
    idestart(b);

  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
  }


  release(&idelock);
}

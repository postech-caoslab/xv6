#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // In long mode, segmentation is mostly vestigial: bases and limits
  // are ignored for code/data segments.  We still need separate kernel
  // and user code descriptors because the DPL is checked, and a TSS
  // for the ring-0 stack pointer (set in switchuvm).
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG64(STA_X|STA_R, 0, 1);
  c->gdt[SEG_KDATA] = SEG64(STA_W, 0, 0);
  c->gdt[SEG_UCODE] = SEG64(STA_X|STA_R, DPL_USER, 1);
  c->gdt[SEG_UDATA] = SEG64(STA_W, DPL_USER, 0);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *table = pgdir;
  pde_t *entry;
  int level;

  for(level = 3; level > 0; level--){
    entry = &table[PX(level, va)];
    if(*entry & PTE_P){
      table = (pde_t*)P2V(PTE_ADDR(*entry));
    } else {
      if(!alloc || (table = (pde_t*)kalloc()) == 0)
        return 0;
      // Make sure all those PTE_P bits are zero.
      memset(table, 0, PGSIZE);
      // The permissions here are overly generous, but they can
      // be further restricted by the permissions in the page table
      // entries, if necessary.
      *entry = V2P(table) | PTE_P | PTE_W | PTE_U;
    }
  }
  return &table[PX(0, va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint64 size, uint64 pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uintp)va);
  last = (char*)PGROUNDDOWN(((uintp)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..USERTOP: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   DEVBASE..DEVBASE+32MB: mapped to DEVSPACE..4GB (devices such
//                                  as ioapic and lapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint64 phys_start;
  uint64 phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,      PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data),   0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,     PTE_W}, // kern data+memory
 { (void*)DEVBASE,  DEVSPACE,      0x100000000, PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVBASE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                k->phys_start, k->perm) < 0) {
      freevm(pgdir, 0);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  uint64 base;
  uint limit;
  uint64 *tss;

  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  // Build the 16-byte TSS descriptor in the two SEG_TSS gdt slots.
  base = (uint64)&mycpu()->ts;
  limit = sizeof(mycpu()->ts) - 1;
  tss = (uint64*)&mycpu()->gdt[SEG_TSS];
  tss[0] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) |
           ((uint64)STS_T64A << 40) | (1ULL << 47) |
           (((uint64)(limit >> 16) & 0xF) << 48) |
           (((base >> 24) & 0xFF) << 56);
  tss[1] = base >> 32;
  mycpu()->ts.rsp0 = (uint64)p->kstack + KSTACKSIZE;
  // Setting IOPL=0 in rflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;
  pte_t *pte;

  if((uintp) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
allocuvm(pde_t *pgdir, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz >= USERTOP)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
deallocuvm(pde_t *pgdir, uint64 oldsz, uint64 newsz)
{
  pte_t *pte;
  uint64 a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = (((a >> PDXSHIFT) + 1) << PDXSHIFT) - PGSIZE;  // skip to next 2MB region
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Recursively free all the page-table pages at and below 'table',
// which is at the given level (3 = PML4).  Does not free the
// physical pages the leaf PTEs refer to.
static void
freelevel(pde_t *table, int level)
{
  int i;

  if(level > 0){
    for(i = 0; i < NPDENTRIES; i++)
      if((table[i] & PTE_P) && !(table[i] & PTE_PS))
        freelevel((pde_t*)P2V(PTE_ADDR(table[i])), level - 1);
  }
  kfree((char*)table);
}

// Free a page table and all the physical memory pages
// in the user part (which is sz bytes, starting at va 0).
void
freevm(pde_t *pgdir, uint64 sz)
{
  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, sz, 0);
  freelevel(pgdir, 3);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~((uintp)PTE_U);
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint64 sz)
{
  pde_t *d;
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d, i);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint64 va, void *p, uint len)
{
  char *buf, *pa0;
  uint64 n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

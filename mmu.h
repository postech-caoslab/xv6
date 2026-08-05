// This file contains definitions for the
// x86-64 memory management unit (MMU).

// Rflags register
#define FL_IF           0x00000200      // Interrupt Enable

// Control Register flags
#define CR0_PE          0x00000001      // Protection Enable
#define CR0_WP          0x00010000      // Write Protect
#define CR0_PG          0x80000000      // Paging

#define CR4_PAE         0x00000020      // Physical Address Extension

// EFER (Extended Feature Enable Register) MSR
#define EFER_MSR        0xC0000080
#define EFER_LME        (1<<8)          // Long Mode Enable

// various segment selectors.
#define SEG_KCODE 1  // kernel code
#define SEG_KDATA 2  // kernel data+stack
#define SEG_UCODE 3  // user code
#define SEG_UDATA 4  // user data+stack
#define SEG_TSS   5  // this process's task state (takes 2 slots)

// cpu->gdt[NSEGS] holds the above segments (TSS descriptor is 16 bytes).
#define NSEGS     7

#ifndef __ASSEMBLER__
// Segment Descriptor
struct segdesc {
  uint lim_15_0 : 16;  // Low bits of segment limit
  uint base_15_0 : 16; // Low bits of segment base address
  uint base_23_16 : 8; // Middle bits of segment base address
  uint type : 4;       // Segment type (see STS_ constants)
  uint s : 1;          // 0 = system, 1 = application
  uint dpl : 2;        // Descriptor Privilege Level
  uint p : 1;          // Present
  uint lim_19_16 : 4;  // High bits of segment limit
  uint avl : 1;        // Unused (available for software use)
  uint l : 1;          // 64-bit code segment (long mode)
  uint db : 1;         // 0 = 16-bit segment, 1 = 32-bit segment
  uint g : 1;          // Granularity: limit scaled by 4K when set
  uint base_31_24 : 8; // High bits of segment base address
};

// Normal 64-bit code/data segment.  Base and limit are ignored in
// long mode (except for fs/gs), but set them anyway.
#define SEG64(type, dpl, iscode) (struct segdesc)             \
{ 0xffff, 0, 0, type, 1, dpl, 1,                              \
  0xf, 0, (iscode) ? 1 : 0, (iscode) ? 0 : 1, 1, 0 }
#endif

#define DPL_USER    0x3     // User DPL

// Application segment type bits
#define STA_X       0x8     // Executable segment
#define STA_W       0x2     // Writeable (non-executable segments)
#define STA_R       0x2     // Readable (executable segments)

// System segment type bits
#define STS_T64A    0x9     // Available 64-bit TSS
#define STS_IG64    0xE     // 64-bit Interrupt Gate
#define STS_TG64    0xF     // 64-bit Trap Gate

// A linear address 'la' has a five-part structure as follows:
//
// +--9--+--9--+--9--+--9--+---------12----------+
// |PML4 |PDPT | PD  | PT  | Offset within Page  |
// +-----+-----+-----+-----+---------------------+
//
// PX(level, va) gives the index at the given level (3 = PML4 .. 0 = PT).

#define PXSHIFT(level)  (12+(9*(level)))
#define PX(level, va)   ((((uintp)(va)) >> PXSHIFT(level)) & 0x1FF)

// Page directory and page table constants.
#define NPDENTRIES      512     // # entries per page-table page
#define PGSIZE          4096    // bytes mapped by a page

#define PTXSHIFT        12      // offset of PTX in a linear address
#define PDXSHIFT        21      // offset of PDX in a linear address

#define PGROUNDUP(sz)  ((((uintp)(sz))+PGSIZE-1) & ~((uintp)(PGSIZE-1)))
#define PGROUNDDOWN(a) (((uintp)(a)) & ~((uintp)(PGSIZE-1)))

// Page table/directory entry flags.
#define PTE_P           0x001   // Present
#define PTE_W           0x002   // Writeable
#define PTE_U           0x004   // User
#define PTE_PS          0x080   // Page Size (2MB pages in a PD entry)

// Address in page table or page directory entry
#define PTE_ADDR(pte)   ((uintp)(pte) & 0xFFFFFFFFFF000)
#define PTE_FLAGS(pte)  ((uintp)(pte) &  0xFFF)

#ifndef __ASSEMBLER__
typedef uint64 pte_t;

// Task state segment format (64-bit)
struct taskstate {
  uint reserved0;
  uint64 rsp0;       // Stack pointer for ring 0 (loaded on traps from user)
  uint64 rsp1;
  uint64 rsp2;
  uint64 reserved1;
  uint64 ist[7];
  uint64 reserved2;
  ushort reserved3;
  ushort iomb;       // I/O map base address
} __attribute__((packed));

// Gate descriptors for interrupts and traps (16 bytes in long mode)
struct gatedesc {
  uint off_15_0 : 16;   // low 16 bits of offset in segment
  uint cs : 16;         // code segment selector
  uint ist : 3;         // interrupt stack table index (0 = none)
  uint rsv1 : 5;        // reserved
  uint type : 4;        // type(STS_{IG64,TG64})
  uint s : 1;           // must be 0 (system)
  uint dpl : 2;         // descriptor(meaning new) privilege level
  uint p : 1;           // Present
  uint off_31_16 : 16;  // middle bits of offset in segment
  uint off_63_32;       // high bits of offset
  uint rsv2;            // reserved
};

// Set up a normal interrupt/trap gate descriptor.
// - istrap: 1 for a trap (= exception) gate, 0 for an interrupt gate.
//   interrupt gate clears FL_IF, trap gate leaves FL_IF alone
// - sel: Code segment selector for interrupt/trap handler
// - off: Offset in code segment for interrupt/trap handler
// - dpl: Descriptor Privilege Level -
//        the privilege level required for software to invoke
//        this interrupt/trap gate explicitly using an int instruction.
#define SETGATE(gate, istrap, sel, off, d)                \
{                                                         \
  (gate).off_15_0 = (uintp)(off) & 0xffff;                \
  (gate).cs = (sel);                                      \
  (gate).ist = 0;                                         \
  (gate).rsv1 = 0;                                        \
  (gate).type = (istrap) ? STS_TG64 : STS_IG64;           \
  (gate).s = 0;                                           \
  (gate).dpl = (d);                                       \
  (gate).p = 1;                                           \
  (gate).off_31_16 = ((uintp)(off) >> 16) & 0xffff;       \
  (gate).off_63_32 = (uintp)(off) >> 32;                  \
  (gate).rsv2 = 0;                                        \
}

#endif

// Memory layout

#define EXTMEM  0x100000            // Start of extended memory
#define PHYSTOP 0xE000000           // Top physical memory
#define DEVSPACE 0xFE000000         // Other devices are at high physical addresses

// Key addresses for address space layout (see kmap in vm.c for layout)
#define KERNBASE 0xFFFFFFFF80000000 // First kernel virtual address
#define DEVBASE  0xFFFFFFFFFE000000 // Virtual address of DEVSPACE mapping
#define KERNLINK (KERNBASE+EXTMEM)  // Address where kernel is linked

// Top of the user half of the address space (base of the canonical hole)
#define USERTOP  0x0000800000000000

#ifndef __ASSEMBLER__

#define V2P(a) (((uintp) (a)) - KERNBASE)
#define P2V(a) ((void *)(((uintp) (a)) + KERNBASE))
#define IO2V(a) ((void *)(((uintp) (a)) - DEVSPACE + DEVBASE))

#endif

#define V2P_WO(x) ((x) - KERNBASE)    // same as V2P, but without casts
#define P2V_WO(x) ((x) + KERNBASE)    // same as P2V, but without casts

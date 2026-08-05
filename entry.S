# The xv6 kernel starts executing in this file. This file is linked with
# the kernel C code, so it can refer to kernel symbols such as main().
# The boot block (bootasm.S and bootmain.c) jumps to entry below, in
# 32-bit protected mode with paging off.  This code builds the boot
# page tables, enters long mode, and jumps to main() running at high
# (KERNBASE) virtual addresses.

#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"

# By convention, the _start symbol specifies the ELF entry point.
# Since we haven't set up virtual memory yet, our entry point is
# the physical address of 'entry'.
.code32
.globl _start
_start = V2P_WO(entry)

# Entering xv6 on boot processor, with paging off.
.globl entry
entry:
  # Build the boot page tables (in .bss, zeroed by the boot loader):
  #   bootpml4[0] = bootpml4[511] = bootpdpt | P | W
  #   bootpdpt[0] = bootpdpt[510] = bootpd   | P | W
  #   bootpd[i] = i*2MB | P | W | PS          (512 entries: 1 GB)
  # This maps VA [0,1GB) and VA [KERNBASE,KERNBASE+1GB) to PA [0,1GB).
  movl    $(V2P_WO(bootpdpt) + (PTE_P|PTE_W)), %eax
  movl    %eax, V2P_WO(bootpml4)
  movl    %eax, V2P_WO(bootpml4) + 511*8

  movl    $(V2P_WO(bootpd) + (PTE_P|PTE_W)), %eax
  movl    %eax, V2P_WO(bootpdpt)
  movl    %eax, V2P_WO(bootpdpt) + 510*8

  movl    $V2P_WO(bootpd), %edi
  movl    $(PTE_P|PTE_W|PTE_PS), %eax
  movl    $512, %ecx
1:
  movl    %eax, (%edi)
  addl    $(1 << 21), %eax
  addl    $8, %edi
  decl    %ecx
  jnz     1b

  # Turn on physical address extension (required for long mode).
  movl    %cr4, %eax
  orl     $(CR4_PAE), %eax
  movl    %eax, %cr4
  # Set page-map level-4 table
  movl    $(V2P_WO(bootpml4)), %eax
  movl    %eax, %cr3
  # Enable long mode (EFER.LME).
  movl    $(EFER_MSR), %ecx
  rdmsr
  orl     $(EFER_LME), %eax
  wrmsr
  # Turn on paging: this activates long mode (compatibility mode).
  movl    %cr0, %eax
  orl     $(CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # Switch to a 64-bit code segment.
  lgdt    V2P_WO(gdtdesc64)
  ljmp    $(SEG_KCODE<<3), $V2P_WO(entry64low)

.code64
entry64low:
  movabs  $entry64high, %rax
  jmp     *%rax

entry64high:
  # Load data segment selectors (SS must be valid for interrupts).
  movw    $(SEG_KDATA<<3), %ax
  movw    %ax, %ss
  movw    %ax, %ds
  movw    %ax, %es
  xorw    %ax, %ax
  movw    %ax, %fs
  movw    %ax, %gs

  # Set up the stack pointer and jump to main().
  movabs  $(stack + KSTACKSIZE), %rax
  movq    %rax, %rsp
  xorq    %rbp, %rbp
  movabs  $main, %rax
  call    *%rax
spin:
  jmp     spin

# Bootstrap GDT: null, 64-bit kernel code, kernel data.
# Indexes must match SEG_KCODE and SEG_KDATA.
.p2align 3
gdt64:
  .quad 0
  .quad 0x00209A0000000000  # 64-bit code: P=1, L=1, type=0xA (X|R)
  .quad 0x0000920000000000  # data: P=1, type=0x2 (W)
gdtdesc64:
  .word (gdtdesc64 - gdt64 - 1)
  .long V2P_WO(gdt64)       # loaded from 32-bit mode: 32-bit base

.comm stack, KSTACKSIZE, 16

# Boot page tables, used by this file and entryother.S for APs.
.section .bss
.p2align 12
.globl bootpml4
bootpml4:
  .space 4096
bootpdpt:
  .space 4096
bootpd:
  .space 4096

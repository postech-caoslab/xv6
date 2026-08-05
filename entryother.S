#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# Each non-boot CPU ("AP") is started up in response to a STARTUP
# IPI from the boot CPU.  Section B.4.2 of the Multi-Processor
# Specification says that the AP will start in real mode with CS:IP
# set to XY00:0000, where XY is an 8-bit value sent with the
# STARTUP. Thus this code must start at a 4096-byte boundary.
#
# Because this code sets DS to zero, it must sit
# at an address in the low 2^16 bytes.
#
# Startothers (in main.c) sends the STARTUPs one at a time.
# It copies this code (start) at 0x7000.  It puts the address of
# a newly allocated per-core stack in start-8, the address of the
# place to jump to (mpenter) in start-16, and the physical address
# of bootpml4 in start-24.
#
# This code combines elements of bootasm.S and entry.S.

.code16
.globl start
start:
  cli

  # Zero data segment registers DS, ES, and SS.
  xorw    %ax,%ax
  movw    %ax,%ds
  movw    %ax,%es
  movw    %ax,%ss

  # Switch from real to protected mode.  Use a bootstrap GDT that makes
  # virtual addresses map directly to physical addresses so that the
  # effective memory map doesn't change during the transition.
  lgdt    gdtdesc
  movl    %cr0, %eax
  orl     $CR0_PE, %eax
  movl    %eax, %cr0

  # Complete the transition to 32-bit protected mode by using a long jmp
  # to reload %cs and %eip.  The segment descriptors are set up with no
  # translation, so that the mapping is still the identity mapping.
  ljmpl    $(3<<3), $(start32)

//PAGEBREAK!
.code32  # Tell assembler to generate 32-bit code now.
start32:
  # Set up the protected-mode data segment registers
  movw    $(2<<3), %ax            # Our data segment selector
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %ss                # -> SS: Stack Segment
  movw    $0, %ax                 # Zero segments not ready for use
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # Turn on physical address extension (required for long mode).
  movl    %cr4, %eax
  orl     $(CR4_PAE), %eax
  movl    %eax, %cr4
  # Use the boot page tables built by the BSP (entry.S).
  movl    (start-24), %eax
  movl    %eax, %cr3
  # Enable long mode (EFER.LME).
  movl    $(EFER_MSR), %ecx
  rdmsr
  orl     $(EFER_LME), %eax
  wrmsr
  # Turn on paging: this activates long mode.
  movl    %cr0, %eax
  orl     $(CR0_PE|CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # Switch to a 64-bit code segment.  The selector must equal
  # SEG_KCODE<<3 so that %cs remains valid after seginit() installs
  # the per-CPU GDT (interrupt frames store %cs, and iretq reloads it).
  ljmp    $(1<<3), $(start64)

.code64
start64:
  # SS/DS/ES: null selectors are fine in 64-bit ring 0.
  xorw    %ax, %ax
  movw    %ax, %ss
  movw    %ax, %ds
  movw    %ax, %es

  # Switch to the stack allocated by startothers()
  movq    (start-8), %rsp
  xorq    %rbp, %rbp
  # Call mpenter()
  movq    (start-16), %rax
  call    *%rax

  movw    $0x8a00, %ax
  movw    %ax, %dx
  outw    %ax, %dx
  movw    $0x8ae0, %ax
  outw    %ax, %dx
spin:
  jmp     spin

# Bootstrap GDT: null, 64-bit code, data, 32-bit code.
# The 64-bit code and data selectors match SEG_KCODE and SEG_KDATA.
.p2align 3
gdt:
  SEG_NULLASM
  .quad 0x00209A0000000000              # 64-bit code (sel 0x08)
  SEG_ASM(STA_W, 0, 0xffffffff)         # data        (sel 0x10)
  SEG_ASM(STA_X|STA_R, 0, 0xffffffff)   # 32-bit code (sel 0x18)

gdtdesc:
  .word   (gdtdesc - gdt - 1)
  .long   gdt

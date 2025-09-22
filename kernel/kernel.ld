OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    /*
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    */
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);
  
  /* —— 供物理内存管理使用的三个符号 —— */
  /* KERNEL_DATA：内核占用区域末尾（页对齐） */
  KERNEL_DATA = .;

  /* ALLOC_BEGIN：可分配区域起点（等于 KERNEL_DATA；如需保留页，可在此再 +0x1000） */
  ALLOC_BEGIN = KERNEL_DATA;

  /* ALLOC_END：可分配区域终点（开区间）。本实验固定 128MB 物理内存 */
  ALLOC_END = 0x80000000 + 128M;
}

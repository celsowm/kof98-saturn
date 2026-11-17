/* Merge all .text.* into one .text at VMA 0; keep data out. */
SECTIONS
{
  .text 0x0 : { *(.text .text.*) }
  .data 0x0 : { *(.data .data.*) }
  .bss  0x0 : { *(.bss .bss.*) }
  /DISCARD/ : { *(.rela.* .rel.* .comment .note.* .shstrtab .symtab .strtab) }
}

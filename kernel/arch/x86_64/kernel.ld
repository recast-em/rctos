/* kernel.ld - Kernel im oberen Adressraum; je Segment eigene Rechte (W^X). */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    requests PT_LOAD FLAGS(6);   /* RW: Anfragen an den Bootloader */
    text     PT_LOAD FLAGS(5);   /* RX */
    rodata   PT_LOAD FLAGS(4);   /* R  */
    data     PT_LOAD FLAGS(6);   /* RW */
}

SECTIONS
{
    . = 0xffffffff80000000;
    rc_image_start = .;

    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests

    . = ALIGN(4096);
    rc_text_start = .;
    .text : { *(.text .text.*) } :text
    rc_text_end = .;

    . = ALIGN(4096);
    rc_rodata_start = .;
    .rodata : { *(.rodata .rodata.*) } :rodata
    rc_rodata_end = .;

    . = ALIGN(4096);
    rc_data_start = .;
    .data : { *(.data .data.*) } :data
    rc_data_end = .;
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    rc_bss_end = .;

    /DISCARD/ : {
        *(.eh_frame*) *(.note .note.*) *(.comment) *(.interp) *(.dynamic) *(.dynsym) *(.dynstr)
    }
}

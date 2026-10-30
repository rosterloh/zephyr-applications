/* The word ahead of the note keeps the output section PROGBITS: a section
 * that starts with the note becomes SHT_NOTE, and esptool's elf2image drops
 * those from the flash image. */
. = ALIGN(4);
LONG(0)
app_build_id_note = .;
KEEP(*(.note.gnu.build-id))

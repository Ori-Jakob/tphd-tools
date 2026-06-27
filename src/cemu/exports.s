.extern TPHDToolsEntry

.section .fexports, "ax", @0x80000001
.align 4

.long 1
.long 0x25015cb7

.long TPHDToolsEntry
.long 0x10

.string "TPHDToolsEntry"


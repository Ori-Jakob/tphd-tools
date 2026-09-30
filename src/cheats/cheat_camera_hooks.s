.section .text, "ax", @progbits
.align 2

.extern tphdCameraFilterFixedType

.global tphdCameraNextTypeHook
.type tphdCameraNextTypeHook, @function
tphdCameraNextTypeHook:
    stwu 1, -0x20(1)
    stw 0, 8(1)
    mflr 0
    stw 0, 12(1)
    or 4, 31, 31
    bl tphdCameraFilterFixedType
    or 5, 3, 3
    lwz 0, 12(1)
    mtlr 0
    lwz 0, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdCameraNextTypeHook, .-tphdCameraNextTypeHook

[TPHDv81]
moduleMatches = 0x1A03E108

0x02BEF83C = _cCt_Counter_rest:

.origin = codecave

_pack_block:
_pk_version:
.int 1

_rpl_state:
.byte 0
.align 4

_rpl_module:
.int 0

_rpl_fn:
.int 0

_rpl_name:
.string "tphd_tools"
.align 4

_rpl_export:
.string "rpl_cemu_entry"
.align 4

_rpl_missing:
.string "[tphd_tools] OSDynLoad_Acquire(tphd_tools) failed, is tphd_tools.rpl in the pack code folder?\n"
.align 4

_rpl_noexport:
.string "[tphd_tools] tphd_tools.rpl loaded but has no rpl_cemu_entry\n"
.align 4

_rpl_ok:
.string "[tphd_tools] rpl_cemu_entry resolved\n"
.align 4

_rpl_alloc_msg:
.string "[tphd_tools] loader alloc %u bytes align %u -> %08x\n"
.align 4

_rpl_game_alloc:
.int 0
_rpl_game_free:
.int 0

_rpl_alloc:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x08(r1)
    stw   r4, 0x0C(r1)
    stw   r5, 0x10(r1)
    li    r3, 1
    bl    import.coreinit.MEMGetBaseHeapHandle
    cmpwi r3, 0
    beq   _rpl_alloc_fail
    lwz   r4, 0x08(r1)
    lwz   r5, 0x0C(r1)
    bl    import.coreinit.MEMAllocFromExpHeapEx
    stw   r3, 0x14(r1)
    lwz   r5, 0x10(r1)
    stw   r3, 0(r5)
    mr    r6, r3
    lwz   r4, 0x08(r1)
    lwz   r5, 0x0C(r1)
    lis   r3, _rpl_alloc_msg@ha
    addi  r3, r3, _rpl_alloc_msg@l
    bl    import.coreinit.OSReport
    lwz   r3, 0x14(r1)
    cmpwi r3, 0
    beq   _rpl_alloc_fail
    li    r3, 0
    b     _rpl_alloc_done
_rpl_alloc_fail:
    lwz   r5, 0x10(r1)
    li    r0, 0
    stw   r0, 0(r5)
    lis   r3, 0xFFFF
    ori   r3, r3, 0xFFFF
_rpl_alloc_done:
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

_rpl_free:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x08(r1)
    li    r3, 1
    bl    import.coreinit.MEMGetBaseHeapHandle
    cmpwi r3, 0
    beq   _rpl_free_done
    lwz   r4, 0x08(r1)
    bl    import.coreinit.MEMFreeToExpHeap
_rpl_free_done:
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

_rpl_resolve:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)

    lis   r3, _rpl_state@ha
    lbz   r4, _rpl_state@l(r3)
    cmpwi r4, 0
    bne   _rpl_resolve_done

    li    r4, 1
    stb   r4, _rpl_state@l(r3)

    lis   r3, _rpl_game_alloc@ha
    addi  r3, r3, _rpl_game_alloc@l
    lis   r4, _rpl_game_free@ha
    addi  r4, r4, _rpl_game_free@l
    bl    import.coreinit.OSDynLoad_GetAllocator

    lis   r3, _rpl_alloc@ha
    addi  r3, r3, _rpl_alloc@l
    lis   r4, _rpl_free@ha
    addi  r4, r4, _rpl_free@l
    bl    import.coreinit.OSDynLoad_SetAllocator

    lis   r3, _rpl_name@ha
    addi  r3, r3, _rpl_name@l
    lis   r4, _rpl_module@ha
    addi  r4, r4, _rpl_module@l
    bl    import.coreinit.OSDynLoad_Acquire

    lis   r3, _rpl_game_alloc@ha
    lwz   r3, _rpl_game_alloc@l(r3)
    lis   r4, _rpl_game_free@ha
    lwz   r4, _rpl_game_free@l(r4)
    bl    import.coreinit.OSDynLoad_SetAllocator

    lis   r3, _rpl_module@ha
    lwz   r3, _rpl_module@l(r3)
    cmpwi r3, 0
    bne   _rpl_have_module

    lis   r3, _rpl_missing@ha
    addi  r3, r3, _rpl_missing@l
    bl    import.coreinit.OSReport
    b     _rpl_resolve_done

_rpl_have_module:
    li    r4, 0
    lis   r5, _rpl_export@ha
    addi  r5, r5, _rpl_export@l
    lis   r6, _rpl_fn@ha
    addi  r6, r6, _rpl_fn@l
    bl    import.coreinit.OSDynLoad_FindExport

    lis   r3, _rpl_fn@ha
    lwz   r3, _rpl_fn@l(r3)
    cmpwi r3, 0
    bne   _rpl_report_ok

    lis   r3, _rpl_noexport@ha
    addi  r3, r3, _rpl_noexport@l
    bl    import.coreinit.OSReport
    b     _rpl_resolve_done

_rpl_report_ok:
    lis   r3, _rpl_ok@ha
    addi  r3, r3, _rpl_ok@l
    bl    import.coreinit.OSReport

_rpl_resolve_done:
    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

counter_hook:
    stwu  r1, -0x40(r1)
    mflr  r0
    stw   r0, 0x34(r1)
    stw   r3, 0x10(r1)

    bl    _rpl_resolve
    cmpwi r9, 0
    beq   counter_done

    mtctr r9
    li    r3, 0
    lis   r4, _pack_block@ha
    addi  r4, r4, _pack_block@l
    li    r5, 0
    li    r6, 0
    bctrl

counter_done:
    lwz   r3, 0x10(r1)
    lwz   r0, 0x34(r1)
    mtlr  r0
    addi  r1, r1, 0x40
    lis   r10, 0x1029
    b     _cCt_Counter_rest

copy_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x10(r1)
    stw   r4, 0x0c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   copy_real

    mtctr r9
    li    r3, 1
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    li    r6, 0
    bctrl
    cmpwi r3, 0
    bne   copy_done

copy_real:
    lwz   r3, 0x10(r1)
    lwz   r4, 0x0c(r1)
    bl    import.gx2.GX2CopyColorBufferToScanBuffer

copy_done:
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

context_hook:
    stwu  r1, -0x20(r1)
    mflr  r0
    stw   r0, 0x14(r1)
    stw   r3, 0x10(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   context_real

    mtctr r9
    li    r3, 3
    lwz   r4, 0x10(r1)
    li    r5, 0
    li    r6, 0
    bctrl

context_real:
    lwz   r3, 0x10(r1)
    bl    import.gx2.GX2SetContextState

    lwz   r0, 0x14(r1)
    mtlr  r0
    addi  r1, r1, 0x20
    blr

vpad_read_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r4, 0x10(r1)

    bl    import.vpad.VPADRead
    stw   r3, 0x0c(r1)

    cmpwi r3, 0
    ble   vpad_done

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   vpad_done

    mtctr r9
    li    r3, 2
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    li    r6, 0
    bctrl

vpad_done:
    lwz   r3, 0x0c(r1)
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

kpad_read_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x14(r1)
    stw   r4, 0x10(r1)

    bl    import.padscore.KPADReadEx
    stw   r3, 0x0c(r1)

    cmpwi r3, 0
    ble   kpad_done

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   kpad_done

    mtctr r9
    li    r3, 4
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    lwz   r6, 0x14(r1)
    bctrl

kpad_done:
    lwz   r3, 0x0c(r1)
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

0x02BEF838 = b   counter_hook

0x02BDE730 = bla copy_hook
0x02BDE758 = bla copy_hook
0x02BDE800 = bla copy_hook
0x02BDE82C = bla copy_hook

0x02AF9130 = bla context_hook
0x02AF9158 = bla context_hook
0x02BD9434 = bla context_hook
0x02BDE138 = bla context_hook
0x02BDE410 = bla context_hook
0x02BDE4D8 = bla context_hook
0x02BDE5A4 = bla context_hook
0x02BDE5B0 = bla context_hook
0x02BDE704 = b   context_hook
0x02BDEB28 = bla context_hook
0x02BDFCD4 = bla context_hook
0x02BDFEC0 = bla context_hook
0x02BE43C0 = bla context_hook
0x02BE4658 = bla context_hook
0x02BE4894 = bla context_hook

0x02BFAE38 = bla vpad_read_hook
0x02BFB94C = bla kpad_read_hook
0x02BFCEAC = bla kpad_read_hook

[TPHDv81]
moduleMatches = 0x1A03E108

0x02BEF83C = _cCt_Counter_rest:
0x0201B204 = _g_0201B204:
0x0201B318 = _g_0201B318:
0x02022BD8 = _g_02022BD8:
0x0203AC18 = _g_0203AC18:
0x0203AC28 = _g_0203AC28:
0x0203AC44 = _g_0203AC44:
0x020502F4 = _g_020502F4:
0x0208BE14 = _g_0208BE14:
0x021A37B8 = _g_021A37B8:
0x021A37EC = _g_021A37EC:
0x021A3830 = _g_021A3830:
0x024DC588 = _g_024DC588:
0x02AA6460 = _g_02AA6460:

.origin = codecave

_pack_block:
_pk_version:
.int 2

_pk_hook_count:
.int 42
_pkh_0:
.int _hkf_0
_pkh_1:
.int _hkf_1
_pkh_2:
.int _hkf_2
_pkh_3:
.int _hkf_3
_pkh_4:
.int _hkf_4
_pkh_5:
.int _hkf_5
_pkh_6:
.int _hkf_6
_pkh_7:
.int _hkf_7
_pkh_8:
.int _hkf_8
_pkh_9:
.int _hkf_9
_pkh_10:
.int _hkf_10
_pkh_11:
.int _hkf_11
_pkh_12:
.int _hkf_12
_pkh_13:
.int _hkf_13
_pkh_14:
.int _hkf_14
_pkh_15:
.int _hkf_15
_pkh_16:
.int _hkf_16
_pkh_17:
.int _hkf_17
_pkh_18:
.int _hkf_18
_pkh_19:
.int _hkf_19
_pkh_20:
.int _hkf_20
_pkh_21:
.int _hkf_21
_pkh_22:
.int _hkf_22
_pkh_23:
.int _hkf_23
_pkh_24:
.int _hkf_24
_pkh_25:
.int _hkf_25
_pkh_26:
.int _hkf_26
_pkh_27:
.int _hkf_27
_pkh_28:
.int _hkf_28
_pkh_29:
.int _hkf_29
_pkh_30:
.int _hkf_30
_pkh_31:
.int _hkf_31
_pkh_32:
.int _hkf_32
_pkh_33:
.int _hkf_33
_pkh_34:
.int _hkf_34
_pkh_35:
.int _hkf_35
_pkh_36:
.int _hkf_36
_pkh_37:
.int _hkf_37
_pkh_38:
.int _hkf_38
_pkh_39:
.int _hkf_39
_pkh_40:
.int _hkf_40
_pkh_41:
.int _hkf_41

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

_hkt_0:
    lis   r11, _pkh_0@ha
    lwz   r11, _pkh_0@l(r11)
    mtctr r11
    bctr

_hkt_1:
    lis   r11, _pkh_1@ha
    lwz   r11, _pkh_1@l(r11)
    mtctr r11
    bctr

_hkt_2:
    lis   r11, _pkh_2@ha
    lwz   r11, _pkh_2@l(r11)
    mtctr r11
    bctr

_hkt_3:
    lis   r11, _pkh_3@ha
    lwz   r11, _pkh_3@l(r11)
    mtctr r11
    bctr

_hkt_4:
    lis   r11, _pkh_4@ha
    lwz   r11, _pkh_4@l(r11)
    mtctr r11
    bctr

_hkt_5:
    lis   r11, _pkh_5@ha
    lwz   r11, _pkh_5@l(r11)
    mtctr r11
    bctr

_hkt_6:
    lis   r11, _pkh_6@ha
    lwz   r11, _pkh_6@l(r11)
    mtctr r11
    bctr

_hkt_7:
    lis   r11, _pkh_7@ha
    lwz   r11, _pkh_7@l(r11)
    mtctr r11
    bctr

_hkt_8:
    lis   r11, _pkh_8@ha
    lwz   r11, _pkh_8@l(r11)
    mtctr r11
    bctr

_hkt_9:
    lis   r11, _pkh_9@ha
    lwz   r11, _pkh_9@l(r11)
    mtctr r11
    bctr

_hkt_10:
    lis   r11, _pkh_10@ha
    lwz   r11, _pkh_10@l(r11)
    mtctr r11
    bctr

_hkt_11:
    lis   r11, _pkh_11@ha
    lwz   r11, _pkh_11@l(r11)
    mtctr r11
    bctr

_hkt_12:
    lis   r11, _pkh_12@ha
    lwz   r11, _pkh_12@l(r11)
    mtctr r11
    bctr

_hkt_13:
    lis   r11, _pkh_13@ha
    lwz   r11, _pkh_13@l(r11)
    mtctr r11
    bctr

_hkt_14:
    lis   r11, _pkh_14@ha
    lwz   r11, _pkh_14@l(r11)
    mtctr r11
    bctr

_hkt_15:
    lis   r11, _pkh_15@ha
    lwz   r11, _pkh_15@l(r11)
    mtctr r11
    bctr

_hkt_16:
    lis   r11, _pkh_16@ha
    lwz   r11, _pkh_16@l(r11)
    mtctr r11
    bctr

_hkt_17:
    lis   r11, _pkh_17@ha
    lwz   r11, _pkh_17@l(r11)
    mtctr r11
    bctr

_hkt_18:
    lis   r11, _pkh_18@ha
    lwz   r11, _pkh_18@l(r11)
    mtctr r11
    bctr

_hkt_19:
    lis   r11, _pkh_19@ha
    lwz   r11, _pkh_19@l(r11)
    mtctr r11
    bctr

_hkt_20:
    lis   r11, _pkh_20@ha
    lwz   r11, _pkh_20@l(r11)
    mtctr r11
    bctr

_hkt_21:
    lis   r11, _pkh_21@ha
    lwz   r11, _pkh_21@l(r11)
    mtctr r11
    bctr

_hkt_22:
    lis   r11, _pkh_22@ha
    lwz   r11, _pkh_22@l(r11)
    mtctr r11
    bctr

_hkt_23:
    lis   r11, _pkh_23@ha
    lwz   r11, _pkh_23@l(r11)
    mtctr r11
    bctr

_hkt_24:
    lis   r11, _pkh_24@ha
    lwz   r11, _pkh_24@l(r11)
    mtctr r11
    bctr

_hkt_25:
    lis   r11, _pkh_25@ha
    lwz   r11, _pkh_25@l(r11)
    mtctr r11
    bctr

_hkt_26:
    lis   r11, _pkh_26@ha
    lwz   r11, _pkh_26@l(r11)
    mtctr r11
    bctr

_hkt_27:
    lis   r11, _pkh_27@ha
    lwz   r11, _pkh_27@l(r11)
    mtctr r11
    bctr

_hkt_28:
    lis   r11, _pkh_28@ha
    lwz   r11, _pkh_28@l(r11)
    mtctr r11
    bctr

_hkt_29:
    lis   r11, _pkh_29@ha
    lwz   r11, _pkh_29@l(r11)
    mtctr r11
    bctr

_hkt_30:
    lis   r11, _pkh_30@ha
    lwz   r11, _pkh_30@l(r11)
    mtctr r11
    bctr

_hkt_31:
    lis   r11, _pkh_31@ha
    lwz   r11, _pkh_31@l(r11)
    mtctr r11
    bctr

_hkt_32:
    lis   r11, _pkh_32@ha
    lwz   r11, _pkh_32@l(r11)
    mtctr r11
    bctr

_hkt_33:
    lis   r11, _pkh_33@ha
    lwz   r11, _pkh_33@l(r11)
    mtctr r11
    bctr

_hkt_34:
    lis   r11, _pkh_34@ha
    lwz   r11, _pkh_34@l(r11)
    mtctr r11
    bctr

_hkt_35:
    lis   r11, _pkh_35@ha
    lwz   r11, _pkh_35@l(r11)
    mtctr r11
    bctr

_hkt_36:
    lis   r11, _pkh_36@ha
    lwz   r11, _pkh_36@l(r11)
    mtctr r11
    bctr

_hkt_37:
    lis   r11, _pkh_37@ha
    lwz   r11, _pkh_37@l(r11)
    mtctr r11
    bctr

_hkt_38:
    lis   r11, _pkh_38@ha
    lwz   r11, _pkh_38@l(r11)
    mtctr r11
    bctr

_hkt_39:
    lis   r11, _pkh_39@ha
    lwz   r11, _pkh_39@l(r11)
    mtctr r11
    bctr

_hkt_40:
    lis   r11, _pkh_40@ha
    lwz   r11, _pkh_40@l(r11)
    mtctr r11
    bctr

_hkt_41:
    lis   r11, _pkh_41@ha
    lwz   r11, _pkh_41@l(r11)
    mtctr r11
    bctr

_hkf_0:
    li    r3, 0
    blr

_hkf_1:
    lfs   f30, 0x73F0(r4)
    blr

_hkf_2:
    lfs   f31, 0x73F0(r12)
    blr

_hkf_3:
    lis   r11, 0x1000
    lfs   f30, 0x73BC(r11)
    blr

_hkf_4:
    lfs   f29, 0x77A0(r7)
    blr

_hkf_5:
    lfs   f31, 0x7480(r4)
    blr

_hkf_6:
    lfs   f13, 0x7480(r5)
    blr

_hkf_7:
    lis   r11, 0x1000
    lfs   f1, 0x77A0(r11)
    blr

_hkf_8:
    lfs   f1, 0x73BC(r10)
    blr

_hkf_9:
    lfs   f1, 0x73BC(r10)
    blr

_hkf_10:
    lfs   f1, 0x73BC(r10)
    blr

_hkf_11:
    addi  r11, r7, -1
    blr

_hkf_12:
    lfs   f1, 0x751C(r12)
    blr

_hkf_13:
    addi  r30, r30, -1
    blr

_hkf_14:
    bge   _g_020502F4
    blr

_hkf_15:
    li    r3, 0
    b     _g_0201B318

_hkf_16:
    b     _g_0201B204

_hkf_17:
    b     _g_02022BD8

_hkf_18:
    b     _g_02022BD8

_hkf_19:
    b     _g_02022BD8

_hkf_20:
    stfs  f13, 0x34F4(r30)
    blr

_hkf_21:
    stfs  f13, 0x34F4(r30)
    blr

_hkf_22:
    stfs  f13, 0x34F4(r30)
    blr

_hkf_23:
    stfs  f30, 0x34F4(r30)
    blr

_hkf_24:
    beq   _g_0203AC28
    b     _g_0203AC18

_hkf_25:
    beq   _g_0203AC44
    b     _g_0203AC28

_hkf_26:
    fadds f9, f9, f8
    blr

_hkf_27:
    subf  r8, r7, r8
    blr

_hkf_28:
    lwz   r12, 0x6280(r31)
    blr

_hkf_29:
    b     _g_0208BE14

_hkf_30:
    b     _g_0208BE14

_hkf_31:
    b     _g_0208BE14

_hkf_32:
    b     _g_02AA6460

_hkf_33:
    lwz   r3, 0xE0C(r3)
    b     _g_024DC588

_hkf_34:
    beq   _g_021A3830
    b     _g_021A37B8

_hkf_35:
    beq   _g_021A3830
    b     _g_021A37EC

_hkf_36:
    addi  r11, r8, 2
    blr

_hkf_37:
    addi  r11, r8, 1
    blr

_hkf_38:
    stw   r3, 0x118(r29)
    blr

_hkf_39:
    cmplw r9, r10
    blr

_hkf_40:
    lis   r11, 0x1011
    lwz   r11, -0x4284(r11)
    cmplw r11, r0
    blr

_hkf_41:
    lwz   r31, 0xC(r1)
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

0x0202B12C = bla _hkt_0
0x020592D0 = bla _hkt_1
0x020592E8 = bla _hkt_2
0x020592F0 = bla _hkt_3
0x020592F8 = bla _hkt_4
0x0205932C = bla _hkt_5
0x020423B8 = bla _hkt_6
0x0209CF84 = bla _hkt_7
0x02059C94 = bla _hkt_8
0x02059D34 = bla _hkt_9
0x02059DC4 = bla _hkt_10
0x0288D8AC = bla _hkt_11
0x020B3128 = bla _hkt_12
0x0248802C = bla _hkt_13
0x02050C5C = bla _hkt_14
0x0201B314 = b   _hkt_15
0x02050EB0 = bla _hkt_16
0x02023648 = bla _hkt_17
0x02023884 = bla _hkt_18
0x0202BDDC = bla _hkt_19
0x020639D8 = bla _hkt_20
0x020639F0 = bla _hkt_21
0x02063A34 = bla _hkt_22
0x02063A44 = bla _hkt_23
0x0203AC14 = b   _hkt_24
0x0203AC24 = b   _hkt_25
0x02A1C664 = bla _hkt_26
0x02904118 = bla _hkt_27
0x02A1DB74 = bla _hkt_28
0x0208C398 = bla _hkt_29
0x020A1C34 = bla _hkt_30
0x020BF018 = bla _hkt_31
0x0203152C = bla _hkt_32
0x024DC584 = b   _hkt_33
0x021A37B4 = b   _hkt_34
0x021A37E8 = b   _hkt_35
0x0287CF1C = bla _hkt_36
0x0287CF54 = bla _hkt_37
0x02AC6388 = bla _hkt_38
0x024A7424 = bla _hkt_39
0x026BEEE0 = bla _hkt_40
0x02667158 = bla _hkt_41

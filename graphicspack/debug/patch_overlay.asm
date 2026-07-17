; ============================================================================
; TPHD ImGui overlay -- debug graphics-pack codecave (Twilight Princess HD)
;
; This variant loads tphd_tools_debug.rpl. Keep hook logic synchronized with
; graphicspack/release/patch_overlay.asm.
; ============================================================================

[TPHDv81]
moduleMatches = 0x1A03E108, 0xA3175EEA

.origin = codecave

; ------------------------------- data ---------------------------------------
_ovl_state:            ; 0 = export not yet resolved, 1 = resolve attempted
.byte 0
.align 4

_ovl_module:
.int 0                 ; OSDynLoad_Module handle (out)

_ovl_fn:
.int 0                 ; resolved TPHDToolsEntry address (out); 0 until resolved

_ovl_name:
.string "tphd_tools_debug" ; loads tphd_tools_debug.rpl
.align 4

_ovl_export:
.string "TPHDToolsEntry" ; the single export we resolve
.align 4

; --------------------------- present hook -----------------------------------
overlay_present_hook:
    stwu  r1, -0x40(r1)
    mflr  r0
    stw   r0, 0x34(r1)

    ; ---- one-time: acquire module + resolve TPHDToolsEntry ----
    lis   r3, _ovl_state@ha
    lbz   r4, _ovl_state@l(r3)
    cmpwi r4, 0
    bne   ovl_call_entry

    li    r4, 1
    stb   r4, _ovl_state@l(r3)

    lis   r3, _ovl_name@ha
    addi  r3, r3, _ovl_name@l
    lis   r4, _ovl_module@ha
    addi  r4, r4, _ovl_module@l
    bl    import.coreinit.OSDynLoad_Acquire

    lis   r3, _ovl_module@ha
    lwz   r3, _ovl_module@l(r3)
    cmpwi r3, 0
    beq   ovl_call_entry

    li    r4, 0
    lis   r5, _ovl_export@ha
    addi  r5, r5, _ovl_export@l
    lis   r6, _ovl_fn@ha
    addi  r6, r6, _ovl_fn@l
    bl    import.coreinit.OSDynLoad_FindExport

ovl_call_entry:
    lis   r3, _ovl_fn@ha
    lwz   r3, _ovl_fn@l(r3)
    cmpwi r3, 0
    beq   ovl_after_load
    mtctr r3
    li    r3, 0
    lis   r4, 0x1028
    ori   r4, r4, 0x16d8
    lis   r5, 0x1028
    ori   r5, r5, 0x1820
    bctrl

ovl_after_load:
    lis   r0, 0x02bd
    ori   r0, r0, 0xe770
    mtctr r0
    bctrl

    lwz   r0, 0x34(r1)
    mtlr  r0
    addi  r1, r1, 0x40
    blr

; ----------------------- GamePad scan-out hook ------------------------------
drc_copy_hook:
    stwu  r1, -0x20(r1)
    mflr  r0
    stw   r0, 0x1c(r1)
    stw   r3, 0x10(r1)
    stw   r4, 0x0c(r1)

    cmpwi r4, 4
    bne   drc_copy_real

    lis   r9, _ovl_fn@ha
    lwz   r9, _ovl_fn@l(r9)
    cmpwi r9, 0
    beq   drc_copy_real
    mtctr r9
    li    r3, 3
    lwz   r4, 0x10(r1)
    li    r5, 0
    bctrl

drc_copy_real:
    lwz   r3, 0x10(r1)
    lwz   r4, 0x0c(r1)
    bl    import.gx2.GX2CopyColorBufferToScanBuffer

    lwz   r0, 0x1c(r1)
    mtlr  r0
    addi  r1, r1, 0x20
    blr

; --------------------------- VPADRead hook ----------------------------------
vpad_read_hook:
    stwu  r1, -0x20(r1)
    mflr  r0
    stw   r0, 0x1c(r1)
    stw   r4, 0x10(r1)

    bl    import.vpad.VPADRead
    stw   r3, 0x0c(r1)

    lis   r9, _ovl_fn@ha
    lwz   r9, _ovl_fn@l(r9)
    cmpwi r9, 0
    beq   vpad_read_done
    mtctr r9
    li    r3, 1
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    bctrl

vpad_read_done:
    lwz   r3, 0x0c(r1)
    lwz   r0, 0x1c(r1)
    mtlr  r0
    addi  r1, r1, 0x20
    blr

; --------------------------- KPADReadEx hook --------------------------------
kpad_read_hook:
    stwu  r1, -0x20(r1)
    mflr  r0
    stw   r0, 0x1c(r1)
    stw   r4, 0x10(r1)

    bl    import.padscore.KPADReadEx
    stw   r3, 0x0c(r1)

    lis   r9, _ovl_fn@ha
    lwz   r9, _ovl_fn@l(r9)
    cmpwi r9, 0
    beq   kpad_read_done
    mtctr r9
    li    r3, 2
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    bctrl

kpad_read_done:
    lwz   r3, 0x0c(r1)
    lwz   r0, 0x1c(r1)
    mtlr  r0
    addi  r1, r1, 0x20
    blr

; ------------------------ dScnPly::phase_1 hook -----------------------------
; See the synchronized release pack for the RE notes. Dispatch reason 4 before
; chaining Zelda.rpx dScnPly::phase_1 @ 0x02ac1108.
scene_phase1_hook:
    stwu  r1, -0x40(r1)
    mflr  r0
    stw   r0, 0x34(r1)
    stw   r3, 0x30(r1)

    lis   r9, _ovl_fn@ha
    lwz   r9, _ovl_fn@l(r9)
    cmpwi r9, 0
    beq   scene_phase1_real
    mtctr r9
    li    r3, 4
    lwz   r4, 0x30(r1)
    li    r5, 0
    bctrl

scene_phase1_real:
    lwz   r3, 0x30(r1)
    lis   r12, 0x02ac
    ori   r12, r12, 0x1108
    mtctr r12
    bctrl

    lwz   r0, 0x34(r1)
    mtlr  r0
    addi  r1, r1, 0x40
    blr

; --------------------- dScnRoom zone-create phase hook ---------------------
; See the synchronized release pack for the RE notes. Dispatch reason 5 before
; Zelda.rpx FUN_02ac3f68 parses room.dzr, then reason 6 after zone allocation.
room_zone_phase_hook:
    stwu  r1, -0x40(r1)
    mflr  r0
    stw   r0, 0x34(r1)
    stw   r3, 0x30(r1)

    lis   r9, _ovl_fn@ha
    lwz   r9, _ovl_fn@l(r9)
    cmpwi r9, 0
    beq   room_zone_phase_real
    mtctr r9
    li    r3, 5
    lwz   r4, 0x30(r1)
    li    r5, 0
    bctrl

room_zone_phase_real:
    lwz   r3, 0x30(r1)
    lis   r12, 0x02ac
    ori   r12, r12, 0x3f68
    mtctr r12
    bctrl
    stw   r3, 0x2c(r1)

    lis   r9, _ovl_fn@ha
    lwz   r9, _ovl_fn@l(r9)
    cmpwi r9, 0
    beq   room_zone_phase_done
    mtctr r9
    li    r3, 6
    lwz   r4, 0x30(r1)
    li    r5, 0
    bctrl

room_zone_phase_done:
    lwz   r3, 0x2c(r1)
    lwz   r0, 0x34(r1)
    mtlr  r0
    addi  r1, r1, 0x40
    blr

; ------------------------------- hooks --------------------------------------
0x02af0f94 = bla overlay_present_hook
0x02bde82c = bla drc_copy_hook
0x02bfae38 = bla vpad_read_hook
0x02bfb94c = bla kpad_read_hook
0x02bfceac = bla kpad_read_hook
0x10129b28 = .int scene_phase1_hook
0x10129bd8 = .int room_zone_phase_hook

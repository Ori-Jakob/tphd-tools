# Permanent TPHD equipment-modifier thunks.
#
# Zelda.rpx calls these with `bl`, so each thunk returns to the instruction
# immediately following the replaced native instruction. They use only the
# destination register of that instruction plus registers proven dead at the
# corresponding TPHD site.

.section .text, "ax", @progbits
.align 2

.extern g_tphdSuperClawshotEnabled
.extern g_tphdInfiniteSpinnerTimeEnabled
.extern g_tphdRemoteBombsEnabled
.extern g_tphdUnrestrictedBombsEnabled
.extern g_tphdUnrestrictedItemsEnabled
.extern g_tphdFastIronBootsEnabled
.extern g_tphdFastIronBootsMultiplier
.extern g_tphdAlwaysGreatSpinMode

.global tphdSuperClawshotFailureHook
.type tphdSuperClawshotFailureHook, @function
tphdSuperClawshotFailureHook:
    lis 3, g_tphdSuperClawshotEnabled@ha
    lbz 3, g_tphdSuperClawshotEnabled@l(3)
    blr
.size tphdSuperClawshotFailureHook, .-tphdSuperClawshotFailureHook

.global tphdInfiniteSpinnerTimeHook
.type tphdInfiniteSpinnerTimeHook, @function
tphdInfiniteSpinnerTimeHook:
    # Native: r11 = r7 - 1. Enabled: r11 = r7.
    # Use the native destination as address scratch. D-form loads treat RA=0
    # as a literal zero base, so r0 cannot address an RPL global here.
    lis 11, g_tphdInfiniteSpinnerTimeEnabled@ha
    lbz 11, g_tphdInfiniteSpinnerTimeEnabled@l(11)
    xori 11, 11, 1
    sub 11, 7, 11
    blr
.size tphdInfiniteSpinnerTimeHook, .-tphdInfiniteSpinnerTimeHook

.global tphdRemoteBombFuseHook
.type tphdRemoteBombFuseHook, @function
tphdRemoteBombFuseHook:
    # Native: r30 = r30 - 1. Enabled: preserve r30.
    # r0 is dead on both continuations. Save the old timer there, use r30 as
    # the nonzero base/destination for the mode byte, then produce the native
    # destination value. (RA=0 is not a usable load base on PowerPC.)
    or 0, 30, 30
    lis 30, g_tphdRemoteBombsEnabled@ha
    lbz 30, g_tphdRemoteBombsEnabled@l(30)
    xori 30, 30, 1
    sub 30, 0, 30
    blr
.size tphdRemoteBombFuseHook, .-tphdRemoteBombFuseHook

.global tphdUnrestrictedBombsHook
.type tphdUnrestrictedBombsHook, @function
tphdUnrestrictedBombsHook:
    # Replaces `bge 0x020502F4` after `cmplwi r9,3`. Preserve the comparison
    # CR exactly. Disabled follows the native branch; enabled always returns to
    # 0x02050C60. r0/r12/CTR are dead on both native continuations.
    mfcr 0
    lis 12, g_tphdUnrestrictedBombsEnabled@ha
    lbz 12, g_tphdUnrestrictedBombsEnabled@l(12)
    cmpwi 12, 0
    bne .Lunrestricted_allow
    mtcrf 0xff, 0
    blt .Lunrestricted_return
    lis 12, 0x0205
    ori 12, 12, 0x02F4
    mtctr 12
    bctr
.Lunrestricted_allow:
    mtcrf 0xff, 0
.Lunrestricted_return:
    blr
.size tphdUnrestrictedBombsHook, .-tphdUnrestrictedBombsHook

.global tphdUnrestrictedItemsHook
.type tphdUnrestrictedItemsHook, @function
tphdUnrestrictedItemsHook:
    # Called only from checkCastleTownUseItem's native restricted return.
    # Disabled preserves return 0; enabled converts that failure to success.
    lis 3, g_tphdUnrestrictedItemsEnabled@ha
    lbz 3, g_tphdUnrestrictedItemsEnabled@l(3)
    blr
.size tphdUnrestrictedItemsHook, .-tphdUnrestrictedItemsHook

.global tphdUnrestrictedSwordHook
.type tphdUnrestrictedSwordHook, @function
tphdUnrestrictedSwordHook:
    # This replaces only checkItemChangeFromButton's sword-specific call to
    # checkNotBattleStage. That function returns nonzero when sword drawing is
    # prohibited, so the enabled result is zero. Disabled tail-calls the native
    # function with the call site's LR still intact.
    lis 12, g_tphdUnrestrictedItemsEnabled@ha
    lbz 12, g_tphdUnrestrictedItemsEnabled@l(12)
    cmpwi 12, 0
    beq .Lunrestricted_sword_native
    li 3, 0
    blr
.Lunrestricted_sword_native:
    lis 12, 0x0201
    ori 12, 12, 0xB204
    mtctr 12
    bctr
.size tphdUnrestrictedSwordHook, .-tphdUnrestrictedSwordHook

.global tphdFastIronBootsAnimeHook
.type tphdFastIronBootsAnimeHook, @function
tphdFastIronBootsAnimeHook:
    # The three hooked sites call checkBootsMoveAnime. Returning false selects
    # the ordinary movement animation paths used by Dusklight's fast-boots
    # implementation. Disabled tail-calls the native check.
    lis 12, g_tphdFastIronBootsEnabled@ha
    lbz 12, g_tphdFastIronBootsEnabled@l(12)
    cmpwi 12, 0
    beq .Lfast_boots_anime_native
    li 3, 0
    blr
.Lfast_boots_anime_native:
    lis 12, 0x0202
    ori 12, 12, 0x2BD8
    mtctr 12
    bctr
.size tphdFastIronBootsAnimeHook, .-tphdFastIronBootsAnimeHook

.global tphdFastIronBootsStoreF13Hook
.type tphdFastIronBootsStoreF13Hook, @function
tphdFastIronBootsStoreF13Hook:
    # Native instruction: stfs f13,0x34F4(r30). The store itself clobbers no
    # registers or CR fields, so preserve everything used to consult the flag.
    stwu 1, -0x30(1)
    stw 0, 8(1)
    stw 12, 12(1)
    mfcr 0
    stw 0, 16(1)
    stfd 0, 24(1)
    lis 12, g_tphdFastIronBootsEnabled@ha
    lbz 12, g_tphdFastIronBootsEnabled@l(12)
    cmpwi 12, 0
    beq .Lfast_boots_store_f13_native
    lis 12, g_tphdFastIronBootsMultiplier@ha
    lfs 0, g_tphdFastIronBootsMultiplier@l(12)
    stfs 0, 0x34F4(30)
    b .Lfast_boots_store_f13_done
.Lfast_boots_store_f13_native:
    stfs 13, 0x34F4(30)
.Lfast_boots_store_f13_done:
    lfd 0, 24(1)
    lwz 12, 16(1)
    mtcrf 0xff, 12
    lwz 12, 12(1)
    lwz 0, 8(1)
    addi 1, 1, 0x30
    blr
.size tphdFastIronBootsStoreF13Hook, .-tphdFastIronBootsStoreF13Hook

.global tphdFastIronBootsStoreF30Hook
.type tphdFastIronBootsStoreF30Hook, @function
tphdFastIronBootsStoreF30Hook:
    # Same redirect for the one path whose native source is f30.
    stwu 1, -0x30(1)
    stw 0, 8(1)
    stw 12, 12(1)
    mfcr 0
    stw 0, 16(1)
    stfd 0, 24(1)
    lis 12, g_tphdFastIronBootsEnabled@ha
    lbz 12, g_tphdFastIronBootsEnabled@l(12)
    cmpwi 12, 0
    beq .Lfast_boots_store_f30_native
    lis 12, g_tphdFastIronBootsMultiplier@ha
    lfs 0, g_tphdFastIronBootsMultiplier@l(12)
    stfs 0, 0x34F4(30)
    b .Lfast_boots_store_f30_done
.Lfast_boots_store_f30_native:
    stfs 30, 0x34F4(30)
.Lfast_boots_store_f30_done:
    lfd 0, 24(1)
    lwz 12, 16(1)
    mtcrf 0xff, 12
    lwz 12, 12(1)
    lwz 0, 8(1)
    addi 1, 1, 0x30
    blr
.size tphdFastIronBootsStoreF30Hook, .-tphdFastIronBootsStoreF30Hook

.global tphdAlwaysGreatSpinSkillHook
.type tphdAlwaysGreatSpinSkillHook, @function
tphdAlwaysGreatSpinSkillHook:
    # Replaces checkCutLargeTurnState's native `beq 0x0203AC28` after the
    # acquired-skill/training-flag test. Mode 2 bypasses that test; modes 0/1
    # reproduce the native branch. This is a jump hook, so LR still belongs to
    # checkCutLargeTurnState's caller and must remain untouched.
    mfcr 11
    lis 12, g_tphdAlwaysGreatSpinMode@ha
    lbz 12, g_tphdAlwaysGreatSpinMode@l(12)
    cmpwi 12, 2
    beq .Lgreat_spin_skill_pass
    mtcrf 0xff, 11
    beq .Lgreat_spin_skill_fail
.Lgreat_spin_skill_pass:
    mtcrf 0xff, 11
    lis 12, 0x0203
    ori 12, 12, 0xAC18
    mtctr 12
    bctr
.Lgreat_spin_skill_fail:
    lis 12, 0x0203
    ori 12, 12, 0xAC28
    mtctr 12
    bctr
.size tphdAlwaysGreatSpinSkillHook, .-tphdAlwaysGreatSpinSkillHook

.global tphdAlwaysGreatSpinHealthHook
.type tphdAlwaysGreatSpinHealthHook, @function
tphdAlwaysGreatSpinHealthHook:
    # Replaces the native `beq 0x0203AC44` after current/max-health compare.
    # Either enabled mode accepts non-full health; disabled preserves the
    # compare and its native success/failure destinations.
    mfcr 11
    lis 12, g_tphdAlwaysGreatSpinMode@ha
    lbz 12, g_tphdAlwaysGreatSpinMode@l(12)
    cmpwi 12, 0
    bne .Lgreat_spin_health_pass
    mtcrf 0xff, 11
    beq .Lgreat_spin_health_pass
    lis 12, 0x0203
    ori 12, 12, 0xAC28
    mtctr 12
    bctr
.Lgreat_spin_health_pass:
    mtcrf 0xff, 11
    lis 12, 0x0203
    ori 12, 12, 0xAC44
    mtctr 12
    bctr
.size tphdAlwaysGreatSpinHealthHook, .-tphdAlwaysGreatSpinHealthHook

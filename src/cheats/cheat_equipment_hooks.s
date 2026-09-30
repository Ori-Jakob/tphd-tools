.section .text, "ax", @progbits
.align 2

.extern g_tphdSuperClawshotEnabled
.extern g_tphdInfiniteSpinnerTimeEnabled
.extern g_tphdSpinnerSpeed
.extern g_tphdRemoteBombsEnabled
.extern g_tphdUnrestrictedBombsEnabled
.extern g_tphdUnrestrictedItemsEnabled
.extern g_tphdFastIronBootsEnabled
.extern g_tphdFastIronBootsMultiplier
.extern g_tphdAlwaysGreatSpinMode
.extern g_tphdClawshotBossShootSpeed
.extern g_tphdClawshotShootSpeed
.extern g_tphdClawshotRange
.extern g_tphdClawshotPullSpeed
.extern g_tphdClawshotReturnSpeed

.global tphdSuperClawshotFailureHook
.type tphdSuperClawshotFailureHook, @function
tphdSuperClawshotFailureHook:
    lis 3, g_tphdSuperClawshotEnabled@ha
    lbz 3, g_tphdSuperClawshotEnabled@l(3)
    blr
.size tphdSuperClawshotFailureHook, .-tphdSuperClawshotFailureHook

.global tphdClawshotBossShootSpeedHook
.type tphdClawshotBossShootSpeedHook, @function
tphdClawshotBossShootSpeedHook:
    lis 11, g_tphdClawshotBossShootSpeed@ha
    lfs 30, g_tphdClawshotBossShootSpeed@l(11)
    blr
.size tphdClawshotBossShootSpeedHook, .-tphdClawshotBossShootSpeedHook

.global tphdClawshotReturnSpeedHook
.type tphdClawshotReturnSpeedHook, @function
tphdClawshotReturnSpeedHook:
    lis 11, g_tphdClawshotReturnSpeed@ha
    lfs 31, g_tphdClawshotReturnSpeed@l(11)
    blr
.size tphdClawshotReturnSpeedHook, .-tphdClawshotReturnSpeedHook

.global tphdClawshotShootSpeedF30Hook
.type tphdClawshotShootSpeedF30Hook, @function
tphdClawshotShootSpeedF30Hook:
    lis 11, g_tphdClawshotShootSpeed@ha
    lfs 30, g_tphdClawshotShootSpeed@l(11)
    blr
.size tphdClawshotShootSpeedF30Hook, .-tphdClawshotShootSpeedF30Hook

.global tphdClawshotRangeF29Hook
.type tphdClawshotRangeF29Hook, @function
tphdClawshotRangeF29Hook:
    lis 11, g_tphdClawshotRange@ha
    lfs 29, g_tphdClawshotRange@l(11)
    blr
.size tphdClawshotRangeF29Hook, .-tphdClawshotRangeF29Hook

.global tphdClawshotPullSpeedF31Hook
.type tphdClawshotPullSpeedF31Hook, @function
tphdClawshotPullSpeedF31Hook:
    lis 11, g_tphdClawshotPullSpeed@ha
    lfs 31, g_tphdClawshotPullSpeed@l(11)
    blr
.size tphdClawshotPullSpeedF31Hook, .-tphdClawshotPullSpeedF31Hook

.global tphdClawshotPullSpeedF13Hook
.type tphdClawshotPullSpeedF13Hook, @function
tphdClawshotPullSpeedF13Hook:
    lis 11, g_tphdClawshotPullSpeed@ha
    lfs 13, g_tphdClawshotPullSpeed@l(11)
    blr
.size tphdClawshotPullSpeedF13Hook, .-tphdClawshotPullSpeedF13Hook

.global tphdClawshotRangeF1Hook
.type tphdClawshotRangeF1Hook, @function
tphdClawshotRangeF1Hook:
    lis 11, g_tphdClawshotRange@ha
    lfs 1, g_tphdClawshotRange@l(11)
    blr
.size tphdClawshotRangeF1Hook, .-tphdClawshotRangeF1Hook

.global tphdClawshotShootSpeedF1Hook
.type tphdClawshotShootSpeedF1Hook, @function
tphdClawshotShootSpeedF1Hook:
    lis 11, g_tphdClawshotShootSpeed@ha
    lfs 1, g_tphdClawshotShootSpeed@l(11)
    blr
.size tphdClawshotShootSpeedF1Hook, .-tphdClawshotShootSpeedF1Hook

.global tphdInfiniteSpinnerTimeHook
.type tphdInfiniteSpinnerTimeHook, @function
tphdInfiniteSpinnerTimeHook:
    lis 11, g_tphdInfiniteSpinnerTimeEnabled@ha
    lbz 11, g_tphdInfiniteSpinnerTimeEnabled@l(11)
    xori 11, 11, 1
    sub 11, 7, 11
    blr
.size tphdInfiniteSpinnerTimeHook, .-tphdInfiniteSpinnerTimeHook

.global tphdSpinnerSpeedHook
.type tphdSpinnerSpeedHook, @function
tphdSpinnerSpeedHook:
    lis 12, g_tphdSpinnerSpeed@ha
    lfs 1, g_tphdSpinnerSpeed@l(12)
    blr
.size tphdSpinnerSpeedHook, .-tphdSpinnerSpeedHook

.global tphdRemoteBombFuseHook
.type tphdRemoteBombFuseHook, @function
tphdRemoteBombFuseHook:
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
    lis 3, g_tphdUnrestrictedItemsEnabled@ha
    lbz 3, g_tphdUnrestrictedItemsEnabled@l(3)
    blr
.size tphdUnrestrictedItemsHook, .-tphdUnrestrictedItemsHook

.global tphdUnrestrictedSwordHook
.type tphdUnrestrictedSwordHook, @function
tphdUnrestrictedSwordHook:
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

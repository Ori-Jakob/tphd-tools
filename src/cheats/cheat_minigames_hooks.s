.section .text, "ax", @progbits
.align 2

.extern g_tphdGameDataDelta
.extern g_tphdStarGameInstantWinEnabled
.extern g_tphdBoatNoPointLossEnabled
.extern g_tphdBoatPointMultiplier
.extern g_tphdFreezeTimerEnabled
.extern tphdTimerFreezeStep
.extern g_tphdGoatInstantWinEnabled
.extern g_tphdFruitComboKeepEnabled
.extern g_tphdEasySumoEnabled

.global tphdStarGameClearHook
.type tphdStarGameClearHook, @function
tphdStarGameClearHook:
    lis 12, g_tphdStarGameInstantWinEnabled@ha
    lbz 12, g_tphdStarGameInstantWinEnabled@l(12)
    cmpwi 12, 0
    beq .Lstar_game_native
    li 3, 1
    blr
.Lstar_game_native:
    lwz 3, 0xE0C(3)
    lis 12, 0x0271
    ori 12, 12, 0xE34C
    mtctr 12
    bctr
.size tphdStarGameClearHook, .-tphdStarGameClearHook

.global tphdCanoeScoreSkipHook
.type tphdCanoeScoreSkipHook, @function
tphdCanoeScoreSkipHook:
    mfcr 11
    lis 12, g_tphdBoatNoPointLossEnabled@ha
    lbz 12, g_tphdBoatNoPointLossEnabled@l(12)
    cmpwi 12, 0
    bne .Lcanoe_skip
    mtcrf 0xff, 11
    beq .Lcanoe_skip_restored
    lis 12, 0x021A
    ori 12, 12, 0x37EC
    mtctr 12
    bctr
.Lcanoe_skip:
    mtcrf 0xff, 11
.Lcanoe_skip_restored:
    lis 12, 0x021A
    ori 12, 12, 0x3830
    mtctr 12
    bctr
.size tphdCanoeScoreSkipHook, .-tphdCanoeScoreSkipHook

.global tphdBoatScoreGain2Hook
.type tphdBoatScoreGain2Hook, @function
tphdBoatScoreGain2Hook:
    li 11, 2
    b .Lboat_score_common
.size tphdBoatScoreGain2Hook, .-tphdBoatScoreGain2Hook

.global tphdBoatScoreGain1Hook
.type tphdBoatScoreGain1Hook, @function
tphdBoatScoreGain1Hook:
    li 11, 1
.Lboat_score_common:
    stwu 1, -0x20(1)
    stw 0, 8(1)
    stw 10, 12(1)
    mfcr 0
    stw 0, 16(1)
    lis 10, g_tphdBoatPointMultiplier@ha
    lbz 10, g_tphdBoatPointMultiplier@l(10)
    cmpwi 10, 1
    ble .Lboat_score_add
    mullw 11, 11, 10
.Lboat_score_add:
    add 11, 11, 8
    cmpwi 11, 99
    ble .Lboat_score_done
    li 11, 99
.Lboat_score_done:
    lwz 0, 16(1)
    mtcrf 0xff, 0
    lwz 10, 12(1)
    lwz 0, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdBoatScoreGain1Hook, .-tphdBoatScoreGain1Hook

.global tphdTimerFreezeHook
.type tphdTimerFreezeHook, @function
tphdTimerFreezeHook:
    lis 12, g_tphdFreezeTimerEnabled@ha
    lbz 12, g_tphdFreezeTimerEnabled@l(12)
    mtctr 12
    bdnz .Ltimer_native
    mflr 0
    stwu 1, -0x20(1)
    stw 0, 0x24(1)
    stw 3, 8(1)
    stw 4, 12(1)
    or 3, 29, 29
    lwz 4, 8(1)
    lwz 5, 12(1)
    bl tphdTimerFreezeStep
    lwz 3, 8(1)
    lwz 4, 12(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
.Ltimer_native:
    stw 3, 0x118(29)
    blr
.size tphdTimerFreezeHook, .-tphdTimerFreezeHook

.global tphdGoatInstantWinHook
.type tphdGoatInstantWinHook, @function
tphdGoatInstantWinHook:
    lis 11, g_tphdGoatInstantWinEnabled@ha
    lbz 11, g_tphdGoatInstantWinEnabled@l(11)
    cmpwi 11, 0
    beq .Lgoat_native
    li 11, 1
    stb 11, 0xFCA(30)
.Lgoat_native:
    cmplw 9, 10
    blr
.size tphdGoatInstantWinHook, .-tphdGoatInstantWinHook

.global tphdFruitComboKeepHook
.type tphdFruitComboKeepHook, @function
tphdFruitComboKeepHook:
    lis 11, g_tphdGameDataDelta@ha
    lwz 11, g_tphdGameDataDelta@l(11)
    addis 11, 11, 0x1011
    lwz 11, -0x4284(11)
    lis 12, g_tphdFruitComboKeepEnabled@ha
    lbz 12, g_tphdFruitComboKeepEnabled@l(12)
    cmpwi 12, 0
    beq .Lfruit_native
    cmpwi 11, -1
    beq .Lfruit_native
    cmplw 0, 0
    blr
.Lfruit_native:
    cmplw 11, 0
    blr
.size tphdFruitComboKeepHook, .-tphdFruitComboKeepHook

.global tphdEasySumoHook
.type tphdEasySumoHook, @function
tphdEasySumoHook:
    lis 12, g_tphdEasySumoEnabled@ha
    lbz 12, g_tphdEasySumoEnabled@l(12)
    cmpwi 12, 0
    beq .Leasy_sumo_native
    li 0, 4
    stw 0, 0xE74(31)
.Leasy_sumo_native:
    lwz 31, 0xC(1)
    blr
.size tphdEasySumoHook, .-tphdEasySumoHook

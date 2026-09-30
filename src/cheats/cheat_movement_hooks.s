.section .text, "ax", @progbits
.align 2

.extern g_tphdClimbingSpeedMultiplier
.extern g_tphdClimbHeightMultiplier
.extern g_tphdBlockPushSpeedMultiplier
.extern g_tphdBlockPushSpeedQuarters
.extern g_tphdBemosMovePhase
.extern g_tphdChainPullSpeedMultiplier
.extern g_tphdChainPullSpeedQuarters
.extern tphdLv4ChandelierCurveDelta
.extern g_tphdCrawlSpeedMultiplier
.extern g_tphdRollSpeedMultiplier
.extern g_tphdTimeSpeedMultiplier
.extern tphdInstantTextPrepareSequence
.extern tphdInstantTextPrepareIsSend
.extern tphdInstantTextFilterIsSend

.global tphdClimbingSpeedReturnHook
.type tphdClimbingSpeedReturnHook, @function
tphdClimbingSpeedReturnHook:
    lis 12, g_tphdClimbingSpeedMultiplier@ha
    lfs 0, g_tphdClimbingSpeedMultiplier@l(12)
    fmuls 1, 1, 0
    blr
.size tphdClimbingSpeedReturnHook, .-tphdClimbingSpeedReturnHook

.global tphdCrawlSpeedReturnHook
.type tphdCrawlSpeedReturnHook, @function
tphdCrawlSpeedReturnHook:
    lis 12, g_tphdCrawlSpeedMultiplier@ha
    lfs 0, g_tphdCrawlSpeedMultiplier@l(12)
    fmuls 1, 1, 0
    blr
.size tphdCrawlSpeedReturnHook, .-tphdCrawlSpeedReturnHook

.global tphdBlockPushSpeedReturnHook
.type tphdBlockPushSpeedReturnHook, @function
tphdBlockPushSpeedReturnHook:
    lis 12, g_tphdBlockPushSpeedMultiplier@ha
    lfs 0, g_tphdBlockPushSpeedMultiplier@l(12)
    fmuls 1, 1, 0
    blr
.size tphdBlockPushSpeedReturnHook, .-tphdBlockPushSpeedReturnHook

.global tphdBlockPullDurationHook
.type tphdBlockPullDurationHook, @function
tphdBlockPullDurationHook:
    lha 0, 0xa(12)
    mulli 0, 0, 4
    lis 10, g_tphdBlockPushSpeedQuarters@ha
    lwz 10, g_tphdBlockPushSpeedQuarters@l(10)
    srwi 11, 10, 1
    add 0, 0, 11
    divw 0, 0, 10
    cmpwi 0, 1
    bge 1f
    li 0, 1
1:
    blr
.size tphdBlockPullDurationHook, .-tphdBlockPullDurationHook

.global tphdBlockPushDurationHook
.type tphdBlockPushDurationHook, @function
tphdBlockPushDurationHook:
    lha 7, 0x4(12)
    mulli 7, 7, 4
    lis 11, g_tphdBlockPushSpeedQuarters@ha
    lwz 11, g_tphdBlockPushSpeedQuarters@l(11)
    srwi 0, 11, 1
    add 7, 7, 0
    divw 7, 7, 11
    cmpwi 7, 1
    bge 1f
    li 7, 1
1:
    blr
.size tphdBlockPushDurationHook, .-tphdBlockPushDurationHook

.global tphdBemosDurationHook
.type tphdBemosDurationHook, @function
tphdBemosDurationHook:
    li 0, 13
    mulli 0, 0, 4
    lis 10, g_tphdBlockPushSpeedQuarters@ha
    lwz 10, g_tphdBlockPushSpeedQuarters@l(10)
    srwi 11, 10, 1
    add 0, 0, 11
    divw 0, 0, 10
    cmpwi 0, 1
    bge 1f
    li 0, 1
1:
    blr
.size tphdBemosDurationHook, .-tphdBemosDurationHook

.global tphdBemosPhaseHook
.type tphdBemosPhaseHook, @function
tphdBemosPhaseHook:
    lis 12, g_tphdBemosMovePhase@ha
    lfs 13, g_tphdBemosMovePhase@l(12)
    blr
.size tphdBemosPhaseHook, .-tphdBemosPhaseHook

.set _daObjCwall_initWalk_afterDurationHook, 0x026fd660
.set _daObjCwall_initWalk_afterPhaseHook, 0x026fd674

.global tphdChainWallDurationHook
.type tphdChainWallDurationHook, @function
tphdChainWallDurationHook:
    li 11, 13
    mulli 11, 11, 4
    lis 10, g_tphdChainPullSpeedQuarters@ha
    lwz 10, g_tphdChainPullSpeedQuarters@l(10)
    srwi 9, 10, 1
    add 11, 11, 9
    divw 11, 11, 10
    cmpwi 11, 1
    bge 1f
    li 11, 1
1:
    lis 12, _daObjCwall_initWalk_afterDurationHook@ha
    addi 12, 12, _daObjCwall_initWalk_afterDurationHook@l
    mtctr 12
    bctr
.size tphdChainWallDurationHook, .-tphdChainWallDurationHook

.global tphdChainWallPhaseHook
.type tphdChainWallPhaseHook, @function
tphdChainWallPhaseHook:
    li 11, 13
    mulli 11, 11, 4
    lis 10, g_tphdChainPullSpeedQuarters@ha
    lwz 10, g_tphdChainPullSpeedQuarters@l(10)
    srwi 9, 10, 1
    add 11, 11, 9
    divw 11, 11, 10
    cmpwi 11, 1
    bge 1f
    li 11, 1
1:
    li 12, 0x7fff
    divw 12, 12, 11
    lis 10, _daObjCwall_initWalk_afterPhaseHook@ha
    addi 10, 10, _daObjCwall_initWalk_afterPhaseHook@l
    mtctr 10
    bctr
.size tphdChainWallPhaseHook, .-tphdChainWallPhaseHook

.global tphdChainWallElapsedHook
.type tphdChainWallElapsedHook, @function
tphdChainWallElapsedHook:
    lha 11, 0xf18(29)
    li 8, 0x7fff
    divw 8, 8, 9
    subf 8, 11, 8
    blr
.size tphdChainWallElapsedHook, .-tphdChainWallElapsedHook

.global tphdHumanChainMaxSpeedBlendHook
.type tphdHumanChainMaxSpeedBlendHook, @function
tphdHumanChainMaxSpeedBlendHook:
    lfs 10, 0x7474(12)
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 1, 0x10(1)
    lis 12, g_tphdChainPullSpeedMultiplier@ha
    lfs 1, g_tphdChainPullSpeedMultiplier@l(12)
    fmuls 10, 10, 1
    lfd 1, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdHumanChainMaxSpeedBlendHook, .-tphdHumanChainMaxSpeedBlendHook

.global tphdHumanChainMaxSpeedActionHook
.type tphdHumanChainMaxSpeedActionHook, @function
tphdHumanChainMaxSpeedActionHook:
    lfs 0, 0x7474(10)
    b tphdScaleChainMaxSpeed
.size tphdHumanChainMaxSpeedActionHook, .-tphdHumanChainMaxSpeedActionHook

.global tphdWolfChainMaxSpeedActionHook
.type tphdWolfChainMaxSpeedActionHook, @function
tphdWolfChainMaxSpeedActionHook:
    lfs 0, 0x7474(12)
    b tphdScaleChainMaxSpeed
.size tphdWolfChainMaxSpeedActionHook, .-tphdWolfChainMaxSpeedActionHook

.type tphdScaleChainMaxSpeed, @function
tphdScaleChainMaxSpeed:
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 1, 0x10(1)
    lis 12, g_tphdChainPullSpeedMultiplier@ha
    lfs 1, g_tphdChainPullSpeedMultiplier@l(12)
    fmuls 0, 0, 1
    lfd 1, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdScaleChainMaxSpeed, .-tphdScaleChainMaxSpeed

.global tphdHumanChainAnimationHeavyHook
.type tphdHumanChainAnimationHeavyHook, @function
tphdHumanChainAnimationHeavyHook:
    lfs 1, 0x7314(9)
    b tphdScaleChainAnimation
.size tphdHumanChainAnimationHeavyHook, .-tphdHumanChainAnimationHeavyHook

.global tphdHumanChainAnimationNormalHook
.type tphdHumanChainAnimationNormalHook, @function
tphdHumanChainAnimationNormalHook:
    fmr 1, 29
    b tphdScaleChainAnimation
.size tphdHumanChainAnimationNormalHook, .-tphdHumanChainAnimationNormalHook

.global tphdWolfChainAnimationHeavyHook
.type tphdWolfChainAnimationHeavyHook, @function
tphdWolfChainAnimationHeavyHook:
    lfs 1, 0x7314(10)
    b tphdScaleChainAnimation
.size tphdWolfChainAnimationHeavyHook, .-tphdWolfChainAnimationHeavyHook

.global tphdWolfChainAnimationNormalHook
.type tphdWolfChainAnimationNormalHook, @function
tphdWolfChainAnimationNormalHook:
    fmr 1, 31
    b tphdScaleChainAnimation
.size tphdWolfChainAnimationNormalHook, .-tphdWolfChainAnimationNormalHook

.type tphdScaleChainAnimation, @function
tphdScaleChainAnimation:
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 0, 0x10(1)
    lis 12, g_tphdChainPullSpeedMultiplier@ha
    lfs 0, g_tphdChainPullSpeedMultiplier@l(12)
    fmuls 1, 1, 0
    lfd 0, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdScaleChainAnimation, .-tphdScaleChainAnimation

.global tphdLv4ChandelierCurveHook
.type tphdLv4ChandelierCurveHook, @function
tphdLv4ChandelierCurveHook:
    stwu 1, -0x70(1)
    mflr 0
    stw 0, 0x04(1)
    mfctr 0
    stw 0, 0x28(1)
    mfcr 0
    stw 0, 0x2c(1)
    mfxer 0
    stw 0, 0x30(1)
    stw 3, 0x34(1)
    stw 4, 0x38(1)
    stw 5, 0x3c(1)
    stw 6, 0x40(1)
    stw 7, 0x44(1)
    stw 8, 0x48(1)
    stw 9, 0x4c(1)
    stw 10, 0x50(1)
    stw 11, 0x54(1)
    stw 12, 0x58(1)

    mr 3, 29
    mr 4, 5
    bl tphdLv4ChandelierCurveDelta
    stw 3, 0x5c(1)

    lwz 3, 0x34(1)
    lwz 4, 0x38(1)
    lwz 5, 0x3c(1)
    lwz 6, 0x40(1)
    lwz 7, 0x44(1)
    lwz 8, 0x48(1)
    lwz 9, 0x4c(1)
    lwz 10, 0x50(1)
    lwz 11, 0x54(1)
    lwz 12, 0x58(1)
    lwz 0, 0x30(1)
    mtxer 0
    lwz 0, 0x2c(1)
    mtcrf 0xff, 0
    lwz 0, 0x28(1)
    mtctr 0
    lwz 0, 0x04(1)
    mtlr 0
    lwz 0, 0x5c(1)
    addi 1, 1, 0x70
    blr
.size tphdLv4ChandelierCurveHook, .-tphdLv4ChandelierCurveHook

.global tphdWolfClimbAnimationHook
.type tphdWolfClimbAnimationHook, @function
tphdWolfClimbAnimationHook:
    lfs 1, 0x7090(10)
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 0, 0x10(1)
    lis 12, g_tphdClimbingSpeedMultiplier@ha
    lfs 0, g_tphdClimbingSpeedMultiplier@l(12)
    fmuls 1, 1, 0
    lfd 0, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdWolfClimbAnimationHook, .-tphdWolfClimbAnimationHook

.global tphdWolfClimbParamHook
.type tphdWolfClimbParamHook, @function
tphdWolfClimbParamHook:
    lfs 2, 0x8(5)
    lfs 1, 0x4(5)
    lfs 3, 0xc(5)

    lis 12, 0x1001
    addi 12, 12, -0x1a84
    lis 11, g_tphdGameDataDelta@ha
    lwz 11, g_tphdGameDataDelta@l(11)
    add 12, 12, 11
    cmplw 5, 12
    blt 1f
    addi 11, 12, 0x64
    cmplw 5, 11
    bgt 1f
    lis 12, g_tphdClimbingSpeedMultiplier@ha
    lfs 0, g_tphdClimbingSpeedMultiplier@l(12)
    fmuls 1, 1, 0
1:
    lha 5, 0x0(5)
    lis 12, 0x0201
    ori 12, 12, 0xee8c
    mtctr 12
    bctr
.size tphdWolfClimbParamHook, .-tphdWolfClimbParamHook

.global tphdHumanLedgeClimbAnimationHook
.type tphdHumanLedgeClimbAnimationHook, @function
tphdHumanLedgeClimbAnimationHook:
    lis 12, g_tphdClimbingSpeedMultiplier@ha
    lfs 1, g_tphdClimbingSpeedMultiplier@l(12)
    blr
.size tphdHumanLedgeClimbAnimationHook, .-tphdHumanLedgeClimbAnimationHook

.global tphdClimbProbeHeightHook
.type tphdClimbProbeHeightHook, @function
tphdClimbProbeHeightHook:
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 10, 0x10(1)
    lis 12, g_tphdClimbHeightMultiplier@ha
    lfs 10, g_tphdClimbHeightMultiplier@l(12)
    fmadds 31, 22, 10, 23
    lfd 10, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdClimbProbeHeightHook, .-tphdClimbProbeHeightHook

.global tphdClimbDeltaHeightHook
.type tphdClimbDeltaHeightHook, @function
tphdClimbDeltaHeightHook:
    fsubs 6, 11, 23
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 10, 0x10(1)
    lis 12, g_tphdClimbHeightMultiplier@ha
    lfs 10, g_tphdClimbHeightMultiplier@l(12)
    fdivs 6, 6, 10
    lfd 10, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdClimbDeltaHeightHook, .-tphdClimbDeltaHeightHook

.global tphdRollFrontSpeedHook
.type tphdRollFrontSpeedHook, @function
tphdRollFrontSpeedHook:
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 0, 0x10(1)
    lis 12, g_tphdRollSpeedMultiplier@ha
    lfs 0, g_tphdRollSpeedMultiplier@l(12)
    fmuls 12, 12, 0
    fmuls 11, 11, 0
    lfd 0, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    lwz 0, 0x574(31)
    blr
.size tphdRollFrontSpeedHook, .-tphdRollFrontSpeedHook

.global tphdTimeRateHook
.type tphdTimeRateHook, @function
tphdTimeRateHook:
    lfs 8, 0x1248(30)
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 0, 0x10(1)
    lis 12, g_tphdTimeSpeedMultiplier@ha
    lfs 0, g_tphdTimeSpeedMultiplier@l(12)
    fmuls 8, 8, 0
    lfd 0, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdTimeRateHook, .-tphdTimeRateHook

.global tphdAlternateTimeRateHook
.type tphdAlternateTimeRateHook, @function
tphdAlternateTimeRateHook:
    lfs 10, 0x1248(30)
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 0, 0x10(1)
    lis 12, g_tphdTimeSpeedMultiplier@ha
    lfs 0, g_tphdTimeSpeedMultiplier@l(12)
    fmuls 10, 10, 0
    lfd 0, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdAlternateTimeRateHook, .-tphdAlternateTimeRateHook

.global tphdDoBeginInstantTextHook
.type tphdDoBeginInstantTextHook, @function
tphdDoBeginInstantTextHook:
    mflr 0
    stwu 1, -0x20(1)
    stw 0, 0x24(1)
    stw 3, 8(1)
    bl .Ldo_begin_native
    lwz 3, 8(1)
    bl tphdInstantTextPrepareSequence
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr
.Ldo_begin_native:
    stwu 1, -0x30(1)
    lis 12, 0x02a5
    ori 12, 12, 0x8588
    mtctr 12
    bctr
.size tphdDoBeginInstantTextHook, .-tphdDoBeginInstantTextHook

.global tphdDoIsReadyInstantTextHook
.type tphdDoIsReadyInstantTextHook, @function
tphdDoIsReadyInstantTextHook:
    mflr 0
    stwu 1, -0x10(1)
    stw 0, 0x14(1)
    stw 3, 8(1)
    bl tphdInstantTextPrepareSequence
    lwz 3, 8(1)
    bl .Ldo_is_ready_native
    lwz 0, 0x14(1)
    mtlr 0
    addi 1, 1, 0x10
    blr
.Ldo_is_ready_native:
    mflr 0
    lis 12, 0x02a5
    ori 12, 12, 0x8b58
    mtctr 12
    bctr
.size tphdDoIsReadyInstantTextHook, .-tphdDoIsReadyInstantTextHook

.global tphdDoTagInstantTextHook
.type tphdDoTagInstantTextHook, @function
tphdDoTagInstantTextHook:
    mflr 0
    stwu 1, -0x20(1)
    stw 0, 0x24(1)
    stw 3, 8(1)
    bl .Ldo_tag_native
    stw 3, 12(1)
    lwz 3, 8(1)
    bl tphdInstantTextPrepareSequence
    lwz 3, 12(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr
.Ldo_tag_native:
    mflr 0
    lis 12, 0x02a5
    ori 12, 12, 0x94b4
    mtctr 12
    bctr
.size tphdDoTagInstantTextHook, .-tphdDoTagInstantTextHook

.global tphdIsSendInstantTextHook
.type tphdIsSendInstantTextHook, @function
tphdIsSendInstantTextHook:
    mflr 0
    stwu 1, -0x10(1)
    stw 0, 0x14(1)
    stw 3, 8(1)
    bl tphdInstantTextPrepareIsSend
    lwz 3, 8(1)
    bl .Lis_send_native
    lwz 4, 8(1)
    bl tphdInstantTextFilterIsSend
    lwz 0, 0x14(1)
    mtlr 0
    addi 1, 1, 0x10
    blr
.Lis_send_native:
    mflr 0
    lis 12, 0x02a6
    ori 12, 12, 0x7d1c
    mtctr 12
    bctr
.size tphdIsSendInstantTextHook, .-tphdIsSendInstantTextHook

.global tphdInstantTextOutnowAcceptHook
.type tphdInstantTextOutnowAcceptHook, @function
tphdInstantTextOutnowAcceptHook:
    stwu 1, -0x10(1)
    mflr 0
    stw 0, 0x14(1)
    bl tphdInstantTextOutnowAccept
    lwz 0, 0x14(1)
    mtlr 0
    addi 1, 1, 0x10
    blr
.size tphdInstantTextOutnowAcceptHook, .-tphdInstantTextOutnowAcceptHook

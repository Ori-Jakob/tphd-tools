# Permanent TPHD quality-of-life movement and day-clock thunks.

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

# The ladder, wall/vine, ledge-shimmy, crawl, and push/pull helpers all return
# their animation-driven movement rate in f1. A multiplier of one is therefore
# exactly the native result, including each form's own HIO tuning.
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

# Movebox actors keep their own travel timers. The slider advances in quarter
# steps, so nativeFrames * 4 / speedQuarters gives an inverse duration without
# floating-point conversion code. Keep the native engagement thresholds, round
# travel to the nearest frame, and retain a one-frame minimum.

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

# Obj_bm has the same kind of eased grid motion as Movebox, but its duration
# and phase are fixed constants rather than attribute-table fields.
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

# daObjCwall_c::initWalk is a leaf function, so these are non-linking jump
# hooks. Declare the native continuations once and return through CTR without
# disturbing the actor caller's LR.
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
    li 12, 0x7fff
    divw 12, 12, 11
    lis 10, _daObjCwall_initWalk_afterPhaseHook@ha
    addi 10, 10, _daObjCwall_initWalk_afterPhaseHook@l
    mtctr 10
    bctr
.size tphdChainWallPhaseHook, .-tphdChainWallPhaseHook

# modeWalk has already loaded the stored phase step into r9. Derive the exact
# duration from it so changing the slider during a pull cannot desynchronize
# the elapsed-frame calculation from the active movement.
.global tphdChainWallElapsedHook
.type tphdChainWallElapsedHook, @function
tphdChainWallElapsedHook:
    li 8, 0x7fff
    divw 8, 8, 9
    subf 8, 11, 8
    blr
.size tphdChainWallElapsedHook, .-tphdChainWallElapsedHook

# Chain pulling has a separate movement limiter and animation setup for human
# and Wolf Link. Reproduce each native value and scale only the chain-specific
# branch. Save scratch state that the displaced instruction did not modify.
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

# Obj_Lv4Chan expects the selected curve contribution in r0. The helper needs
# the chandelier actor (r29) and clamped Link animation frame (r5). Preserve
# every volatile register and special register that the displaced lwz left
# untouched, then return the helper result in r0.
.global tphdLv4ChandelierCurveHook
.type tphdLv4ChandelierCurveHook, @function
tphdLv4ChandelierCurveHook:
    # Keep the ABI linkage and eight-word parameter-save areas free for the
    # C++ helper; saved live state starts after them.
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

# Wolf Link's ledge-ready procedure has its own animation setup instead of the
# human hang-rate helper. Reproduce the displaced native load before applying
# the shared climbing multiplier.
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

# setSingleAnimeWolfParam is a leaf tail helper. This thunk is entered with a
# non-linking branch so the original caller LR survives. Scale only descriptors
# in Wolf Link's wall-hang HIO block, reproduce the helper, then tail-call the
# native setSingleAnimeWolf implementation.
.global tphdWolfClimbParamHook
.type tphdWolfClimbParamHook, @function
tphdWolfClimbParamHook:
    lfs 2, 0x8(5)
    lfs 1, 0x4(5)
    lfs 3, 0xc(5)

    lis 12, 0x1001
    addi 12, 12, -0x1a84
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

# Human ledge-top climbing has a dedicated animation call rather than using
# the ladder/wall movement-rate accessors.
.global tphdHumanLedgeClimbAnimationHook
.type tphdHumanLedgeClimbAnimationHook, @function
tphdHumanLedgeClimbAnimationHook:
    lfs 1, 0x7014(11)
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
.size tphdHumanLedgeClimbAnimationHook, .-tphdHumanLedgeClimbAnimationHook

# The wall-edge probe is shared by human and Wolf Link after the game selects
# the appropriate form-specific hang height in f22. Extend only the probe Y;
# the selected native threshold remains in f22 for the later classification.
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

# Native: f6 = ledgeY - bodyY. Dividing that delta by the multiplier is
# equivalent to scaling every form-specific climb threshold while retaining
# the engine's normal action selection (step, grab, jump-climb, or hang).
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

# Front roll: scale both the animation input and the final post-clamp movement
# speed. The latter remains subject to the native water/heavy multipliers.
.global tphdRollFrontAnimationHook
.type tphdRollFrontAnimationHook, @function
tphdRollFrontAnimationHook:
    lfs 1, 0x73e0(10)
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 0, 0x10(1)
    lis 12, g_tphdRollSpeedMultiplier@ha
    lfs 0, g_tphdRollSpeedMultiplier@l(12)
    fmuls 1, 1, 0
    lfd 0, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdRollFrontAnimationHook, .-tphdRollFrontAnimationHook

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

# Side roll uses different native constants but the same two-part treatment.
.global tphdRollSideAnimationHook
.type tphdRollSideAnimationHook, @function
tphdRollSideAnimationHook:
    lfs 1, 0x728c(9)
    stwu 1, -0x20(1)
    stw 12, 8(1)
    stfd 0, 0x10(1)
    lis 12, g_tphdRollSpeedMultiplier@ha
    lfs 0, g_tphdRollSpeedMultiplier@l(12)
    fmuls 1, 1, 0
    lfd 0, 0x10(1)
    lwz 12, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdRollSideAnimationHook, .-tphdRollSideAnimationHook

.global tphdRollSideSpeedHook
.type tphdRollSideSpeedHook, @function
tphdRollSideSpeedHook:
    lfs 12, 0x73ac(12)
    stwu 1, -0x20(1)
    stw 11, 8(1)
    stfd 0, 0x10(1)
    lis 11, g_tphdRollSpeedMultiplier@ha
    lfs 0, g_tphdRollSpeedMultiplier@l(11)
    fmuls 12, 12, 0
    lfd 0, 0x10(1)
    lwz 11, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdRollSideSpeedHook, .-tphdRollSideSpeedHook

# setDaytime loads the native time_change_rate once and reuses it for the
# Fishing Pond/Hena extra increments. Scaling that loaded value retains all of
# those engine rules as well as native event freezes.
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

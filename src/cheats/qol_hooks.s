# Permanent TPHD quality-of-life movement and day-clock thunks.

.section .text, "ax", @progbits
.align 2

.extern g_tphdClimbingSpeedMultiplier
.extern g_tphdClimbHeightMultiplier
.extern g_tphdBlockPushSpeedMultiplier
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

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
    # r0 is overwritten four instructions later at 0x0288D8C0.
    lis 0, g_tphdInfiniteSpinnerTimeEnabled@ha
    lbz 0, g_tphdInfiniteSpinnerTimeEnabled@l(0)
    xori 0, 0, 1
    sub 11, 7, 0
    blr
.size tphdInfiniteSpinnerTimeHook, .-tphdInfiniteSpinnerTimeHook

.global tphdRemoteBombFuseHook
.type tphdRemoteBombFuseHook, @function
tphdRemoteBombFuseHook:
    # Native: r30 = r30 - 1. Enabled: preserve r30.
    # r0 is dead on both continuations and is reloaded before its next use.
    lis 0, g_tphdRemoteBombsEnabled@ha
    lbz 0, g_tphdRemoteBombsEnabled@l(0)
    xori 0, 0, 1
    sub 30, 30, 0
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

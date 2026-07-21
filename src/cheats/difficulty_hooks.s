# Permanent TPHD difficulty/economy thunks.

.section .text, "ax", @progbits
.align 2

.extern g_tphdDamageReceivedMultiplier
.extern g_tphdDamageGivenQuarterScale
.extern g_tphdRupeeMultiplier
.extern g_tphdRupeeMode
.extern g_tphdRupeesOnlyIncreaseEnabled
.extern g_tphdInfiniteEnemyHealthEnabled
.extern g_tphdNoFallDamageEnabled
.extern g_tphdAlwaysFairyRevivalEnabled
.extern g_tphdEasySumoEnabled

.global tphdDamageReceivedHook
.type tphdDamageReceivedHook, @function
tphdDamageReceivedHook:
    # Native: f9 = f9 + f8. Only negative deltas are damage; positive healing
    # remains unchanged. Preserve the integer/FP scratch and all CR fields that
    # the native fadds would have left untouched.
    stwu 1, -0x30(1)
    stw 0, 8(1)
    stw 12, 12(1)
    mfcr 0
    stw 0, 16(1)
    stfd 0, 24(1)
    stfs 8, 32(1)
    lwz 12, 32(1)
    cmpwi 12, 0
    bge .Ldamage_received_native
    lis 12, g_tphdDamageReceivedMultiplier@ha
    lfs 0, g_tphdDamageReceivedMultiplier@l(12)
    fmuls 0, 8, 0
    fadds 9, 9, 0
    b .Ldamage_received_done
.Ldamage_received_native:
    fadds 9, 9, 8
.Ldamage_received_done:
    lfd 0, 24(1)
    lwz 12, 16(1)
    mtcrf 0xff, 12
    lwz 12, 12(1)
    lwz 0, 8(1)
    addi 1, 1, 0x30
    blr
.size tphdDamageReceivedHook, .-tphdDamageReceivedHook

.global tphdDamageGivenHook
.type tphdDamageGivenHook, @function
tphdDamageGivenHook:
    # Native: r8 = r8 - r7, where r8 is the target's signed health at +0x566
    # and r7 is attack power. A quarter-scale integer keeps the UI's 0.25 steps
    # exact; round upward so a nonzero hit never becomes zero damage. Preserve
    # every scratch register and CR field the replaced `subf r8,r7,r8` left
    # untouched. In particular, `addi r0,r0,N` cannot be used as an accumulator:
    # PowerPC treats r0 as a literal-zero base for addi.
    stwu 1, -0x20(1)
    stw 0, 8(1)
    stw 12, 12(1)
    mfcr 0
    stw 0, 16(1)
    lis 12, g_tphdInfiniteEnemyHealthEnabled@ha
    lbz 12, g_tphdInfiniteEnemyHealthEnabled@l(12)
    cmpwi 12, 0
    bne .Ldamage_given_done
    lis 12, g_tphdDamageGivenQuarterScale@ha
    lwz 12, g_tphdDamageGivenQuarterScale@l(12)
    cmpwi 12, 4
    beq .Ldamage_given_native
    mullw 12, 7, 12
    addi 12, 12, 3
    srwi 12, 12, 2
    subf 8, 12, 8
    b .Ldamage_given_done
.Ldamage_given_native:
    subf 8, 7, 8
.Ldamage_given_done:
    lwz 0, 16(1)
    mtcrf 0xff, 0
    lwz 12, 12(1)
    lwz 0, 8(1)
    addi 1, 1, 0x20
    blr
.size tphdDamageGivenHook, .-tphdDamageGivenHook

.global tphdRupeeChangeHook
.type tphdRupeeChangeHook, @function
tphdRupeeChangeHook:
    # Native: lwz r12,0x6280(r31). Apply the selected multiplier according to
    # the original sign, then optionally turn a resulting spend into a gain.
    # r11 is preserved because the replaced load did not touch it.
    stwu 1, -0x10(1)
    stw 11, 8(1)
    lwz 12, 0x6280(31)
    cmpwi 12, 0
    beq .Lrupee_done
    blt .Lrupee_spending
.Lrupee_acquiring:
    lis 11, g_tphdRupeeMode@ha
    lbz 11, g_tphdRupeeMode@l(11)
    cmpwi 11, 0
    beq .Lrupee_multiply
    cmpwi 11, 2
    beq .Lrupee_multiply
    b .Lrupee_after_multiply
.Lrupee_spending:
    lis 11, g_tphdRupeeMode@ha
    lbz 11, g_tphdRupeeMode@l(11)
    cmpwi 11, 1
    beq .Lrupee_multiply
    cmpwi 11, 2
    bne .Lrupee_after_multiply
.Lrupee_multiply:
    lis 11, g_tphdRupeeMultiplier@ha
    lbz 11, g_tphdRupeeMultiplier@l(11)
    mullw 12, 12, 11
.Lrupee_after_multiply:
    lis 11, g_tphdRupeesOnlyIncreaseEnabled@ha
    lbz 11, g_tphdRupeesOnlyIncreaseEnabled@l(11)
    cmpwi 11, 0
    beq .Lrupee_done
    cmpwi 12, 0
    bge .Lrupee_done
    neg 12, 12
.Lrupee_done:
    lwz 11, 8(1)
    addi 1, 1, 0x10
    blr
.size tphdRupeeChangeHook, .-tphdRupeeChangeHook

.global tphdNoFallDamageHook
.type tphdNoFallDamageHook, @function
tphdNoFallDamageHook:
    # checkNoLandDamageSlidePolygon returns true for native safe-landing
    # surfaces. Reusing that result makes all three callers skip the damaging
    # landing procedure and its animation instead of repairing health later.
    lis 12, g_tphdNoFallDamageEnabled@ha
    lbz 12, g_tphdNoFallDamageEnabled@l(12)
    cmpwi 12, 0
    beq .Lno_fall_damage_native
    li 3, 1
    blr
.Lno_fall_damage_native:
    lis 12, 0x0208
    ori 12, 12, 0xBE14
    mtctr 12
    bctr
.size tphdNoFallDamageHook, .-tphdNoFallDamageHook

.global tphdAlwaysFairyRevivalHook
.type tphdAlwaysFairyRevivalHook, @function
tphdAlwaysFairyRevivalHook:
    # This is only the death path's dComIfGs_checkBottle(FAIRY) call. Returning
    # true enters the game's native makeFairy revival; the subsequent bottle
    # replacement simply finds nothing when no fairy bottle exists.
    lis 12, g_tphdAlwaysFairyRevivalEnabled@ha
    lbz 12, g_tphdAlwaysFairyRevivalEnabled@l(12)
    cmpwi 12, 0
    beq .Lalways_fairy_native
    li 3, 1
    blr
.Lalways_fairy_native:
    lis 12, 0x02AA
    ori 12, 12, 0x6460
    mtctr 12
    bctr
.size tphdAlwaysFairyRevivalHook, .-tphdAlwaysFairyRevivalHook

.global tphdEasySumoHook
.type tphdEasySumoHook, @function
tphdEasySumoHook:
    # Runs at daNpcWrestler_c::sumouAI's common epilogue while r31 still holds
    # the wrestler. Action 4 is the game's native wait/open response; forcing
    # that selected response makes Link's normal offensive actions win their
    # exchange without bypassing movement, ring-out, or match events.
    lis 12, g_tphdEasySumoEnabled@ha
    lbz 12, g_tphdEasySumoEnabled@l(12)
    cmpwi 12, 0
    beq .Leasy_sumo_native
    li 0, 4
    stw 0, 0xE74(31)
.Leasy_sumo_native:
    # Replaced native epilogue instruction.
    lwz 31, 0xC(1)
    blr
.size tphdEasySumoHook, .-tphdEasySumoHook

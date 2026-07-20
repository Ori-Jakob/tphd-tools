#include "code_patch.h"

#include <coreinit/cache.h>

#include <stdint.h>

#include "logger.h"

namespace CodePatch {

namespace {

static void flushInstruction(volatile uint32_t* instruction)
{
    DCFlushRange((void*)instruction, sizeof(*instruction));
    ICInvalidateRange((void*)instruction, sizeof(*instruction));
}

static void reportConflict(const char* name, Owner& owner, const Word& word,
                           uint32_t found, const char* operation)
{
    if (owner.conflictReported)
        return;
    Logger::LogWarn(
        "[tphd_tools][patch] %s %s refused: "
        "%08X contains %08X (native %08X, ours %08X)",
        name, operation, (unsigned)word.address, (unsigned)found,
        (unsigned)word.expected, (unsigned)word.replacement);
    owner.conflictReported = true;
}

} // namespace

uint32_t MakeLis(unsigned reg, const void* value)
{
    const uint32_t address = (uint32_t)(uintptr_t)value;
    const uint16_t highAdjusted = (uint16_t)((address + 0x8000u) >> 16);
    return 0x3C000000u | ((reg & 31u) << 21) | highAdjusted;
}

uint32_t MakeLfs(unsigned floatReg, unsigned baseReg, const void* value)
{
    const uint32_t address = (uint32_t)(uintptr_t)value;
    return 0xC0000000u | ((floatReg & 31u) << 21) |
           ((baseReg & 31u) << 16) | (address & 0xFFFFu);
}

bool MakeBranchLink(uint32_t source, const void* target,
                    uint32_t* instruction)
{
    if (!instruction)
        return false;
    const int64_t delta = (int64_t)(uint32_t)(uintptr_t)target -
                          (int64_t)source;
    if ((delta & 3) != 0 || delta < -0x02000000 || delta > 0x01FFFFFC)
        return false;
    *instruction = 0x48000001u | ((uint32_t)delta & 0x03FFFFFCu);
    return true;
}

bool MakeBranch(uint32_t source, const void* target, uint32_t* instruction)
{
    if (!instruction)
        return false;
    const int64_t delta = (int64_t)(uint32_t)(uintptr_t)target -
                          (int64_t)source;
    if ((delta & 3) != 0 || delta < -0x02000000 || delta > 0x01FFFFFC)
        return false;
    *instruction = 0x48000000u | ((uint32_t)delta & 0x03FFFFFCu);
    return true;
}

bool MakeHookBranch(const char* name, Owner& owner, uint32_t source,
                    const void* target, uint32_t* instruction)
{
    if (MakeBranchLink(source, target, instruction))
        return true;
    if (!owner.conflictReported) {
        Logger::LogWarn(
            "[tphd_tools][patch] %s hook out of branch range: %08X -> %08X",
            name, (unsigned)source, (unsigned)(uintptr_t)target);
        owner.conflictReported = true;
    }
    return false;
}

bool MakeHookJump(const char* name, Owner& owner, uint32_t source,
                  const void* target, uint32_t* instruction)
{
    if (MakeBranch(source, target, instruction))
        return true;
    if (!owner.conflictReported) {
        Logger::LogWarn(
            "[tphd_tools][patch] %s jump out of branch range: %08X -> %08X",
            name, (unsigned)source, (unsigned)(uintptr_t)target);
        owner.conflictReported = true;
    }
    return false;
}

bool BuildSingleHook(const char* name, Owner& owner, uint32_t address,
                     uint32_t expected, const void* hook, Word* word)
{
    if (!word)
        return false;
    uint32_t branch = 0;
    if (!MakeHookBranch(name, owner, address, hook, &branch))
        return false;
    *word = { address, expected, branch };
    return true;
}

bool Apply(const char* name, Owner& owner, const Word* words, int count)
{
    if (!words || count <= 0)
        return false;

    // Reconcile against live text instead of trusting only the bookkeeping bit.
    bool needsWrite = false;
    for (int i = 0; i < count; ++i) {
        const volatile uint32_t* p =
            (const volatile uint32_t*)words[i].address;
        const uint32_t found = *p;
        if (found == words[i].expected) {
            needsWrite = true;
        } else if (found != words[i].replacement) {
            reportConflict(name, owner, words[i], found, "install");
            owner.applied = false;
            return false;
        }
    }

    if (needsWrite) {
        for (int i = 0; i < count; ++i) {
            volatile uint32_t* p = (volatile uint32_t*)words[i].address;
            if (*p == words[i].expected) {
                *p = words[i].replacement;
                flushInstruction(p);
            }
        }
        OSMemoryBarrier();
    }

    for (int i = 0; i < count; ++i) {
        const volatile uint32_t* p =
            (const volatile uint32_t*)words[i].address;
        const uint32_t found = *p;
        if (found != words[i].replacement) {
            reportConflict(name, owner, words[i], found, "verify-install");
            owner.applied = false;
            return false;
        }
    }

    owner.applied = true;
    owner.conflictReported = false;
    if (needsWrite)
        Logger::Log("[tphd_tools][patch] %s installed (%d words)",
                    name, count);
    return true;
}

bool Remove(const char* name, Owner& owner, const Word* words, int count)
{
    if (!words || count <= 0)
        return false;

    bool conflict = false;
    bool wrote = false;
    for (int i = 0; i < count; ++i) {
        volatile uint32_t* p = (volatile uint32_t*)words[i].address;
        const uint32_t found = *p;
        if (found == words[i].replacement) {
            *p = words[i].expected;
            flushInstruction(p);
            wrote = true;
        } else if (found != words[i].expected) {
            reportConflict(name, owner, words[i], found, "restore");
            conflict = true;
        }
    }
    if (wrote)
        OSMemoryBarrier();

    bool restored = !conflict;
    for (int i = 0; i < count; ++i) {
        const volatile uint32_t* p =
            (const volatile uint32_t*)words[i].address;
        if (*p != words[i].expected) {
            restored = false;
            if (*p == words[i].replacement)
                reportConflict(name, owner, words[i], *p, "verify-restore");
        }
    }

    owner.applied = !restored;
    if (restored) {
        owner.conflictReported = false;
        if (wrote)
            Logger::Log("[tphd_tools][patch] %s restored (%d words)",
                        name, count);
    }
    return restored;
}

} // namespace CodePatch

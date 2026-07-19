// Reusable, conflict-safe PowerPC code patch helpers.

#pragma once

#include <stdint.h>

namespace CodePatch {

struct Word {
    uint32_t address;
    uint32_t expected;
    uint32_t replacement;
};

// Tracks ownership only for diagnostics. Apply/Remove always reconcile against
// live text, so losing this state cannot orphan an installed patch.
struct Owner {
    bool applied = false;
    bool conflictReported = false;
};

// Encode common PowerPC instructions that load a value through its address.
uint32_t MakeLis(unsigned reg, const void* value);
uint32_t MakeLfs(unsigned floatReg, unsigned baseReg, const void* value);

// Encode a relative PPC `bl`. Returns false when the target is unaligned or
// outside the signed 26-bit branch range.
bool MakeBranchLink(uint32_t source, const void* target,
                    uint32_t* instruction);

// MakeBranchLink with one-time, owner-scoped range diagnostics.
bool MakeHookBranch(const char* name, Owner& owner, uint32_t source,
                    const void* target, uint32_t* instruction);

// Build the common one-word form: replace a guarded native instruction with a
// relative call to `hook`.
bool BuildSingleHook(const char* name, Owner& owner, uint32_t address,
                     uint32_t expected, const void* hook, Word* word);

// Install/restore a guarded patch set. Live words must be either the declared
// native instruction or this exact replacement; unrelated patches are never
// overwritten. Both operations read the full set back before reporting success.
bool Apply(const char* name, Owner& owner, const Word* words, int count);
bool Remove(const char* name, Owner& owner, const Word* words, int count);

} // namespace CodePatch

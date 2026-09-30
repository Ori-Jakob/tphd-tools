#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Hotkeys {
enum Id {
    HOTKEY_MENU = 0,
    HOTKEY_QUICK_ACCESS,
    HOTKEY_MOON_JUMP,
    HOTKEY_QUICK_TRANSFORM,
    HOTKEY_DETONATE_BOMBS,
    HOTKEY_COUNT
};

enum Scope {
    SCOPE_GLOBAL = 0,
};

uint32_t    Get(Id id);
void        Set(Id id, uint32_t buttons);
uint32_t    Default(Id id);
const char* Name(Id id);
const char* Group(Id id);
const char* ConfigKey(Id id);
bool        IsHold(Id id);
Scope       ScopeOf(Id id);

bool        IsInclusive(Id id);
void        SetInclusive(Id id, bool inclusive);
bool        DefaultInclusive(Id id);
bool        Matches(Id id, uint32_t held);

bool        Conflicts(Id target, uint32_t buttons, Id other);
const char* Validate(Id target, uint32_t buttons);

bool OverlayComboHeld(uint32_t held);

bool Pressed(Id id);
bool Held(Id id);

void SuppressUntilReleased(uint32_t buttons);

void Tick();

void ResetToDefaults();
void OnApplicationStart();
}

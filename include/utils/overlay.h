// overlay.h -- shared state for the TPHD ImGui overlay.
//
// Both platform front ends use these shared modules:
//   renderer.* -- ImGui + GX2 setup and drawing
//   input.*    -- VPAD/KPAD filtering, cross-device menu input, hotkey
//   menu.*     -- the ImGui UI (load toast, settings, future cheats/tools)
// This header holds the cross-module state (settings + menu visibility).
#pragma once

#include <stdint.h>

namespace ov {

// Which device(s) to neutralize while the menu is open.
//   GamePad = VPADRead, Pro = KPADReadEx (Pro/Classic/Wiimote).
enum BlockMode { BLOCK_ALL = 0, BLOCK_GAMEPAD = 1, BLOCK_PRO = 2 };

// Device-neutral button bits. Both the Wii U GamePad (VPAD) and the
// Pro/Classic controllers (KPAD) map into these, so menu navigation and the
// configurable open/close hotkey work the same regardless of controller.
enum MenuButton : uint32_t {
    MB_UP     = 1u << 0,  MB_DOWN   = 1u << 1,  MB_LEFT   = 1u << 2,  MB_RIGHT  = 1u << 3,
    MB_A      = 1u << 4,  MB_B      = 1u << 5,  MB_X      = 1u << 6,  MB_Y      = 1u << 7,
    MB_L      = 1u << 8,  MB_R      = 1u << 9,  MB_ZL     = 1u << 10, MB_ZR     = 1u << 11,
    MB_PLUS   = 1u << 12, MB_MINUS  = 1u << 13, MB_LSTICK = 1u << 14, MB_RSTICK = 1u << 15,
};

// Map device-neutral MenuButton bits into the Pro-controller codes used by the
// game's own input path (dCam_normalizeButtons / PRO_BTN_*). Fly Cam and Moon
// Jump read that layout rather than the overlay's MenuButton set.
inline uint32_t MenuButtonsToPro(uint32_t m)
{
    uint32_t p = 0;
    if (m & MB_A)      p |= 0x00000010u;  // PRO_BTN_A
    if (m & MB_B)      p |= 0x00000040u;  // PRO_BTN_B
    if (m & MB_X)      p |= 0x00000008u;  // PRO_BTN_X
    if (m & MB_Y)      p |= 0x00000020u;  // PRO_BTN_Y
    if (m & MB_PLUS)   p |= 0x00000400u;  // +
    if (m & MB_MINUS)  p |= 0x00001000u;  // -
    if (m & MB_LSTICK) p |= 0x00020000u;  // PRO_BTN_L3
    if (m & MB_RSTICK) p |= 0x00010000u;  // PRO_BTN_R3
    if (m & MB_L)      p |= 0x00002000u;  // PRO_BTN_L
    if (m & MB_ZL)     p |= 0x00000080u;  // PRO_BTN_ZL
    if (m & MB_R)      p |= 0x00000200u;  // PRO_BTN_R
    if (m & MB_ZR)     p |= 0x00000004u;  // PRO_BTN_ZR
    if (m & MB_UP)     p |= 0x00000001u;  // D-Up
    if (m & MB_DOWN)   p |= 0x00004000u;  // D-Down
    if (m & MB_LEFT)   p |= 0x00000002u;  // D-Left
    if (m & MB_RIGHT)  p |= 0x00008000u;  // D-Right
    return p;
}

// Which controller to finalize when a save/debug-save load happens from the title
// screen (before the boot controller-select). AUTO prefers a connected Pro.
enum ControllerPref { CTRL_AUTO = 0, CTRL_GAMEPAD = 1, CTRL_PRO = 2 };

// Color buffer(s) that receive the overlay. The UI is laid out in TV
// coordinates and scaled by the renderer when drawn to the GamePad buffer.
enum RenderTarget { RENDER_TV = 0, RENDER_GAMEPAD = 1, RENDER_BOTH = 2 };

struct Settings {
    bool     blockEnabled = true;          // block game input while the menu is open
    int      blockMode    = BLOCK_ALL;     // OverlayBlockMode
    uint32_t hotkey       = MB_ZR | MB_DOWN;  // open/close combo (MenuButton bits)
    bool     freezeOnMenu = false;         // halt the game (freeze bit) while the menu is open
    bool     actionNotifications = true;   // show short toasts for practice actions
    bool     gameResetHotkey = true;       // game-reset combo writes the engine reset flag
    // Rebindable feature hotkeys (MenuButton bits). Defaults match the original
    // hard-coded combos (Start+X+B, ZL+ZR+Start, ZR+Y, ZL+ZR+L3+R3,
    // ZR+A, ZR+D-Left, ZR+D-Right, ZR+L3).
    uint32_t gameResetCombo        = MB_PLUS | MB_X | MB_B;
    uint32_t saveStateReloadCombo  = MB_ZL | MB_ZR | MB_PLUS;
    uint32_t quickTransformCombo   = MB_ZR | MB_Y;
    uint32_t flyCamCombo           = MB_ZL | MB_ZR | MB_LSTICK | MB_RSTICK;
    uint32_t moonJumpCombo         = MB_ZR | MB_A;
    uint32_t saveCoordinatesCombo  = MB_ZR | MB_LEFT;
    uint32_t loadCoordinatesCombo  = MB_ZR | MB_RIGHT;
    uint32_t remoteBombsCombo      = MB_ZR | MB_LSTICK;
    int      controllerPref  = CTRL_AUTO;  // controller to select for title-screen loads
    // Draw on both screens by default: a console player may only be looking at
    // the GamePad, and with a TV-only default they'd get the input block with
    // no visible menu and no way to discover the setting that fixes it.
    int      renderTarget = RENDER_BOTH;   // TV, GamePad, or both color buffers
    float    overlayOpacity  = 1.0f;       // shared background opacity for passive HUD windows
    bool     boldLetters     = true;       // black outline on passive HUD text
    float    windowAdjustDeadzone = 0.17f; // ZL+stick window move/resize stick deadzone (magnitude)
};

extern Settings g_settings;
extern bool     g_menuVisible;             // false on load; user opens with the hotkey

} // namespace ov

struct GX2ColorBuffer;

namespace Overlay {

// The per-frame overlay body: consume input, run the hotkey/tools/cheats, and
// build one ImGui frame from the game's TV presentation. Cemu supplies both
// color buffers here, while Aroma supplies the TV buffer and draws the GamePad
// copy later through PresentGamePad. Safe to call with a null TV buffer.
void Present(GX2ColorBuffer* tv, GX2ColorBuffer* gamePad);

// Draw the most recently completed ImGui frame to a separately presented
// GamePad buffer. Used by Aroma's DRC copy hook.
void PresentGamePad(GX2ColorBuffer* gamePad);

// True when PresentGamePad would actually draw (renderer up, a completed frame
// is available, and the configured target includes the GamePad). Lets the DRC
// hook skip the GX2 context switch + flush entirely on frames with nothing to
// draw.
bool WantsGamePadDraw();

// Aroma lifecycle helpers. They are no-ops until the renderer has initialized.
// Application transitions preserve ImGui settings but discard stale GX2 device
// objects and completed draw data.
void OnApplicationStart();
void OnApplicationEnd();

} // namespace Overlay

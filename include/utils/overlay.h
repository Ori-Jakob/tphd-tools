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
    bool     gameResetHotkey = true;       // Start+X+B writes the engine reset flag
    int      controllerPref  = CTRL_AUTO;  // controller to select for title-screen loads
    int      renderTarget = RENDER_TV;     // TV, GamePad, or both color buffers
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

} // namespace Overlay

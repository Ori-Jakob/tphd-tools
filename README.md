# TPHD Tools

TPHD Tools is an experimental in-game tool suite for *The Legend of Zelda:
Twilight Princess HD*. It uses Dear ImGui and the Wii U's native GX2 renderer to
draw an overlay over the game's TV output, GamePad output, or both.

It builds from one shared codebase with two platform-specific front ends:

| Platform | Output | Integration |
| --- | --- | --- |
| Cemu | `tphd_tools.rpl` | Loaded by a Cemu graphics-pack codecave |
| Wii U with Aroma | `tphd_tools.wps` | Loaded as a Wii U Plugin System plugin |

## Current status

The overlay, input hooks, configuration storage, tools, cheats, Save Loader, and
both build targets are implemented. Boss Practice and the autosplitter are
compile-time experimental features and are excluded from standard builds.

Game bindings and hook addresses currently target TPHD v81. The Cemu graphics
pack declares support for these title IDs:

- Japan: `000500001019C800`
- United States: `000500001019E500`
- Europe: `000500001019E600`

Other game revisions are not supported unless their addresses and graphics-pack
module match are verified.

## Features

The in-game top bar is organized as **Practice**, **Gameplay**,
**Camera & HUD**, optional **Experimental**, and **Settings**.

### Practice

- Save Loader with named states, folders, position restoration, and post-load
  actor or flag commands
- One-slot Save/Load Coordinates with same-stage validation, in-place native
  room streaming, and exact Link facing plus camera restoration
- Loader for the game's shipped debug saves and personal save images
- Warps by preset or stage, room, layer, and spawn
- Link position and facing editor

### Gameplay

- Player cheats including Moon Jump, Quick Transform, Invincibility, and
  infinite health or air
- Resource and item-rule cheats including infinite ammo or rupees and
  unrestricted item use
- Inventory editor for player status, items, equipment, collection data,
  dungeon/area data, warp portals, and event flags
- Clawshot, Spinner, bomb, and Iron Boots modifiers
- Combat, survival, and economy difficulty controls
- Human/Wolf movement, climb-height, and world-clock modifiers

### Camera & HUD

- Fly Cam with independent movement, rotation, speed control, and optional
  teleport-to-camera exit
- Modern third-person camera with sensitivity, FOV, Epona, and inversion settings
- GamePad and Pro Controller input viewer
- Game information HUD with position, angle, stage, room, time, and optional
  system date and time

### Experimental

These features require a build made with `EXPERIMENTAL=1` and appear under the
in-game **Experimental** menu:

- Boss Practice launcher with native opening/direct-fight entry modes and an
  isolated current, recommended, or custom combat loadout (Deku Toad is the
  first verified encounter)
- Native timer and autosplitter driven by Wii U split JSON files

### Settings and input

- GamePad, Pro Controller, and Classic Controller input
- GamePad touchscreen and Cemu mouse input
- Optional game-input blocking while the menu is open
- Optional game freeze while the menu is open
- Optional action notifications for save-state loads, game resets, and
  coordinate saves/loads (enabled by default)
- Selectable overlay output: TV, GamePad, or both
- Rebindable menu hotkey and configurable controller preference
- Persistent settings, window positions, tool state, and cheat state
- On-screen software keyboard for text fields

## Controls

The menu is hidden on startup. The default open and close hotkey is
`ZR + D-Pad Down`.

| Input | Action |
| --- | --- |
| D-Pad | Navigate |
| A | Select or activate |
| B | Cancel or go back |
| Y | Cycle focus between the menu bar and open tool windows |
| L / R | Change tabs in the focused window |
| Hold ZL + right stick | Move the focused window |
| Hold ZL + left stick | Resize the focused window |
| Plus + X + B | Reset the game when the reset hotkey is enabled |
| ZR + D-Pad Left | Save Link and camera coordinates |
| ZR + D-Pad Right | Load saved coordinates in the same stage |

Fly Cam is enabled from **Camera & HUD** and toggled with
`ZL + ZR + L3 + R3`. While flying, the left stick moves, the right stick rotates,
`ZL` and `ZR` move vertically, and `L` and `R` change speed. Press `L3` to return
the camera to Link or `R3` to move Link to the camera.

Save/Load Coordinates is enabled from **Practice > Save & Restore**. Its saved
slot includes the stage, room, spawn, layer, Link position/facing, and camera target/eye. A
  load is rejected when the current stage differs. A different saved room is
  streamed through the engine's native room-table path without a stage reload;
  Link is pinned at the saved position with zero momentum until the destination
  collision is ready. The tool then finalizes the room/spawn/layer and transforms,
  puts Link into the normal idle action with zero momentum, and stamps the live
  room information once more on the next frame. Both hotkeys are configurable
  under **Settings > Rebind Hotkeys**, and the saved slot plus Fly Cam and
  Save/Load Coordinates enabled states persist in `config.json`.

## Requirements

- devkitPro with the Wii U development packages and WUT
- GNU Make
- WiiUPluginSystem SDK for the Aroma target
- `libmappedmemory` for the Aroma target (and MemoryMappingModule on the console)
- The `external/imgui` Git submodule

Initialize the submodule after cloning:

```sh
git submodule update --init --recursive
```

`DEVKITPRO` must be set in the build environment. The Makefiles include the WUT
and WUPS rules from the devkitPro installation.

## Build

Run builds from the project directory:

```sh
# Cemu release and debug RPLs
make
make debug

# Aroma release and debug plugins
make -f Makefile.aroma
make -f Makefile.aroma debug

# Experimental Cemu release and debug RPLs
make EXPERIMENTAL=1
make EXPERIMENTAL=1 debug

# Experimental Aroma release and debug plugins
make -f Makefile.aroma EXPERIMENTAL=1
make -f Makefile.aroma EXPERIMENTAL=1 debug
```

The outputs are:

```text
tphd_tools.rpl
tphd_tools_debug.rpl
tphd_tools.wps
tphd_tools_debug.wps
tphd_tools_experimental.rpl
tphd_tools_experimental_debug.rpl
tphd_tools_experimental.wps
tphd_tools_experimental_debug.wps
```

Use `make clean` or `make -f Makefile.aroma clean` to remove the corresponding
build output. A version string can be embedded with `VERSION=<version>`.
Without one, both Makefiles calculate a deterministic `CRC32-XXXXXXXX` from the
shared build inputs. The displayed/logged version always includes `DEBUG` or
`RELEASE`, and experimental builds also include `EXPERIMENTAL`.

`run_build.ps1` applies the same automatic versioning rule. Pass `-e` or
`--experimental` to opt into the experimental feature set, and combine it with
`-d` or `--debug` when needed. The script installs the selected experimental
artifact under the normal module filename expected by the Cemu graphics pack.

## Install on Cemu

1. Build `tphd_tools.rpl`.
2. Copy it into the game's `code` directory next to `Zelda.rpx`.
3. Copy `graphicspack/release/rules.txt` and `patch_overlay.asm` into one
   graphics-pack directory under Cemu's `graphicPacks` directory.
4. Enable **TPHD Tools** in Cemu's Graphics Packs window and start the game.

The RPL filename must remain `tphd_tools.rpl`; the graphics pack loads that
module name directly.

For a debug build, use `tphd_tools_debug.rpl` together with the files from
`graphicspack/debug`. Do not mix release and debug artifacts.

For a manual experimental install, rename `tphd_tools_experimental.rpl` to
`tphd_tools.rpl`, or `tphd_tools_experimental_debug.rpl` to
`tphd_tools_debug.rpl`, when copying it into the game directory.

`run_build.ps1` is a developer convenience script with local Cemu and game
paths. Edit its destination paths before using it on another system.

## Install on Aroma

1. Build `tphd_tools.wps`.
2. Copy it to:

   ```text
   sd:/wiiu/environments/aroma/plugins/tphd_tools.wps
   ```

3. Boot Aroma and launch TPHD.

The Aroma build does not use the Cemu RPL or graphics pack. Its WUPS hooks are
limited to game processes and only activate for the supported TPHD title IDs.
Experimental Aroma artifacts may likewise be renamed to `tphd_tools.wps` when
installed.

## SD card data

Both front ends store data under `sd:/tphd_tools`:

| Path | Contents |
| --- | --- |
| `config.json` | Menu, tool, cheat, and window settings |
| `log.txt` | Current log; the previous log is rotated to `log.txt.old` |
| `savestates/` | Save Loader state files and optional subfolders |
| `saves/` | Personal save images used by the Debug Save Loader |
| `splits/` | Autosplitter JSON files |
| `split_times/` | Per-route personal-best history |
| `split_golds/` | Per-route best segments |

## Implementation

The Cemu graphics pack installs present, GamePad scan-out, `VPADRead`, and
`KPADReadEx` hooks. All hooks dispatch through the RPL's single exported function,
`TPHDToolsEntry(reason, a, b)`.

The Aroma plugin replaces `GX2CopyColorBufferToScanBuffer`, the GX2
initialization/context setters, `VPADRead`, and `KPADReadEx` for game processes.
It draws immediately before the completed TV color buffer is copied to the scan
buffer, under a private mapped GX2 context, then restores the game's context
before chaining the real copy.

Both front ends call the same overlay, input, storage, tool, cheat, and game
binding code:

```text
src/cemu/          Cemu RPL entry point and storage backend
src/aroma/         Aroma WUPS entry point and storage backend
src/utils/         Renderer, input, menu, configuration, and logging
src/tools/         Fly Cam, warps, Save Loader, autosplitter, and HUD tools
src/cheats/        Runtime cheats and inventory editor
src/debug/         Game information and debug-save tools
include/game/      Reverse-engineered TPHD structures and function bindings
graphicspack/      Release and debug Cemu hook packs
external/imgui/    Dear ImGui Wii U branch
external/cjson/    cJSON source used for configuration and split files
```

## Known limitations

- ImGui timing is fixed at TPHD's 30 Hz presentation rate.
- The Aroma plugin preserves its ImGui settings across applications, but drops
  and recreates all GPU-facing GX2 objects for each TPHD launch.
- Several tools and cheats write directly to game state through fixed TPHD v81
  addresses. Unsupported revisions can crash or corrupt runtime state.

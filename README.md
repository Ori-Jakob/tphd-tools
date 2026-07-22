# TPHD Tools

TPHD Tools is an experimental in-game practice, debugging, and gameplay tool suite for *The
Legend of Zelda: Twilight Princess HD*. It renders a Dear ImGui overlay through the Wii U GX2
API and shares the same tools between Cemu and a Wii U running Aroma.

| Platform | Artifact | Integration |
| --- | --- | --- |
| Cemu | `tphd_tools.rpl` | A graphics-pack codecave loads the RPL and forwards game hooks |
| Wii U with Aroma | `tphd_tools.wps` | A Wii U Plugin System plugin installs its own runtime hooks |

Standard builds include the practice tools, gameplay editors and modifiers, cameras, HUDs,
and settings described below. Boss Practice, the autosplitter, and the block push/pull and
chain-pull speed modifiers are available only when `EXPERIMENTAL=1` is set.


## Compatibility

The current title gates and Cemu graphics-pack rules accept these Wii U title IDs:

| Region | Product code | Title ID |
| --- | --- | --- |
| Japan | AZAJ | `000500001019C800` |
| United States | AZAE | `000500001019E500` |
| Europe | AZAP | `000500001019E600` |

The bundled graphics pack targets RPX module hashes `0x1A03E108`. A matching
title ID is not, by itself, proof that an unverified executable revision is safe. Many game
structures, functions, patch sites, and global variables use fixed v81 addresses.

## Capabilities

The standard menu contains **Practice**, **Gameplay**, **Camera & HUD**, and **Settings**.
Experimental builds add an **Experimental** menu.

### Practice

| Tool | What it does |
| --- | --- |
| Save Loader | Captures named state files, organizes them into folders, and restores persistent save data, scene information, Link, camera, and Epona state. A state can optionally run ordered actor-spawn, actor-delete, or current-area flag commands after the destination scene is stable. |
| Save/Load Coordinates | Keeps one persistent position slot containing the stage, room, layer, spawn, Link position and facing, and camera target and eye. Loads are limited to the same stage; cross-room loads use the game's room-streaming path and wait for destination collision before releasing Link. |
| Debug Save Loader | Opens the debug, QA, and cutscene saves shipped with the game and personal raw save images stored on the SD card. Personal images are validated before the game deserializer receives them. |
| Warps | Offers 103 curated destinations grouped by region, plus manual stage, room, spawn, and layer entry. |
| Link Position Editor | Reads or writes Link's X, Y, Z, and facing values. |

Save Loader files are tool-managed game-state captures, not emulator snapshots. Loading a
state may perform a full scene reload before restoring the remaining data. The optional
command editor can select actors by process ID, engine name, or friendly name; spawn multiple
actors with explicit parameters and transforms; remove matching actors; and set chest,
switch, item, dungeon-item, or key-count values after loading.

### Gameplay

| Group | Capabilities |
| --- | --- |
| Player | Moon Jump, Quick Transform, Invincibility, Infinite Hearts, and Infinite Air. |
| Items and resources | Infinite Ammo, Infinite Rupees, and Unrestricted Items in areas where the game normally blocks them. |
| Inventory Editor | Edits health, rupees, lantern oil, wallet size, form, 24 inventory slots, capacities and counts, equipped and owned gear, obtained-item flags, Fused Shadows, Mirror Shards, Poe Souls, Hidden Skills, and Golden Bugs. |
| World Editor | Edits 786 searchable story event flags; current-area chest, switch, item, dungeon-item, and stage-event flags; dungeon key count; and the persistent state of 15 warp portals. It is opened from **Movement & World > World & Time**. |
| Equipment modifiers | Super Clawshot, Infinite Spinner Time, adjustable Spinner speed, Remote Bombs, No Bomb Limit, Fast Iron Boots, and Always Great Spin Attack. |
| Difficulty | Scales damage received and dealt, gives enemies infinite health, simplifies sumo, changes rupee gains or losses, removes fall damage, and controls fairy revival behavior. |
| Movement and world | Adjusts climbing, climb height, crawling, roll speed and distance, time of day, and clock progression. Experimental builds also include block push/pull and chain-pull speed modifiers. Setting clock progression to zero freezes the normal world clock. |

The Inventory Editor and World Editor modify the live save structures. They do not validate
whether combinations of equipment, dungeon state, and story flags are logically reachable.
Changing event flags can skip progression checks or leave a cutscene in an invalid state.

### Camera and HUD

| Tool | What it does |
| --- | --- |
| Fly Cam | Detaches the camera for free movement and rotation, with adjustable speed and separate exits for returning to Link or moving Link to the camera. |
| Modern Camera | Adds right-stick chase-camera control with horizontal and vertical sensitivity, field-of-view scaling, optional global FOV, Epona support, axis inversion, and an advanced diagnostic view. |
| Game Info HUD | Shows session time, game time of day, facing and look angles, speed, XYZ position, and Link's current action. System date and time are optional. |
| Input Viewer | Displays the active controller's buttons and analog sticks in a movable, resizable passive HUD. |

Passive HUDs remain visible while the main menu is closed. Interactive editor windows are
drawn only while the menu is open.

### Settings and input

Settings cover:

- menu input blocking for all controllers, GamePad only, or Pro Controller only;
- optional game freeze while the menu is open;
- automatic, GamePad, or Pro Controller preference when a save is loaded from the title
  screen;
- TV, GamePad, or simultaneous overlay output;
- overlay opacity, bold text, and window movement and resize deadzones;
- notifications for actions such as state loads, resets, and coordinate operations; and
- rebinding for every tool hotkey, with conflict detection before an existing binding is
  replaced.

The input layer supports the Wii U GamePad and KPAD devices, including Pro and Classic
Controllers. GamePad touch input is supported, and Cemu mouse input is exposed to the UI as
touch. Text fields can open the Wii U software keyboard.

### Experimental tools

Experimental tools are absent from standard binaries, not merely hidden in the menu.

| Tool | Current scope |
| --- | --- |
| Boss Practice | Launches the Lakebed Temple Deku Toad encounter at its opening cinematic or directly into the fight. The launcher can retain the current loadout or construct a recommended or custom temporary loadout. |
| Auto Splitter | Loads route JSON files from `tphd_tools/splits`, displays an in-game timer and split list, evaluates memory conditions, tracks personal-best history and gold segments, and can remove gameplay load time. |
| Block and chain speed modifiers | Scales Link's animation and the associated object movement for block push/pull and chain-pull actions. These modifiers remain experimental while their actor-specific timing paths are stabilized. |

## Controls

The menu is hidden when the game starts. Its default toggle is `ZR + D-Pad Down`.

### Menu navigation

| Input | Action |
| --- | --- |
| D-Pad | Navigate menus and controls |
| A | Select or activate |
| B | Cancel or go back |
| Y | Cycle focus between the menu bar and open tool windows |
| L / R | Change tabs in the focused window |
| Hold ZL + right stick | Move the focused window |
| Hold ZL + left stick | Resize the focused window |

### Default hotkeys

Every hotkey can be changed under **Settings > Rebind Hotkeys**. Tool-specific shortcuts act
only while their corresponding tool or option is enabled.

| Hotkey | Action |
| --- | --- |
| `ZR + D-Pad Down` | Open or close the menu |
| `Plus + X + B` | Reset the game |
| `ZL + ZR + Plus` | Reload the last Save Loader state |
| `ZR + Y` | Quick Transform |
| `ZL + ZR + L3 + R3` | Enter or leave Fly Cam |
| `ZR + A` | Moon Jump |
| `ZR + D-Pad Left` | Save the coordinate slot |
| `ZR + D-Pad Right` | Load the coordinate slot |
| `ZR + L3` | Detonate player bombs when Remote Bombs is enabled |

### Fly Cam

| Input | Action |
| --- | --- |
| Left stick | Move relative to the camera |
| Right stick | Rotate yaw and pitch |
| ZR / ZL | Move up or down |
| R / L | Double or halve movement speed |
| Hold A / B | Use maximum or minimum speed |
| L3 | Exit and return the camera to Link |
| R3 | Exit and move Link to the camera |

## Building from source

### Requirements

All builds require:

- devkitPro with devkitPPC and WUT;
- GNU Make;
- the `DEVKITPRO` environment variable; and
- the `external/imgui` Git submodule.

The Aroma target additionally requires the WiiUPluginSystem SDK and WUMS
`libmappedmemory`. A console install also needs MemoryMappingModule at runtime.

Initialize the repository submodules after cloning:

```sh
git submodule update --init --recursive
```

When `VERSION` is omitted, the Makefiles call `powershell.exe` to calculate a deterministic
source CRC32. On a host without Windows PowerShell, pass an explicit version to `make`, such
as `VERSION=dev-local`.

### Build matrix

Run commands from the repository root:

| Target | Command | Output |
| --- | --- | --- |
| Cemu release | `make` | `tphd_tools.rpl` |
| Cemu debug | `make debug` | `tphd_tools_debug.rpl` |
| Cemu experimental release | `make EXPERIMENTAL=1` | `tphd_tools_experimental.rpl` |
| Cemu experimental debug | `make EXPERIMENTAL=1 debug` | `tphd_tools_experimental_debug.rpl` |
| Aroma release | `make -f Makefile.aroma` | `tphd_tools.wps` |
| Aroma debug | `make -f Makefile.aroma debug` | `tphd_tools_debug.wps` |
| Aroma experimental release | `make -f Makefile.aroma EXPERIMENTAL=1` | `tphd_tools_experimental.wps` |
| Aroma experimental debug | `make -f Makefile.aroma EXPERIMENTAL=1 debug` | `tphd_tools_experimental_debug.wps` |

Release builds use `-O2`; debug builds use `-O0` and define `TPHD_TOOLS_DEBUG`. Clean the Cemu
artifacts with `make clean` and the Aroma artifacts with `make -f Makefile.aroma clean`.

An explicit version can be embedded in either target:

```sh
make VERSION=0.3.0
make -f Makefile.aroma VERSION=0.3.0
```

Without `VERSION`, `scripts/source_crc32.ps1` hashes the shared build inputs and emits a
`CRC32-XXXXXXXX` identifier. The version shown in the menu and log also states whether the
binary is `DEBUG` or `RELEASE` and whether it includes `EXPERIMENTAL` tools.

### Cemu build-and-install script

`run_build.ps1` forces a Cemu RPL rebuild and, unless told otherwise, copies the result and
matching graphics-pack files to its configured local destinations.

| Option | Effect |
| --- | --- |
| `-d`, `--debug` | Build and install the debug variant |
| `-e`, `--experimental` | Include experimental tools |
| `-ni`, `--no-install` | Build only; skip destination validation and all installation copies |
| `-v <value>`, `--version <value>` | Use an explicit embedded version |
| `--version=<value>` | Use an explicit embedded version |

Examples:

```powershell
# Standard release build without installation
.\run_build.ps1 -ni

# Experimental debug build and installation
.\run_build.ps1 -e -d
```

The script contains developer-specific game `code` directories in `$destinations`; edit them
before installing on another system. Its graphics-pack destination is derived from
`$env:APPDATA\Cemu\graphicPacks`.

## Installing on Cemu

1. Build the required RPL variant.
2. Copy the RPL into the game's `code` directory beside `Zelda.rpx`.
3. Create a graphics-pack directory, for example
   `Cemu/graphicPacks/TwilightPrincessHD/tphd_tools`.
4. Copy `graphicspack/release/rules.txt` and `graphicspack/release/patch_overlay.asm` into that
   directory.
5. Enable **TPHD Tools** in Cemu's Graphics Packs window and launch the game.

The release pack loads `tphd_tools.rpl` by that exact name. For a debug install, copy
`tphd_tools_debug.rpl` and use both files from `graphicspack/debug`. Do not combine a release
RPL with the debug pack or a debug RPL with the release pack.

Experimental output names identify the build artifact but are not the module names expected
by the packs. Rename `tphd_tools_experimental.rpl` to `tphd_tools.rpl`, or
`tphd_tools_experimental_debug.rpl` to `tphd_tools_debug.rpl`, when installing manually.
`run_build.ps1` performs this name mapping automatically.

## Installing on Aroma

1. Build the required WPS variant.
2. Copy it to `sd:/wiiu/environments/aroma/plugins/tphd_tools.wps`.
3. Boot the Aroma environment and launch TPHD.

The Aroma build does not use the Cemu graphics pack or RPL. Its function replacements are
limited to game processes, and initialization is gated to the three supported title IDs.
Rename an experimental WPS artifact to `tphd_tools.wps` when installing it manually.

## Persistent data

Both frontends use the logical SD-card directory `sd:/tphd_tools`. Cemu binds its configured
emulated SD card at `/vol/external01`; Aroma accesses the physical SD card through the WUT
devoptab. TPHD Tools creates its own directories when storage becomes available.

| Path | Contents |
| --- | --- |
| `config.json` | Menu behavior, controller selection, hotkeys, tools, cheats, modifiers, coordinate slot, HUD layout, and ImGui window state |
| `log.txt` | Current asynchronous log; the previous session is rotated to `log.txt.old` |
| `savestates/` | Save Loader `.bin` files and immediate subfolders |
| `saves/` | Personal `.dat` images for the Debug Save Loader |
| `splits/` | Experimental autosplitter route JSON files |
| `split_times/` | Per-route personal-best history |
| `split_golds/` | Per-route best segments |

Configuration writes are debounced and performed by a background worker. Save Loader and
debug-save directory I/O also run outside the presentation hook so ordinary SD-card access
does not stall every rendered frame.

## Architecture

The project has one shared tool layer and two small platform frontends:

- `src/cemu/` provides the RPL entry point, runtime setup, allocator, and Cemu storage backend.
  The graphics pack hooks presentation, GamePad scan-out, controller reads, and scene/room
  loading, then dispatches them through the single `TPHDToolsEntry(reason, a, b)` export.
- `src/aroma/` provides WUPS lifecycle handling, title gating, input and GX2 function
  replacements, mapped-memory rendering resources, and the Aroma storage backend. It draws
  with a private GX2 context and restores the game's context before continuing the real
  scan-out call.
- `src/utils/` contains the overlay lifecycle, GX2 renderer, menu, input normalization,
  configuration, logging, notifications, software keyboard, and guarded code-patch helper.
- `src/tools/`, `src/cheats/`, and `src/debug/` contain the shared user-facing functionality.
- `include/game/` contains reverse-engineered TPHD structures, functions, globals, and data
  tables used by the tools.
- `graphicspack/release/` and `graphicspack/debug/` contain the matching Cemu hook packs.
- `external/imgui/` is the Wii U Dear ImGui branch; `external/cjson/` is the vendored JSON
  implementation.

Runtime instruction patches are guarded: the patch helper accepts the original game
instruction or its own replacement and logs a conflict instead of overwriting an unrelated
patch. This reduces collisions with other modifications at shared patch sites, but it does
not make arbitrary graphics-pack combinations compatible.

The actor selector used by Save Loader embeds `data/procs.bin`. When
`actor_data/actor_table.tsv` changes, regenerate it with:

```sh
python tools/gen_procs_bin.py
```


## License

This repository does not currently declare a project-level license. Unless a license is
added, no permission to copy, modify, or redistribute the project's original source should
be assumed. Third-party code under `external/` remains subject to its own license terms.

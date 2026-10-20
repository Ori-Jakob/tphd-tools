# TPHD Tools

An in-game menu for The Legend of Zelda: Twilight Princess HD with practice tools, cheats, camera options and HUDs. It runs on a Wii U with Aroma and on Cemu, from the same `tphd_tools.rpl`.

Supported games (all v81, they share one executable):

| Region | Title ID |
| --- | --- |
| USA | `000500001019E500` |
| Europe | `000500001019E600` |
| Japan | `000500001019C800` |

## Download

Grab the latest `TPHD-Tools-<version>.zip` from the Releases page. The zip has two folders:

```
Cemu/TPHD-Tools/          the Cemu graphics pack
Wii U/wiiu/               files for the root of your SD card
```

There is also an `-experimental` zip with a few extra options that are still rough (see [Experimental](#experimental)).

## Installing on a Wii U

You need a console running [Aroma](https://aroma.foryour.cafe/).

1. Copy everything inside `Wii U/` to the root of your SD card. You should end up with:
   ```
   sd:/wiiu/environments/aroma/plugins/rpl_loader.wps
   sd:/wiiu/rpl-loader/universal/tphd_tools.rpl
   ```
2. Reboot the console. Aroma only picks up new plugins at boot.
3. Start Twilight Princess HD. A short message shows the menu button once the tools are loaded.

To update, replace `tphd_tools.rpl`. That file is read every time the game starts, so a new version only needs a game restart. Replacing `rpl_loader.wps` needs a console reboot.

If a bad build ever stops the game from starting, hold L + R + ZL + ZR while launching it. The loader then skips the tools for that launch.

## Installing on Cemu

1. Open Cemu's graphic packs folder (Options > Graphic packs, or `%APPDATA%/Cemu/graphicPacks` on Windows).
2. Copy the `Cemu/TPHD-Tools` folder into it.
3. In Cemu, open the Graphic packs window, find The Legend of Zelda: Twilight Princess HD > Mods > TPHD Tools, tick it and start the game.

To update, delete the old `TPHD-Tools` folder and copy the new one in.

## Using the menu

Press **ZR + D-Pad Down** to open or close the menu. You can also tap the floating icon on the GamePad (it starts in the top left corner and you can drag it anywhere).

| Button | In the menu |
| --- | --- |
| D-Pad | Move around |
| A | Select, toggle or edit |
| B | Back or cancel |
| Y | Jump to the menu bar |
| L / R | Previous / next window |
| ZL / ZR | Previous / next tab |
| Right stick | Scroll |
| Hold Minus + left stick | Move the focused window |
| Hold Minus + right stick | Resize the focused window |

The GamePad touch screen works too, and on Cemu the mouse acts as touch. Number and text fields open an on-screen keyboard.

While the menu is open the game does not see your controller. Settings > "Pause the game while the menu is open" also stops the game itself.

**Quick Access** (ZL + ZR + D-Pad Down) is a short list of the toggles and sliders you use most, without the rest of the menu. Pick what goes in it from Practice > Quick Access.

## What's in it

### Practice

- **Quick Access** list editor
- **Save & Restore**: Save Loader (named game states in folders, with optional actor and flag commands after loading), Debug Save Loader (the debug, QA and cutscene saves on the disc, plus your own `.dat` saves from the SD card), and one coordinate slot you can save and load with a button press
- **Timing**: Roll Checker (early, exact or late on the A press after a roll) and a Practice Timer with frame, load and in-game time
- **Navigation**: Warps (103 destinations plus manual stage, room, spawn and layer) and a Link position editor

### Gameplay

- **Player**: Moon Jump, Quick Transform, Invincibility, Infinite Hearts, Infinite Air
- **Items & Equipment**: Inventory Editor, Infinite Ammo, Infinite Rupees, Unrestricted Items, and equipment modifiers (Super Clawshot, Infinite Spinner Time, Spinner Speed, Remote Bombs, No Bomb Limit, Fast Iron Boots, Always Great Spin Attack)
- **Difficulty**: damage taken and dealt, Infinite Enemy Health, rupee multiplier, Rupees Only Increase, No Fall Damage, fairy revival without a fairy
- **Minigames**: STAR Game and Goat Herding instant wins, Rapids Ride point options, Fruit Pop Flight combo, Easy Sumo Wrestling
- **Movement & World**: Epona dash options, climbing, crawling and roll speed, climb height, time of day and clock speed, World Editor (story flags, area flags, keys and warp portals), and Hold B for Instant Text

The Inventory and World editors change your live save data. They don't check whether a combination makes sense, so changing story flags can break cutscenes or skip progression.

### Camera & HUD

- **Fly Cam**: free camera. Left stick moves, right stick looks, ZR/ZL go up and down, R/L change speed, L3 goes back to Link, R3 moves Link to the camera
- **Modern Camera**: right stick camera control with sensitivity, FOV, Epona support and inverted axes
- **Free Camera In Fixed-Camera Areas**: uses the normal follow camera where the game locks the view
- **Game Info** HUD (position, speed, angles, action, time of day, frame count and more, each row can be hidden or reordered) and an **Input Viewer**

### Settings

Menu opacity and size, which screen the menu is drawn on, toasts, the floating icon, the controller the game uses after a load, the reload and reset shortcuts, and rebinding for every hotkey and menu button.

## Hotkeys

All of these can be changed in Settings > Hotkeys.

| Hotkey | Default |
| --- | --- |
| Open menu | ZR + D-Pad Down |
| Quick Access | ZL + ZR + D-Pad Down |
| Reload stage | Plus + D-Pad Down |
| Reset game | Plus + X + B |
| Reload last state | ZL + ZR + Plus |
| Save coordinates | ZR + D-Pad Left |
| Load coordinates | ZR + D-Pad Right |
| Timer start/stop | A + D-Pad Left |
| Timer reset | A + D-Pad Right |
| Moon Jump (hold) | ZR + A |
| Quick Transform | ZR + Y |
| Detonate bombs | ZR + L3 |
| Fly Cam | ZL + ZR + L3 + R3 |

A hotkey only does something when its tool or cheat is turned on.

## Files on the SD card

Everything lives in `sd:/tphd_tools/`. On Cemu that is `sdcard/tphd_tools/` in Cemu's data folder.

| Path | What |
| --- | --- |
| `config.json` | All settings, hotkeys and window positions |
| `savestates/` | Save Loader states, one folder level deep |
| `saves/` | Your own `.dat` saves for the Debug Save Loader, any folder depth |
| `logs/` | Cemu only, one log per session |

On a Wii U the loader keeps the log. Delete `config.json` to go back to the defaults, or use Settings > Reset to defaults.

## Experimental

The `-experimental` build adds block push/pull and chain pull speed under Movement & World, and a Freeze Timers option under Minigames. These are less tested and some interactions with the game are still being worked out.

## Building from source

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitPPC and wut, plus the Aroma plugin toolchain (WUPS, libfunctionpatcher, libmappedmemory, libkernel) for the loader, and Python 3.

```sh
git clone --recursive https://github.com/Ori-Jakob/tphd-tools-rpl.git
cd tphd-tools-rpl
make                        # tphd_tools.rpl
make DEBUG=1                # debug build with a Diagnostics menu
make EXPERIMENTAL=1         # experimental build
```

On Windows, `build.ps1` builds the loader and the tools and writes the release zip to `dist/`:

```powershell
.\build.ps1                               # release zip
.\build.ps1 -e                            # experimental
.\build.ps1 -d                            # debug
.\build.ps1 -CemuPacks "$env:APPDATA\Cemu\graphicPacks"   # also install the Cemu pack
```

The loader plugin itself comes from [wii-u-rpl-loader](https://github.com/Ori-Jakob/wii-u-rpl-loader), checked out as a submodule.

## Credits

- [TPGZ](https://github.com/zsrtp/tpgz) for the practice tool ideas this started from
- [zeldaret/tp](https://github.com/zeldaret/tp), the Twilight Princess decompilation
- [GaryOderNichts](https://github.com/GaryOderNichts/imgui) for the Wii U Dear ImGui backend
- [devkitPro](https://github.com/devkitpro) and the [Aroma](https://github.com/wiiu-env/Aroma) team

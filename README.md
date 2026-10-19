# Capsule

Launcher/emulator for the Pokemon series, Gen I–IX, for Windows. C++17, SDL2 + OpenGL, Dear ImGui, libretro cores
(mGBA for GB/GBC/GBA, melonDS DS for NDS, Azahar for 3DS), RetroAchievements via rcheevos.

No games are included. You add ROMs dumped from your own cartridges.

[![Latest release](https://img.shields.io/github/v/release/stefanmafijas/CapsuleEmulator)](../../releases/latest)
[![Downloads](https://img.shields.io/github/downloads/stefanmafijas/CapsuleEmulator/total)](../../releases)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

**One app for every generation.** No more juggling mGBA, melonDS, a 3DS emulator and a Switch emulator,
each with its own settings, folders and key bindings.

- Gen I–IX in one library, one install, cores included
- GB, GBC, GBA, DS and 3DS run inside Capsule; Switch games run in your own emulator, embedded in the window
- RetroAchievements built in (GB/GBC/GBA/DS)
- 10 save-state slots per game, auto-saving in-game saves, fast-forward
- Every key rebindable, fullscreen, mouse as DS touch screen
- Per-user install, no admin, open source (GPL-3.0)

## Install

Download `CapsuleSetup.exe` from the [latest release](../../releases/latest) and run it.
It is a per-user install (no admin) to `%LOCALAPPDATA%\Programs\Capsule`, with the emulator cores included.
Uninstalling keeps ROMs/saves/settings unless you choose to delete them.

## Build (MSYS2 UCRT64)

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{SDL2,cmake,ninja}
```

The emulator cores are not in the repo. Download these from
https://buildbot.libretro.com/nightly/windows/x86_64/latest/, unzip them and put the DLLs in `cores/`:
`mgba_libretro.dll`, `melondsds_libretro.dll`, `azahar_libretro.dll`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/selftest.exe   # run from build/
```

Run `build/capsule.exe`. The `cores/` folder is copied next to the exe.

Installer (needs `mingw-w64-ucrt-x86_64-nsis`): `makensis installer/capsule.nsi` builds `build/CapsuleSetup.exe`.

## Use

- Library: pick a generation, then a game. The side panel adds a ROM dumped from
  your own cartridge, from a file or from a direct link to your own backup.
  Any card accepts any ROM its emulator core can run.
- In-game saves go to `saves/` (written every 5 s when changed and on close).
  melonDS DS writes DS saves itself under `saves/melonDS DS/`.
- Save states: 10 slots per game, in `states/`.
- Achievements tab: sign in to RetroAchievements (softcore). GB/GBC/GBA/DS only;
  RetroAchievements has no 3DS support. The login token is stored in `config.ini`.
- Switch games (Gen VIII–IX, Let's Go, BDSP) run in a Switch emulator you install yourself,
  its window embedded in Capsule. Set its .exe and launch arguments in Settings (`{rom}` = game file).
- 3DS/Switch files are linked where they are, not copied (`roms/<id>.link`).
- Custom card: add any GB/GBC/GBA/DS/3DS/Switch ROM (`roms/custom.txt`).
- 3DS games need decrypted `.3ds/.cci/.cxi` dumps and OpenGL 4.3.
- Game covers are generated (colors and shapes). Generation cards show mascot silhouettes from PokeAPI.
- Settings tab: rebind every key, set fast-forward speed.
- DS touch screen: click/drag with the mouse.
- Fullscreen: button in the game toolbar or F11; move the mouse to the top edge for controls.

## License

Capsule's own code is GPL-3.0 (see `LICENSE`). Bundled third-party code and the emulator cores keep
their own licenses, listed in `installer/THIRD_PARTY.txt`.

Capsule is an unofficial fan project, not affiliated with or endorsed by Nintendo, Game Freak,
Creatures or The Pokemon Company. Game names identify compatible games only. No games are included.

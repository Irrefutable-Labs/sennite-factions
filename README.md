# Sennite: Factions

A battle royale where a faction's base is funded by its members' own DSM escrow vaults.

Unreal Engine 5.8, built and run on macOS.

## Build

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" SenniteEditor Mac Development -project="$PWD/Sennite.uproject"
```

Add `-NoHotReloadFromIDE` when an Unreal editor is already open, or the build lands as a hot-reload copy the next launch does not load.

## Run

```bash
Tools/fetch_heroes.sh
```

copies the Fab hero pack the game uses (Paragon: Dekker) into `Content/`; the packs are not in this repository. Then build, and start the game on the test range:

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/Sennite.uproject" -game -windowed -ResX=1280 -ResY=720
```

W A S D to move, Left Shift with W to run, Space to jump, the mouse to look. Walk over a weapon to pick it up; 2 and 3 switch between rifle and shotgun; left mouse button fires. X, C and F preview a wall, a ramp and a floor; left mouse button builds; right mouse button changes material; the same key again leaves build mode.

## Check

```bash
Tools/check_first_playable.sh
```

starts the game on the test range without a window, presses the real keys, and passes only if the player's pawn is `ASenniteCharacter` wearing its hero, the hero animates, and it walks, runs, jumps and turns the camera. It leaves two screenshots in `Saved/FirstPlayable/`.

```bash
Tools/check_weapons.sh
```

does the same for weapons: the hero walks over the rifle and the shotgun lying on the test range, holds each in her hand with her own weapon hidden, switches between them with 2 and 3, and fires. Screenshots in `Saved/Weapons/`.

```bash
Tools/check_building.sh
```

does the same for building: with the materials a player starts with, the hero previews and builds a wall, a ramp and a floor with X, C, F and the left mouse button, changing material with the right mouse button; each piece lands on the build grid, costs its price once and is solid, and she runs up the ramp she built. Screenshots in `Saved/Building/`.

## State

- `Source/Sennite`: gameplay code (character, building, weapons, healing, storm, lobby, match and menu game modes, HUDs). Listen-server hosting; no hosted matchmaking service.
- The hero: `ASenniteCharacter` takes its mesh and animation Blueprint from `Config/DefaultGame.ini` (`HeroMesh`, `HeroAnimClass`), with the materials the pack ships with.
- `Content/Maps/TestRange`: a grey-box map on the 480 cm build grid, made by `Tools/build_test_range.py`.
- Rendering is held to what a phone does (`Config/DefaultEngine.ini`): one shadowed sun, an unshadowed sky fill, no Lumen, no ray tracing, no virtual shadow maps, no Nanite.
- Weapons: `ASenniteRifle` and `ASenniteShotgun` (`Source/Sennite/Public/SenniteWeapons.h`), with the M4 and shotgun models of the Fab pack "Low Poly Weapons VOL.1", brought into `Content/Weapons` by `Tools/import_weapons.py`. Each weapon names its mesh, grip point and muzzle in `Config/DefaultGame.ini`; the hero names the bone that holds a weapon, how the hand grips it, the bones of its own built-in weapon (hidden while a game weapon is held) and its firing animation. The guns are plain gunmetal until the pack's `Textures_Guns.png` is downloaded and the import is run again. There is no pickaxe model, and no hero has a two-handed hold or a reload animation.
- Building: a wall, a ramp and a floor in each of the three materials, with a see-through preview of each (`Source/Sennite/Public/SenniteBuildings.h`). Every piece is a slab one 480 cm build cell across, faced with one panel of the Fab pack "+10 2K PBR SciFi textures 4-FREE" (`Content/Building`, made by `Tools/import_building.py` at 1024 px). Which panel shows each material, and how much damage each takes, is in `Config/DefaultGame.ini` under `[/Script/Sennite.BuildingActor]`. One rule, `ASenniteCharacter::BuildPlacement`, places both the preview and the piece: along the quarter turn the camera faces, a wall on the far edge of the player's cell, a floor or ramp in the next cell, one level up when looking well above the horizon.
- Materials: there is no pickaxe yet, so nothing can be harvested; `StartingMaterialCount` under `[/Script/Sennite.SenniteGameMode]` gives every player 100 of each to build with.
- Not made yet: healing items, the storm and the HUD widgets. Their classes are in the code and still need meshes and layouts.

Third-party notices: `ThirdParty/NOTICE.md`.

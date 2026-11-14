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

W A S D to move, Left Shift with W to run, Space to jump, the mouse to look. Walk over a weapon to pick it up; 2 and 3 switch between rifle and shotgun; left mouse button fires. X, C and F preview a wall, a ramp and a floor; left mouse button builds; right mouse button changes material; the same key again leaves build mode. 4 and 5 hold bandages and potions; left mouse button uses one.

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

```bash
Tools/check_hud.sh
```

checks that the HUD follows the game: health, shield, materials, players left and kills from the start, the piece's name in build mode, a material count that drops when a piece is built, and the held weapon's rounds, which drop when it is fired. It checks the text the HUD sends to the screen; whether that text is legible is for the eye, in the screenshots in `Saved/Hud/`.

```bash
Tools/check_storm.sh
```

checks the storm: the safe circle holds, shrinks by the clock and holds again; the hero takes no damage inside it, loses health every second outside it while the HUD warns her, and stops losing it once back inside. For this run only, the game is started with the storm beginning with one player and running a stage a second. Screenshots in `Saved/Storm/`.

```bash
Tools/check_healing.sh
```

checks healing: the hero picks up bandages and potions, loses health outside the storm's circle, then drinks a potion (20 shield) and uses a bandage (15 health) with the real keys; counts, the item in her hand and the HUD follow. Screenshots in `Saved/Healing/`.

```bash
Tools/check_two_players.sh
```

starts two copies of the game on this machine: one hosts the test range as a listen server, the other joins it over the loopback address, and each checks what it sees of the other. The joining player gets a hero, sees the host's, builds a wall the server keeps and charges for, walks and is left where they walked to, sees the host walk, and leaves; the host sees all of that from its side. Logs and screenshots in `Saved/Net/`.

```bash
Tools/check_combat.sh
```

is the same two games, fighting: the joining player shoots the host twice, for 25 each; the host shoots the joining player until they fall, in four shots, and is credited with the kill; the fallen player becomes a spectator. Health, rounds, the hit marker, the red flash when hurt, kills and players left are checked on both sides. Logs and screenshots in `Saved/Combat/`.

```bash
Tools/check_arena.sh
```

starts the arena map once for each kind of connector. Each run holds the seeding to its promises on plans for 300 seeds and every number of bases (the same seed gives the same arena; every seed a different one; connectors that meet at a base are never the same kind; every jump is inside a walking jump's reach), then has the hero cross that connector to the far base with the real keys and mouse, jumping where it is a jump and waiting for a gap where craft cross. Pictures of each kind in `Saved/Arena/`.

`Tools/check_all.sh` runs all nine.

## State

- `Source/Sennite`: gameplay code (character, building, weapons, healing, storm, lobby, match and menu game modes, HUDs). Listen-server hosting; no hosted matchmaking service.
- The hero: `ASenniteCharacter` takes its mesh and animation Blueprint from `Config/DefaultGame.ini` (`HeroMesh`, `HeroAnimClass`), with the materials the pack ships with.
- `Content/Maps/TestRange`: a grey-box map on the 480 cm build grid, made by `Tools/build_test_range.py`.
- Rendering is held to what a phone does (`Config/DefaultEngine.ini`): one shadowed sun, an unshadowed sky fill, no Lumen, no ray tracing, no virtual shadow maps, no Nanite.
- Weapons: `ASenniteRifle` and `ASenniteShotgun` (`Source/Sennite/Public/SenniteWeapons.h`), with the M4 and shotgun models of the Fab pack "Low Poly Weapons VOL.1", brought into `Content/Weapons` by `Tools/import_weapons.py`. Each weapon names its mesh, grip point and muzzle in `Config/DefaultGame.ini`; the hero names the bone that holds a weapon, how the hand grips it, the bones of its own built-in weapon (hidden while a game weapon is held) and its firing animation. The guns are plain gunmetal until the pack's `Textures_Guns.png` is downloaded and the import is run again. There is no pickaxe model, and no hero has a two-handed hold or a reload animation.
- Building: a wall, a ramp and a floor in each of the three materials, with a see-through preview of each (`Source/Sennite/Public/SenniteBuildings.h`). Every piece is a slab one 480 cm build cell across, faced with one panel of the Fab pack "+10 2K PBR SciFi textures 4-FREE" (`Content/Building`, made by `Tools/import_building.py` at 1024 px). Which panel shows each material, and how much damage each takes, is in `Config/DefaultGame.ini` under `[/Script/Sennite.BuildingActor]`. One rule, `ASenniteCharacter::BuildPlacement`, places both the preview and the piece: along the quarter turn the camera faces, a wall on the far edge of the player's cell, a floor or ramp in the next cell, one level up when looking well above the horizon.
- Materials: there is no pickaxe yet, so nothing can be harvested; `StartingMaterialCount` under `[/Script/Sennite.SenniteGameMode]` gives every player 100 of each to build with.
- HUD: `ASenniteHUD::DrawStatus` draws health and shield, the held weapon's rounds, the three building materials (the one in use marked while building), players left and kills straight onto the canvas, sized to the screen, with no widget assets. The hit marker, blood splash and settings menu still expect widget Blueprints that do not exist.
- Storm: `AStormActor` is a safe circle with a see-through violet wall. A storm placed in a map carries its starting radius and whether its centre is random; `[/Script/Sennite.StormActor]` in `Config/DefaultGame.ini` holds the stage length (30 s; it holds for four stages and shrinks for three), how much of its radius it keeps for each second it shrinks, and how often its damage rises. A player is in the storm when farther from the circle's centre than its radius, and then loses the storm's damage every second. The game mode starts the storm once `PlayersToStartStorm` players (2) are in, after `StormStartDelay` (30 s). The test range has one, centred ahead of the start.
- Two players: a second player can join a hosted match and play in it (`Tools/check_two_players.sh`). To host by hand, start the game with `/Game/Maps/TestRange?listen`; to join, start it with the host's address in place of a map. The lobby and main menu still have no maps or widgets, so a match cannot yet be started from inside the game. The hero is loaded when the game starts (`USenniteGameInstance`), not when the first hero appears.
- Combat: a hit and a death each have one place, `ASenniteCharacter::TakeHit` and `Die`; a bullet lands once. A player who falls becomes a spectator a little above where they fell, flying and in nobody's way. The HUD shows a hit marker when a shot lands, flashes red at the edges when the player is hurt, and for a spectator says so and shows who is left.
- The match arena (`Source/Sennite/Public/SenniteArena.h`, map `Content/Maps/Arena`, made by `Tools/build_arena.py`): the bases are round platforms floating on a ring, joined by connectors chosen and shaped from a seed. Every machine in a match is sent the seed and builds the same arena; nothing of it outlasts the match. Six kinds of connector: floating pieces to jump between, a tree-top boardwalk between huts, a canyon with rims to hold from above, a deck that hovering craft cross, a tunnel through a solid mass, and bridges over the drop. The seed puts the six in an order and connectors take them in turn, so neighbours always differ. Falling off is a death. The shapes are plain blocks, coloured by kind, until each kind has scenery; scenery is seen and nothing else, laid over blocks that are what a player meets. The canyon has its first: the cliff of the Fab pack "Desert Western Cliff Layered XXL 02" on both rims and "Forest Rocky Ground" at their feet, brought into `Content/Scenery` by `Tools/import_scenery.py` with textures at 1024. To look at one: start the game with `/Game/Maps/Arena?Seed=7?Bases=4`, or add `?Connector=Canyon` to make every connector one kind. The bases are bare: a faction's own structures are not read from anywhere yet.
- Healing: `ASenniteBandage` (a pack of 5; each gives back 15 health) and `ASennitePotion` (2; each adds 20 shield), in `Source/Sennite/Public/SenniteHealing.h`. Walk over one to pick it up, 4 and 5 bring them to hand, the left mouse button uses one, which takes a little over three seconds. They have no models yet: a plain box and a plain cylinder stand in, named per item class in `Config/DefaultGame.ini`, and there is no animation for using one.

Third-party notices: `ThirdParty/NOTICE.md`.

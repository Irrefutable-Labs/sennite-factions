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

copies the Fab hero packs the game uses (Paragon: Dekker, Wraith, Crunch, Drongo, Howitzer, Rampage, Sevarog; and the Sci-Fi Trooper) into `Content/`; the packs are not in this repository. Then build, and start the game:

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/Sennite.uproject" -game -windowed -ResX=1280 -ResY=720
```

It opens on the menu (the Menu map, `Tools/build_menu.py`), worked by tap or click: PLAY opens the test range; HEROES is the character select, the hero turning on the stage, `<` and `>` (or the arrow keys) through the list, CHOOSE to keep one; HOST A LOBBY opens a lobby others on the network join, showing the address to give them; JOIN A LOBBY takes that address on the screen's keypad or the keyboard. In the lobby every player's hero stands on the stage; the host taps START and everyone goes to the test range together, each as their own hero. Dekker and Wraith are free; the other heroes are bought with D-bucks in the publisher's shop, as skins are, and can be chosen once the wallet proves it owns them. The heroes and their hands are listed in `Config/DefaultGame.ini` under `[/Script/Sennite.SenniteHeroes]`. A map or an address on the command line goes straight there, as before.

W A S D to move, Left Shift with W to run, Space to jump, the mouse to look. E opens a weapon crate the hero is beside. Walk over a weapon to pick it up; 2 and 3 switch between rifle and shotgun; left mouse button fires, and the rifle goes on firing while it is held; R reloads. X, C and F preview a wall, a ramp and a floor; left mouse button builds; right mouse button changes material; the same key again leaves build mode. G with one of your own pieces in sight edits it: left mouse button cuts out the tile in sight or puts it back (drag across tiles to do several), right mouse button puts every tile back, G again finishes. Walking over a pack of bandages or potions adds it to what the hero carries; 4 and 5 hold bandages and potions; left mouse button uses one.

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

```sh
Tools/check_editing.sh
```

checks editing and damage on a built piece with the real keys and mouse: the hero takes the rifle, builds a wood wall and shoots it twice; the wall loses the rifle's damage, the HUD shows the damage by the aim and the wall's health under it, and the wall's tiles are knocked out of line. G, the left mouse button on the middle and bottom middle tiles, and G again cut a door for everyone, free; a round through the door touches nothing and the hero walks through it; from the other side G and the right mouse button put every tile back, and four more rounds bring the wall down, which breaks into its nine tiles. Screenshots in `Saved/Editing/`.

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
Tools/check_menu.sh
```

checks the menu by taps on the screen: the game opens on it, HEROES shows each hero turning on the stage as itself, a bought hero cannot be chosen on a game with no shop, CHOOSE keeps a free hero in the profile on disk, and PLAY puts the player on the test range as that hero. Screenshots in `Saved/Menu/`.

```bash
Tools/check_lobby.sh
```

starts two copies of the game on the menu, each with its own profile. One taps HOST; the other taps JOIN, types the host's address on the keypad and joins, then chooses another hero in the lobby; the host sees it and taps START, and both reach the test range together, each as their own hero, seeing the other's. Logs and screenshots in `Saved/Lobby/`.

```bash
Tools/check_hero_hands.sh
```

becomes each hero of the list in turn holding the rifle: each holds it in the right hand pointing ahead, top up, with its own weapon hidden, and fires with its own firing animation. It logs each hero's measured grip and palm, which is where the hands in `DefaultGame.ini` come from. Bone lists and pictures in `Saved/HeroHands/`.

```bash
Tools/check_combat.sh
```

is the same two games, fighting: the joining player shoots the host twice, for 25 each; the host shoots the joining player until they fall, in four shots, and is credited with the kill; the fallen player becomes a spectator. Health, rounds, the hit marker, the red flash when hurt, kills and players left are checked on both sides. Logs and screenshots in `Saved/Combat/`.

```bash
Tools/check_arena.sh
```

starts the arena map once for each kind of connector. Each run holds the seeding to its promises on plans for 300 seeds and every number of bases (the same seed gives the same arena; every seed a different one; connectors that meet at a base are never the same kind; every jump is inside a walking jump's reach), then has the hero cross that connector to the far base with the real keys and mouse, jumping where it is a jump and waiting for a gap where craft cross. Pictures of each kind in `Saved/Arena/`.

```bash
Tools/check_match.sh
```

holds how a match ends against the rules on their own (the kills to win, the time, a base wiped: 14 cases), then starts the arena and has the hero open a weapon crate with the real key, take the rifle it lays out, fire and reload with the rounds it gave, and see the crate closed and full again. A whole match, with the publisher's match server and two players' games, needs accounts: `Tools/check_base_editor.sh` runs it (`Server/README.md`).

`Tools/check_all.sh` runs all ten.

## State

- `Source/Sennite`: gameplay code (character, building, weapons, healing, storm, lobby, match and menu game modes, HUDs). Listen-server hosting for the test range; a match runs on a server with no player of its own. No hosted matchmaking service.
- The hero: `ASenniteCharacter` takes its mesh and animation Blueprint from `Config/DefaultGame.ini` (`HeroMesh`, `HeroAnimClass`), with the materials the pack ships with.
- `Content/Maps/TestRange`: a grey-box map on the 480 cm build grid, made by `Tools/build_test_range.py`. Every map (test range, arena, menu) has the engine's sky dome, `/Engine/EngineSky/SM_SkySphere` with its sky material, 20 km out: a phone draws the sky's atmosphere only on such a mesh, and says so in red on screen without one. The sun is weak (phone-level lighting), so the atmosphere's sky is brightened on its own, six times (`SKY_BRIGHTNESS` when a map is made), for a daytime sky on a phone and a computer alike.
- Rendering is held to what a phone does (`Config/DefaultEngine.ini`): one shadowed sun, an unshadowed sky fill, no Lumen, no ray tracing, no virtual shadow maps, no Nanite.
- Weapons: `ASenniteRifle` and `ASenniteShotgun` (`Source/Sennite/Public/SenniteWeapons.h`), with the M4 and shotgun models of the Fab pack "Low Poly Weapons VOL.1", brought into `Content/Weapons` by `Tools/import_weapons.py`. Each weapon names its mesh, grip point and muzzle in `Config/DefaultGame.ini`; the hero names the bone that holds a weapon, how the hand grips it, the bones of its own built-in weapon (hidden while a game weapon is held) and its firing animation. The guns are plain gunmetal until the pack's `Textures_Guns.png` is downloaded and the import is run again. There is no pickaxe model, and no hero has a two-handed hold or a reload animation.
- Building: a wall, a ramp and a floor in each of the three materials, with a see-through preview of each (`Source/Sennite/Public/SenniteBuildings.h`). Every piece is one 480 cm build cell across, made of tiles: a wall of three by three, a ramp or a floor of two by two, each tile a slab faced with one panel of the Fab pack "+10 2K PBR SciFi textures 4-FREE" (`Content/Building`, made by `Tools/import_building.py` at 1024 px). Which panel shows each material, and how much damage each takes, is in `Config/DefaultGame.ini` under `[/Script/Sennite.BuildingActor]`. One rule, `ASenniteCharacter::BuildPlacement`, places both the preview and the piece: along the quarter turn the camera faces, a wall on the far edge of the player's cell, a floor or ramp in the next cell, one level up when looking well above the horizon. A piece can be edited by whoever built it, or in a match by anyone of its faction (`ASenniteCharacter::EditPiece`): tiles are cut out or put back, never all of them, never back into a player, at no cost; the server makes the change for every machine. A piece shows what it has taken: its tiles are knocked out of line and its seams open, more the closer it is to falling, the same on every machine (`ABuildingActor::TileTransform`); the HUD names the piece in sight with what it has left, and shows a shooter what each round took off a piece. A piece that falls breaks into its tiles, which tumble out and shrink away in under a second and a half, on every machine (`ASennitePieceRubble`); nothing above it falls with it. The see-through preview of the next piece is drawn by the building player's own machine, for them alone, and follows them every frame.
- Materials: there is no pickaxe yet, so nothing can be harvested; `StartingMaterialCount` under `[/Script/Sennite.SenniteGameMode]` gives every player 100 of each to build with.
- HUD: `ASenniteHUD::DrawStatus` draws health and shield, the held weapon's rounds, the three building materials (the one in use marked while building), players left and kills straight onto the canvas, sized to the screen, with no widget assets. The hit marker, blood splash and settings menu still expect widget Blueprints that do not exist.
- Storm: `AStormActor` is a safe circle with a see-through violet wall. A storm placed in a map carries its starting radius and whether its centre is random; `[/Script/Sennite.StormActor]` in `Config/DefaultGame.ini` holds the stage length (30 s; it holds for four stages and shrinks for three), how much of its radius it keeps for each second it shrinks, and how often its damage rises. A player is in the storm when farther from the circle's centre than its radius, and then loses the storm's damage every second. The game mode starts the storm once `PlayersToStartStorm` players (2) are in, after `StormStartDelay` (30 s). The test range has one, centred ahead of the start.
- Two players: a second player can join a hosted match and play in it (`Tools/check_two_players.sh`). To host by hand, start the game with `/Game/Maps/TestRange?listen`; to join, start it with the host's address in place of a map. The lobby and main menu still have no maps or widgets, so a match cannot yet be started from inside the game. The hero is loaded when the game starts (`USenniteGameInstance`), not when the first hero appears.
- Combat: a hit and a death each have one place, `ASenniteCharacter::TakeHit` and `Die`; a bullet lands once. A player who falls becomes a spectator a little above where they fell, flying and in nobody's way. The HUD shows a hit marker when a shot lands, flashes red at the edges when the player is hurt, and for a spectator says so and shows who is left.
- The match arena (`Source/Sennite/Public/SenniteArena.h`, map `Content/Maps/Arena`, made by `Tools/build_arena.py`): one piece of land, made for the match from a seed. Each faction playing has a plot on it, 240 m across, and between two plots lies a connector, 240 m long. Every machine in a match is sent the seed and builds the same arena; nothing of it outlasts the match. The land is ground to walk on (`ASenniteArena::GroundHeight`): hills from the seed, about 9 m high, everywhere but the level pad in the middle of each plot, where the faction's buildings stand (`PadRadius`, 50 m), and where a connector meets a plot. Its coast wanders 10 to 60 m past the plots, and its shore goes down under the water of a lake 20 m below the pads (`WaterDepth`); a hero who goes under is out. Nothing of it floats. The ground is one mesh with a point every 5 m, built by each machine from the plan, and it is what heroes stand on. Six kinds of connector, each a different way across: a river with arched bridges over it, on piers, one always whole and the others a higher arch or one broken at a gap to jump; a gorge with floating pieces to jump between; a forest with a tree-top boardwalk above it between huts; a canyon with rims to hold from above; a road that hovering craft cross; and a tunnel through a mass of rock. The seed puts the six in an order and connectors take them in turn, so neighbours always differ. What stands on the land is plain blocks, coloured by kind, until each kind has scenery; scenery is seen and nothing else, laid over blocks that are what a player meets. The canyon has its own: the cliff of the Fab pack "Desert Western Cliff Layered XXL 02" on both rims and "Forest Rocky Ground" at their feet, brought into `Content/Scenery` by `Tools/import_scenery.py` with textures at 1024. To look at one: start the game with `/Game/Maps/Arena?Seed=7?Bases=4`, or add `?Connector=Canyon` to make every connector one kind. The land is drawn as grass, the Fab texture set "Uncut Grass" (Quixel Megascans), brought into `Content/Scenery/Grass` by `Tools/import_ground.py` with its maps at 1024 and laid over the world, so ground of any shape is covered evenly (`BaseGround` in `Config/DefaultGame.ini`); a slope too steep to be a top is drawn as earth. The traffic road and the bridges' decks are asphalt, the Fab texture set "Asphalt Road", brought in the same way (`RoadSurface`). Trees and bushes stand on each plot's land, off its pad and clear of every way out, placed by the seed: four trees and seven bushes of the Fab pack "Trees and bush Pack LOWPOLY", each a few crossed planes with a cut-out photograph (8 to 52 triangles), brought into `Content/Scenery/Trees` by `Tools/import_trees.py` with the tallest 20 m high. A tree has a trunk block, which is what a hero or a shot meets; a bush stops nothing. `TreesPerBase` and `BushesPerBase` say how many the seed tries to stand on a plot (14 and 24); the forest under a tree-top boardwalk has four plots' worth of trees. Round the arena is a country of mountains and a lake: the Fab model "Trident Lakes 211111 01 Scene Output 4096", 11 km square, brought into `Content/Scenery/Land` by `Tools/import_land.py` (the terrain cut to 64,000 triangles, the water to 38,294; colour and normal maps at 2048, occlusion and roughness at 1024; no collision, no Nanite). It is set one and a half times its size (`LandScale`) with its biggest lake's widest open water where the arena stands, so the arena's land rises out of that lake and the mountains stand round it. It is seen and nothing else, and a match server with no screen does not load it. The map's haze lies on the lake and a fall ends 1.5 m under its surface; both are put there when the map is made, so after changing `Land` or `WaterDepth` make the map again (`Tools/build_arena.py`); the arena check fails on a map made for another depth. Without a match the plots are bare but for their weapon crates and trees; in a match each holds its faction's built structures on its pad (`Source/Sennite/Public/SenniteMatchBases.h`).
- Weapon crates (`Source/Sennite/Public/SenniteCrate.h`): two stand by where heroes start on each base of an arena. A hero starts with nothing to shoot with. E beside a closed crate opens it: it lays a rifle and a shotgun in front of it and a pack of bandages and of potions at its ends, and gives the hero who opened it rounds for both weapons (90 and 20); 45 s later it is closed and full again, and what nobody took is gone. A crate is the long green ammunition crate of the Fab pack "Wooden Ammo Crates Pack", brought into `Content/Crate` by `Tools/import_crate.py` with the pack's own textures at 1024 and 512, and scaled to 150 cm long. The model is one piece with no lid of its own, so an opened crate looks as a closed one does: the weapons lying in front of it show it is open. Its numbers are in `Config/DefaultGame.ini` under `[/Script/Sennite.SenniteCrate]`.
- A match (`Source/Sennite/Public/SenniteMatch.h`): played on the publisher's match server, a game with no player of its own, between the factions the publisher's server opened it for. A player plays for the faction their wallet is a member of: their game gets a ticket from the publisher's server and shows it to the match server; until then they have no hero, and a game that shows none is put out. Heroes start on their faction's base, clear of what is built there. A shot counts only across factions and only while the match is being played, which is from when players of two factions are in. A hero that falls returns on its base 5 s later with nothing it picked up; the weapons it had lie where it fell, for whoever comes by. A wall, ramp or floor built in a match is its builder's faction's: a shot wears it down only from another faction, while the match is being played. The match ends when a faction is first to 10 kills; or when its 10 minutes run out, for the faction with the most kills, or for nobody when the most is shared; or when a faction's base is wiped, every structure of it destroyed: that faction is out, its players watch, and the last faction left wins. The HUD shows the time left, each faction's kills and how it ended. The match server tells the publisher's server who won and closes. The match server picks the arena's seed when its address names none, so what lies between the bases differs from match to match. The numbers are in `Config/DefaultGame.ini` under `[/Script/Sennite.SenniteMatch]`. There is no way into a match from inside the game yet: a player's game is started on the match server's address.
- Healing: `ASenniteBandage` (a pack of 5; each gives back 15 health) and `ASennitePotion` (2; each adds 20 shield), in `Source/Sennite/Public/SenniteHealing.h`. Walk over one to pick it up, 4 and 5 bring them to hand, the left mouse button uses one, which takes a little over three seconds. They have no models yet: a plain box and a plain cylinder stand in, named per item class in `Config/DefaultGame.ini`, and there is no animation for using one.

Third-party notices: `ThirdParty/NOTICE.md`.

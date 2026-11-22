# Sennite: Factions

A battle royale where a faction's base is funded by its members' own DSM escrow vaults.

**It works on Android.** The game is played by touch on real Android phones: the phone build has been run on a Galaxy A16, arena and all (see [On a phone](#on-a-phone)). It also plays on a Mac with a keyboard and mouse.

Unreal Engine 5.8, built on macOS. The publisher's server, which runs the factions, the shop and the matches on DSM, is in `Server/` (see `Server/README.md`).

## Build

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" SenniteEditor Mac Development -project="$PWD/Sennite.uproject"
```

Add `-NoHotReloadFromIDE` when an Unreal editor is already open, or the build lands as a hot-reload copy the next launch does not load.

## Run

```bash
Tools/fetch_heroes.sh
```

copies the Fab packs the game uses but this repository does not hold (the heroes: Paragon Dekker, Wraith, Crunch, Drongo, Howitzer, Rampage and Sevarog, and the Sci-Fi Trooper; and the "Stylized Laser Beam VFX" pack the lasers are drawn with) into `Content/`. Then build, and start the game:

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/Sennite.uproject" -game -windowed -ResX=1280 -ResY=720
```

A map or an address on the command line goes straight there. With `-PublisherUrl=http://<host>:8790` the game uses a publisher's server: the store, the base editor and faction matches need one, and without one the game plays with none of them. Other options: `-Sky=DAY`, `DUSK` or `NIGHT` forces a match's sky; `-ProfileSlot=<name>` keeps the settings and the chosen hero under another name; `-PublisherPlayerFile=<file>` is where the player's token with the server is kept (`Saved/Publisher/player.json`).

## The menus

The game opens on the first screen (the Menu map, made by `Tools/build_menu.py`: a hilltop in the arena's lake, with its rocks, trees and mountains), worked by tap or click; Escape or Android's back button goes back. The owner's two songs, "Come With Me" and "Demon Mode" (`Tools/import_music.py`), play one after the other on the menu and in the lobby, never in a match.

- **PLAY** opens a freshly seeded arena on this machine.
- **STORE** sells what a wallet can own, in five sections: HEROES (every hero turning on the stage, a tile for each along the bottom, `<` and `>` or the arrow keys through the list, CHOOSE to play as one), SKINS (each shown on its hero; one owned is worn with WEAR and taken off with TAKE OFF), WEAPONS (what the shop sells: the energy rifle, the sniper, the axe and the katana), MATERIALS (wood, stone and steel in packs: BUY one for D-bucks, SELL one back to the shop for a quarter less; the shop is the only buyer the game has for a material, and D-bucks the only thing it is bought or sold for) and SWAP (any token of the publisher's vaults for another, ERA and D-bucks by default, at DSM's quote through the vaults, accepting no less than 97% of it). The wallet's D-bucks are top right. The button under an item steps the player through: CONNECT YOUR WALLET (on a phone it opens the DSM wallet on the game's connect code; on a computer it copies the code for the wallet to read), then BUY, or GET D-BUCKS (2 ERA for D-bucks) when there are too few. Every purchase and trade is approved in the player's DSM wallet, under Apps, Waiting; on a phone the game opens the wallet there itself and the wallet comes back to the game once it is decided.
- **BASE EDITOR** opens the faction's base on the test range in the planning view (below). It is greyed out without a publisher's server.
- **HOST A LOBBY** opens a lobby others on the network join, showing the address to give them; **JOIN A LOBBY** takes that address on the screen's keypad or the keyboard. In the lobby every player's hero stands on the stage, HEROES chooses another, and the host taps START: everyone goes to an arena together, each as their own hero.
- **SETTINGS**: look sensitivity, invert look, the volume of everything, the music's volume (10% until the player sets it) and graphics quality (the device's own until one is chosen). They apply at once and are kept in the player's profile on disk, with the chosen hero and the last address joined.
- **QUIT**.

Dekker and Wraith are free. Crunch, Drongo, Howitzer, Rampage, Sevarog and the trooper are bought with D-bucks (1,200 each), as the 20 skins are (600 to 1,000), and are chosen once the wallet proves it owns them. The heroes and their hands are listed in `Config/DefaultGame.ini` under `[/Script/Sennite.SenniteHeroes]`; the prices are in `Server/world/shop.json`.

In play, MENU (Escape, Android's back button, or the MENU button top left on a touch screen) opens the in-game menu over the game, which goes on behind it: RESUME, SETTINGS, STORE and BASE EDITOR (the base editor's shop and planning view, where there is one: on the test range with a publisher's server, never in an arena), MAIN MENU and QUIT GAME, each of the last two asked twice. A host who goes back to the main menu ends the game for everyone in it.

The base editor (B, or BASE on a touch screen) is a view from above of the faction's parcel. W A S D move the cursor; Q and E choose a prefab (a watchtower, a lookout, a workshop, a wall; `Server/world/catalogue.json`), R turns it and F places it; N founds a faction on a free parcel, Tab goes to the next parcel and J takes up an invitation. On a structure, C commits materials from the wallet (1 picks the resource, Z the amount), T takes them back, V replaces, X dismantles and G rebuilds. O shows the connect code and M asks the wallet to prove what it holds. P is the faction's people (I invites, K votes to remove a member, U takes the vote back, L leaves); H is the shop and I the inventory, where skins are worn and taken off. The wallet signs each of these and the publisher's server decides what comes of it.

## Controls

**Keyboard and mouse.** W A S D to move (the up and down arrows too; left and right turn), Left Shift with W to run, Space to jump, the mouse to look. 1 is the hero's own weapon, 2 the rifle, 3 the shotgun, 6 the energy rifle, 7 the sniper, 8 the axe, 9 the katana, 4 bandages, 5 potions. The left mouse button fires (the rifle goes on firing while it is held), swings the axe or the katana, or uses what is in hand; hold the right mouse button to aim; R reloads. E opens a weapon crate the hero is beside. X, C and F preview a wall, a ramp and a floor; the left mouse button builds; the right mouse button changes material; the same key again leaves build mode. G with a piece you may edit in sight edits it: the left mouse button cuts out the tile in sight or puts it back (drag across tiles to do several), the right mouse button puts every tile back and finishes, R takes the whole piece away, and G again finishes.

**Touch** (`Source/Sennite/Public/SenniteTouch.h`), shown on a touch screen. A stick under the left thumb walks, and pushed all the way ahead it runs; a drag anywhere else looks, and a finger held on FIRE can drag to look while firing. FIRE (BUILD in build mode, USE with a healing item, ATTACK with the hero's own weapon, SWING with the axe or the katana, TILE while editing), JUMP, RELOAD (REMOVE while editing; dimmed, and heard refused, with no gun in hand), AIM (a tap aims and the next lets go; MAT in build mode, RESET while editing; not there for a melee hero with no gun, nor with the axe or the katana) and OPEN beside a closed crate; WALL, RAMP, FLOOR, EDIT (with a piece to edit in sight; DONE while editing) and BASE along the top right; MENU top left. What the hero holds is on one button, the wheel, at the bottom in the middle, which names what is in hand: a tap takes the next thing the player has (the hero's own weapon, each gun, the axe and the katana, bandages, potions, round again); a finger held on it, or slid off it, opens the wheel: what the player has on an arc round it (nothing they have not got), what is in hand edged in gold; slide to one and let go, and it is in hand and the wheel shuts (let go in the middle, or on the button, and nothing changes). Each button presses the key the keyboard would. In the base editor a grid of the editor's keys runs down the right side, and a tap on the map moves the cursor there.

## On a phone

Android only; there is no iOS build. The phone build is one APK holding the menu, the arena and the test range.

```bash
Tools/package_android.sh
```

builds, cooks and packages it into `Saved/Phone`, with the publisher's server written into the APK (`SENNITE_PUBLISHER_URL`, by default this computer's Wi-Fi address on port 8790; `none` for no server), so a phone the game is newly installed on reaches it too, (log in `Saved/Phone/package.log`) and prints the `adb install -r` line that puts it on the phone. `Tools/package_android.sh -iterate` cooks only what changed. It needs adb, the Android NDK r27c and Java (`ANDROID_HOME`, `NDKROOT` and `JAVA_HOME` default to Android Studio's), and the engine's Android target platform (Epic Games Launcher, UE 5.8, Options, Target Platforms, Android). The app is `com.irrefutablelabs.sennitefactions`: arm64, Vulkan, landscape.

To play with the store, the base editor and faction bases on phones on this computer's Wi-Fi:

```bash
DSM_APP_HOST_BIN=<dsm-app-host> DSM_ENV_CONFIG=<the storage set's env config> Tools/phone_publisher.sh
```

runs the publisher's side until Ctrl-C: its own `dsm-app-host`, with a relay the DSM wallet on a phone can reach, and the publisher's server on port 8790. `DSM_APP_HOST_BIN` is built from the DSM commit the storage set runs (for the beta set, DSM main) and `DSM_ENV_CONFIG` is that set's env config (for the beta set, `dsm_client/frontend/public/dsm_env_config.toml` in that checkout). On its first run it asks the network's faucet to fund the account. The account and the server's record are kept in `Saved/Publisher/` (`SENNITE_PUBLISHER_DIR`; one folder per storage set). `Saved/Publisher/host` is a hot wallet with its recovery phrase on disk: keep only test balances on it.

```bash
Tools/phone_game.sh
```

points the game on the phone at that server (it writes the game's command line on the phone over adb and restarts the game); `Tools/phone_game.sh off` takes it off again. `Tools/phone_give.sh STEEL 300` has the publisher give building materials (WOOD, STONE or STEEL) to every connected wallet that has accepted them.

## Check

`Tools/check_all.sh` runs the 19 in-game checks that need no accounts, one after the other, and passes only if all of them pass. Each starts the game without a window, presses the real keys or taps the real buttons, and leaves its log and screenshots under `Saved/`.

```bash
Tools/check_first_playable.sh
```

checks that the player's pawn is `ASenniteCharacter` wearing its hero, the hero animates, and it walks, runs, jumps and turns the camera; and that with the storm off the storm placed on the range is not seen and is nobody's. Screenshots in `Saved/FirstPlayable/`.

```bash
Tools/check_weapons.sh
```

checks weapons: with no gun in hand the fire button plays the hero's own attack and a shot leaves the top of her staff, a second pull before the attack is over sends nothing and R loads nothing; then the hero opens the crate on the test range, has its rifle in both hands at once with nothing laid on the ground and pointing where she aims, sees each shot kick the rifle and leave the muzzle, holds the rifle and the shotgun each in her hand with her own weapon hidden, switches between them with 2 and 3, and fires; the crate gives her the rifle and the shotgun and keeps the energy rifle and the sniper, which she has not bought; then, with both made hers as the server makes them a player's, the same crate, full again, gives them, and she holds and fires the energy rifle (6) and the sniper (7); then the axe (8) and the katana (9), each in her right hand alone, not aimed, swung with her own attack, sending no bolt, its blow taking its own damage off a wall put in front of her. Screenshots in `Saved/Weapons/`.

```bash
Tools/check_building.sh
```

checks building: with the materials a player starts with, the hero previews and builds a wall, a ramp and a floor with X, C, F and the left mouse button, changing material with the right mouse button; each piece lands on the build grid, costs its price once and is solid, and she runs up the ramp she built. A piece taken away while editing gives back what it cost, once. Screenshots in `Saved/Building/`.

```bash
Tools/check_editing.sh
```

checks editing and damage: the hero takes the rifle, builds a wood wall and shoots it twice; the wall loses the rifle's damage, the HUD shows the damage by the aim and the wall's health under it, and the wall's tiles are knocked out of line. G, the left mouse button on the middle and bottom middle tiles, and G again cut a door for everyone, free; a round through the door touches nothing and the hero walks through it; from the other side G and the right mouse button put every tile back. Then what holds a piece up: looking up she builds a second wall on top of the first, which stands; a wall one level up with nothing under or beside it is not built and costs nothing; four more rounds bring the first wall down, and the wall it held falls a moment after it, each breaking into its nine tiles. Screenshots in `Saved/Editing/`.

```bash
Tools/check_hud.sh
```

checks that the HUD follows the game: health, shield, materials, players left and kills from the start, the piece's name in build mode, a material count that drops when a piece is built, how to open a crate when beside one and what it gave once opened, and the held weapon's rounds, which drop when it is fired. It checks the text the HUD sends to the screen; whether that text is legible is for the eye, in the screenshots in `Saved/Hud/`.

```bash
Tools/check_storm.sh
```

checks the storm, which the game does not have for now (see Storm below): the safe circle holds, shrinks by the clock and holds again; the hero takes no damage inside it, loses health every second outside it while the HUD warns her, and stops losing it once back inside. For this run only the storm is on, beginning with one player and running a stage a second. Screenshots in `Saved/Storm/`.

```bash
Tools/check_healing.sh
```

checks healing: the hero has bandages and potions from the range's crate, loses health outside the storm's circle (on for this run only), then drinks a potion (20 shield) and uses a bandage (15 health) with the real keys; counts, the item in her hand and the HUD follow. Screenshots in `Saved/Healing/`.

```bash
Tools/check_touch.sh
```

plays by touch on a screen with a phone's safe zone: the buttons shown, a drag to turn, the stick to walk and run, OPEN, the weapon wheel (slid to a gun and let go, held open and let go where it came down, tapped for the next thing, slid to the axe she has not bought and refused), FIRE held, RELOAD (dimmed and refused with no gun in hand), JUMP, building, editing and a potion. Screenshots in `Saved/Touch/`.

```bash
Tools/check_game_menu.sh
```

opens the in-game menu by keys and again by touch: settings, resume, main menu and quit. Logs in `Saved/GameMenu/` and `Saved/GameMenuTouch/`.

```bash
Tools/check_two_players.sh
```

starts two copies of the game on this machine: one hosts the test range as a listen server, the other joins it over the loopback address, and each checks what it sees of the other. The joining player gets a hero, sees the host's, builds a wall the server keeps and charges for, walks and is left where they walked to, sees the host walk, and leaves; the host sees all of that from its side. Logs and screenshots in `Saved/Net/`.

```bash
Tools/check_menu.sh
```

checks the first screen by taps on a screen with a phone's safe zone: the game opens on it, HEROES shows each hero turning on the stage as itself, a bought hero cannot be chosen on a game with no shop, CHOOSE keeps a free hero in the profile on disk, and PLAY puts the player in the arena as that hero. Screenshots in `Saved/Menu/`.

```bash
Tools/check_lobby.sh
```

starts two copies of the game on the menu, each with its own profile. One taps HOST; the other taps JOIN, types the host's address on the keypad and joins, then chooses another hero in the lobby; the host sees it and taps START, and both reach the arena together, each as their own hero, seeing the other's. Logs and screenshots in `Saved/Lobby/`.

```bash
Tools/check_hero_hands.sh
```

becomes each hero of the list in turn holding the rifle: each holds it in both hands, the grip hand on the grip and the other on the foregrip, pointing where the player aims, top up, with its own weapon hidden, and a pull of the trigger kicks it. It logs each hero's measured grip and palm, which is where the hands in `DefaultGame.ini` come from. Bone lists and two pictures of each hero in `Saved/HeroHands/`: one from in front and one from the player's camera.

```bash
Tools/check_combat.sh
```

is two games, fighting: the joining player shoots the host twice, for 25 each; the host shoots the joining player until they fall, in four shots, and is credited with the kill; the fallen player becomes a spectator and leaves a crate holding their weapons where they fell. Health, rounds, the hit marker, the red flash when hurt, kills and players left are checked on both sides. Logs and screenshots in `Saved/Combat/`.

```bash
Tools/check_arena.sh
```

starts the arena map once for each kind of connector (or only the one named: `Bridges`, `Tunnel`, `Canyon`, `TreeTop`, `Floating`, `Traffic`). Each run holds the seeding to its promises on plans for 300 seeds and every number of bases (the same seed gives the same arena; every seed a different one; connectors that meet at a base are never the same kind; every jump is inside a walking jump's reach), then has the hero cross that connector to the far base with the real keys and mouse, jumping where it is a jump and waiting for a gap where craft cross. Pictures of each kind in `Saved/Arena/`.

```bash
Tools/check_match.sh
```

holds how a match ends against the rules on their own (the kills to win, the time, a base wiped: 14 cases), then starts the arena and has the hero open a weapon crate with the real key, hold the rifle it gave with nothing laid on the ground, fire and reload with the rounds it gave, see the crate closed and full again, and open it again for rounds and packs but no second rifle.

```bash
Tools/check_sounds.sh
```

checks that every named sound loads and is asked for when its event happens. It runs with no sound device, so it says nothing of what a sound is like to the ear.

```bash
Tools/check_skins.sh
```

puts each skin the shop sells on its hero and checks that it keeps every part of the body; a picture of each from in front and behind.

```bash
Tools/check_sky.sh
```

checks the DAY, DUSK and NIGHT skies and that the seed chooses them by their weights (or only the one named).

```bash
Tools/check_mining.sh
```

checks mining on an arena of two bases: the deposits hold what the game's settings say; with no deposit aimed at the droid's key takes none out; aimed at a wood deposit it takes the player's droid out and sends it there, and what it takes comes out of the deposit and is the player's to build with; the same droid is sent on to stone, and goes on mining with its player 90 m away; its own player's shot does not count on it, three of another player's rifle rounds shoot it down, and then no other droid is taken out that game. Pictures in `Saved/Mining/`.

Three more need a DSM account on a storage set, and `Tools/check_all.sh` does not run them:

- `Tools/check_base_editor.sh` (`DSM_APP_HOST_BIN`, `DSM_ENV_CONFIG`): the base editor, people, the shop and skins, the server killed and back, a prize, three whole matches with the publisher's match server and two players' games, and a hero bought, on real wallets. `SENNITE_TOUCH=1` plays it by touch. See `Server/README.md`.
- `Tools/check_building_paused.sh` (also `DSM_STORAGE_NODE_BIN`, on a developer's own local storage set only): with the leader seat stopped, building pauses and then resumes.
- `Tools/check_store.sh` (a running publisher's server at `SENNITE_PUBLISHER_URL`, by default `http://127.0.0.1:8790`, as `Tools/phone_publisher.sh` starts): STORE and BASE EDITOR on the first screen by taps, with no wallet connected.

## State

- `Source/Sennite`: gameplay code (character, building, weapons, healing, storm, menus, store, base editor, lobby, match and arena game modes, HUDs, touch controls). PLAY plays an arena on this machine; a lobby is hosted by a player's game as a listen server (or by hand: `/Game/Maps/TestRange?listen` hosts the test range, and the host's address in place of a map joins it); a faction match runs on the publisher's match server, a game with no player of its own. There is no hosted matchmaking service.
- Heroes: `ASenniteCharacter` takes its mesh and animation Blueprint from the hero list in `Config/DefaultGame.ini`, with the materials the pack ships with. Crunch, Rampage and Sevarog fight hand to hand (`bMelee`): their own attack is a blow, 40 damage within 2.6 m, with no shot and no aim-in, heard as a swing (Rampage's as the owner's punch recording); with a gun in hand they shoot as any hero does. Sevarog floats, so his steps are not heard. Dekker has a woman's voice, and Wraith, Drongo and the trooper a man's. A skin the wallet has proved it owns is worn in every world, drawn with its own materials, where the skin is made for the hero's skeleton.
- `Content/Maps/TestRange`: a grey-box map on the 480 cm build grid, made by `Tools/build_test_range.py`. Every map (test range, arena, menu) has the engine's sky dome, `/Engine/EngineSky/SM_SkySphere` with its sky material, 20 km out: a phone draws the sky's atmosphere only on such a mesh, and says so in red on screen without one. The sun is weak (phone-level lighting), so the atmosphere's sky is brightened on its own, six times (`SKY_BRIGHTNESS` when a map is made).
- Sky (`Source/Sennite/Public/SenniteSky.h`): a match is played by day, at dusk or at night, drawn from the arena's seed by weight (day 2, dusk 1, night 1), so every player in it has the same sky; at night the shadowed light is the moon and the stars are out. `-Sky=` forces one.
- Rendering is held to what a phone does (`Config/DefaultEngine.ini`): one shadowed sun, an unshadowed sky fill, no Lumen, no ray tracing, no virtual shadow maps, no Nanite.
- Weapons (`Source/Sennite/Public/SenniteWeapons.h`): four guns and two blades, each from one of the owner's Fab packs, brought into `Content/Weapons` by `Tools/import_weapons.py`. The rifle is "Free Sci-Fi Rifle" (30 rounds, fires while held, 25 a hit); the shotgun is "Scifi Gun" (5 shells, one shot every 1.3 s, 80 a hit, 30 m); the energy rifle is "sci-fi Enrgy Rifle" (40 to a cell, ten shots a second, 12 a hit); the sniper is "Sci-Fi Weapon Sniper Gun" (5 rounds, one shot every 1.5 s, 90 a hit, 600 m). The axe is "Scifi axe" (55 a blow, out to 2.8 m, one swing every 0.9 s) and the katana "red scifi katana" (35 a blow, out to 3 m, one swing every 0.55 s): each is held in the hand that holds the hero's own weapon (which is hidden), swung with the hero's own attack, its blow landing a moment into the swing on the nearest thing ahead; it has no rounds and is not aimed (`bMelee` and its numbers under each in `Config/DefaultGame.ini`). The rifle and the shotgun are free; the energy rifle and the sniper are bought in the store for 900 D-bucks each, the axe and the katana for 600 (`bFree` and `ShopId` on each). What a wallet bought is the player's at once, with no crate, and again after a fall; a crate gives the free guns and rounds for every gun the player has. Each model is cut down to under 30,000 triangles with no Nanite, its muzzle along +X. The sci-fi rifle's pack has no textures, so it is one dark metal; the others are painted: each of the pack's flat colours is baked into the model's vertex colours and drawn with one material (`M_GunPaint`), glowing where the pack's material glowed. The sniper's pack has no real colours for its parts, so its colours are chosen in the import. Each gun names its mesh, grip, foregrip, muzzle, what it is called, how fast it fires and reloads, whether it fires while held, the rounds a crate gives for it, and its shot's sound in `Config/DefaultGame.ini`; how hard and how far each hits is with its bullet in `SenniteWeapons.cpp`. One path fires, reloads and switches every gun (`ServerFireWeapon`, `ServerReloadWeapons`, `ServerHoldGun`). The hero names the bone that holds a weapon, how the hand grips it, and the bones of its own built-in weapon (hidden while a game weapon is held). A gun is as large as the hero's arm is long, up to 2.2 times its model. With no gun in hand (key 1) the hero attacks with its own weapon, the one it ships with: a staff, a fist, a hammer. The fire button plays the hero's own attack (`AttackMontage` in the hero list) and, for a hero that is not melee, a laser leaves the weapon (`ASenniteOwnBolt`, from the hero's `CastBone`: the top of Dekker's staff), 20 a hit out to 40 m, one every half second, with nothing to load or run out of. There is no reload animation.
- Holding a gun (`Source/Sennite/Public/SenniteHeroMesh.h`): the heroes' own animations are for their own weapons or for none, so while a hero has a gun in hand both its arms are placed after its animation has run, on every machine: the gun points where the player aims, the grip hand holds it in front of that shoulder, the other hand is on the foregrip, each arm bends at the elbow to reach, and a gun too far for the other hand comes nearer until it can. A shot kicks the gun back and up; the heroes' own firing animations (a cast, a punch, a swing) are not played with a gun. With a gun in hand the hero faces where the player aims and steps sideways and back, and the player's camera looks over its shoulder; with none it turns to face the way it walks and the camera is straight behind. Aiming in, the player sees through the hero's own eyes, closer (the view narrows from 90° to 45°), and their own hero and gun are not drawn for them; everyone else still sees the hero. The arms' numbers are under `[/Script/Sennite.SenniteHeroMesh]` in `Config/DefaultGame.ini`; the camera's are in `SenniteCharacter.h`.
- Bullets (`Source/Sennite/Public/ProjectileActor.h`): the server flies each bullet and decides what it lands on; every machine is sent it and draws it. The rifle, the shotgun and the sniper fire the owner's ammunition models (`SM_RifleRound`, `SM_ShotgunShot`, `SM_SniperRound`, from "Pistol, Rifle, Shotgun Ammo" by `Tools/import_ammo.py`), flying point first. The energy rifle and a hero's own weapon fire laser beams from the "Stylized Laser Beam VFX" pack, with a flash at the muzzle (not drawn right in front of the shooter's own eyes) and a burst where they land. Each bullet class's section in `Config/DefaultGame.ini` says how it is drawn.
- Sounds (`Source/Sennite/Public/SenniteSounds.h`, `Content/Sounds`): everything that happens is heard. From the owner's two sound packs, the Sonniss GDC 2024 bundle (royalty-free for games) and the NOX SOUND Essentials series (CC0): `Tools/cut_sounds.py` cuts each sound out of its pack's file (needs ffmpeg) and `Tools/import_sounds.py` brings them in. Seven are the owner's own recordings (2026-11-19), cut the same way: a button pressed or a thing approved, a button that cancels or goes back, every smaller tick, a build piece placed, a piece or a tile of one taken away, every laser's shot, and Rampage's punch. Every sound has a name, and `Config/DefaultGame.ini` (`SenniteSounds`) says which recordings it is, how loud, and how far it carries. Heard: a hero's steps by the ground it walks on (grass, wood, stone, steel: several takes each, never the same twice running), its jump and landing, a hit on it and its fall, and for the heroes that are people a voice with them, though never when they attack or shoot (the owner's word); each gun's shot (the rifle, the shotgun and the sniper are recordings of real guns; the energy rifle's shot and a hero's own are lasers, and sound as his laser recording; none is from the UI pack), a reload, another weapon taken in hand, the hero's own attack and its swing; a shot landing on a building piece by what the piece is made of, a piece built and a piece broken, a node harvested; a thing picked up, a bandage and a potion used, a crate opened; the match starting (a voice) and won or lost; the wallet connecting, D-bucks arriving and something bought; every button of the menus, the in-game menu and the base editor (a greyed-out one is heard refused, and a press that changes the screen, as PLAY, is heard to its end on the next); in play every tap of a button as the owner's press recording (a slot or RELOAD that has nothing to act on is heard refused; JUMP is heard only as the jump; FIRE is its shot and MENU the menu opening); a building piece's edit as it begins, at each tile marked and as it is made. No sound goes round in the background: the birds on the menu were taken out on the owner's word (the recordings and the setting for a map's air are still there, unused). Most of it is worked out on each machine from what it sees (how far a hero walked, when it left the ground, where a bullet ended), so nothing more goes over the network for it. The packs' files are named for what they are, and each was chosen from its name and length without listening; the choices are one table in `Tools/cut_sounds.py`. The crate's opening is still from the Fab pack "Cyberpunk UI SFX Pack". A storm, if the game had one on, has an alarm and rain. On the test range the ground stops no bullet, so a shot at the ground is not heard landing.
- Building: a wall, a ramp and a floor in each of the three materials, with a see-through preview of each (`Source/Sennite/Public/SenniteBuildings.h`). Every piece is one 480 cm build cell across, made of tiles: a wall of three by three, a ramp or a floor of two by two, each tile a slab faced with one panel of the Fab pack "+10 2K PBR SciFi textures 4-FREE" (`Content/Building`, made by `Tools/import_building.py` at 1024 px). Which panel shows each material, and how much damage each takes, is in `Config/DefaultGame.ini` under `[/Script/Sennite.BuildingActor]`. One rule, `ASenniteCharacter::BuildPlacement`, places both the preview and the piece: along the quarter turn the camera faces, a wall on the far edge of the player's cell, a floor or ramp in the next cell, on the level the player stands on. Standing on a ramp that level is the one the ramp reaches at the edge faced (its top facing up, its foot facing down), so ramps built one from another climb; looking well up builds one level higher, and looking well down from a piece one level lower. A piece can be edited by whoever built it, or in a match by anyone of its faction (`ASenniteCharacter::EditPiece`): tiles are cut out or put back, never all of them, never back into a player, at no cost; the server makes the change for every machine. While editing, the reload button takes the whole piece away (`ASenniteCharacter::ServerRemovePiece`): it goes without breaking, and what it cost goes back to whoever built it, once. A piece shows what it has taken: its tiles are knocked out of line and its seams open, more the closer it is to falling, the same on every machine (`ABuildingActor::TileTransform`); the HUD names the piece in sight with what it has left, and shows a shooter what each round took off a piece. A piece that falls breaks into its tiles, which tumble out and shrink away in under a second and a half, on every machine (`ASennitePieceRubble`). A piece stands only while something holds it up: the land or anything else solid that is not a piece or a hero (a rock, a base's building), or a piece touching it that is itself held up (`ABuildingActor::HeldUp`). A piece with nothing to hold it is not built and costs nothing, and when a piece falls every piece it alone was holding falls a quarter of a second after it. The see-through preview of the next piece is drawn by the building player's own machine, for them alone, and follows them every frame.
- Materials: `StartingMaterialCount` under `[/Script/Sennite.SenniteGameMode]` gives every player 100 of each to build with.
- Mining (`Source/Sennite/Public/SenniteMining.h`): deposits of wood, stone and steel stand on the land between the bases of an arena, each marked on the HUD with what it holds; both sides take from the same ones. A deposit holds so much and no more: it shrinks as it is mined, and with its last unit it is destroyed. A player has one mining droid a game. It is taken in hand in a weapon's place (T; by touch, DROID on the wheel), and with it in hand the fire button (SEND by touch) sends it to the deposit aimed at: the first time it is taken out, and it goes there low over the ground and takes a unit every so often until the deposit is used up or it is sent to another. Taking a weapon or a healing item in hand, or building, puts it away. Once out it stays out, whatever becomes of its player's hero. Another player's shots bring it down (three rifle rounds), and then it is gone until the next game; its own player's do not count. What it takes is its player's to build with at once. In a match the publisher's match server runs, the deposits hold what the publisher says (`Server/world/matches.json`, `mining`), and when the match is over the match server tells the publisher's server what each wallet's droid took and the publisher's account gives it to that wallet: materials for the faction's base, or to sell back to the shop. On any other arena the deposits hold what `[/Script/Sennite.SenniteDeposit]` says and what is mined is for that game alone. The droid is the Fab pack "FREE SciFi Drone" and the steel deposit "Slate Rock", brought in by `Tools/import_mining.py`; the wood and stone deposits are the arena's own tree and rock. With a publisher's server, building materials are also objects in the player's DSM wallet, committed to the faction's base in the base editor.
- HUD: `ASenniteHUD::DrawStatus` draws health and shield, the held weapon's rounds, the three building materials (the one in use marked while building), players left and kills, the hit marker and a red flash at the edges when hurt, straight onto the canvas, sized to the screen, in the menus' style, with no widget assets.
- Storm: the game has no storm for now. `bEnabled` under `[/Script/Sennite.StormActor]` in `Config/DefaultGame.ini` is off: a storm placed in a map is then not seen, never started and hurts nobody, and an arena has none placed on it anyway. The code is kept, and `Tools/check_storm.sh` turns it on for its own run. When on: `AStormActor` is a safe circle with a see-through violet wall. A storm placed in a map carries its starting radius and whether its centre is random; `[/Script/Sennite.StormActor]` holds the stage length (30 s; it holds for four stages and shrinks for three), how much of its radius it keeps for each second it shrinks, and how often its damage rises. A player is in the storm when farther from the circle's centre than its radius, and then loses the storm's damage every second. The game mode starts the storm once `PlayersToStartStorm` players (2) are in, after `StormStartDelay` (30 s). The test range has one, centred ahead of the start.
- Players: the hero is loaded when the game starts (`USenniteGameInstance`), not when the first hero appears. Once in a game, a hero is changed only in the lobby, and a bought one only with a hero pass the publisher's server makes out to that game server for that wallet.
- Combat: a hit and a death each have one place, `ASenniteCharacter::TakeHit` and `Die`; a bullet lands once. A player who falls becomes a spectator a little above where they fell, flying and in nobody's way. The HUD shows a hit marker when a shot lands, flashes red at the edges when the player is hurt, and for a spectator says so and shows who is left.
- The match arena (`Source/Sennite/Public/SenniteArena.h`, map `Content/Maps/Arena`, made by `Tools/build_arena.py`): one piece of land, made for the match from a seed (random each time unless `?Seed=` names one). Each faction playing has a plot on it, 240 m across, and between two plots lies land 480 m long and 240 m wide, with a connector of 240 m by 120 m in its middle. Every machine in a match is sent the seed and builds the same arena; nothing of it outlasts the match. The land is ground to walk on (`ASenniteArena::GroundHeight`): hills from the seed, about 9 m high, everywhere but the level pad in the middle of each plot, where the faction's buildings stand (`PadRadius`, 50 m), and where a connector meets a plot. Its coast wanders 10 to 60 m past the plots, and its shore goes down under the water of a lake 20 m below the pads (`WaterDepth`); a hero who goes under is out. The ground is one mesh with a point every 5 m, built by each machine from the plan, and it is what heroes stand on. Six kinds of connector, each a different way across: a river with arched bridges over it, on piers, one always whole and the others a higher arch or one broken at a gap to jump; a gorge with pieces hanging over it to jump between, some with cover; a forest with a tree-top boardwalk above it between huts; a canyon, a gorge 14 to 22 m wide cut through a mesa 28 to 34 m high that slopes down to each plot, with three side valleys; a road that vehicles cross without stopping, lane after lane, and one that reaches a hero kills it (the hatchback, the pickup, the SUV, the sports car and the box truck of the Fab pack "Vehicle Variety Pack", each at its own size, brought into `Content/Traffic` by `Tools/import_vehicles.py` and named in `CraftModels`); and a tunnel through a mass of rock. The seed puts the six in an order and connectors take them in turn, so neighbours always differ. To look at one: start the game with `/Game/Maps/Arena?Seed=7?Bases=4`, or add `?Connector=Canyon` to make every connector one kind. The land is drawn as grass, the Fab texture set "Uncut Grass" (Quixel Megascans), brought into `Content/Scenery/Grass` by `Tools/import_ground.py` and laid over the world, so ground of any shape is covered evenly (`BaseGround` in `Config/DefaultGame.ini`); a slope too steep to be a top is drawn as earth, and the canyon as its own rock. The traffic road and the bridges' decks are asphalt, the Fab texture set "Asphalt Road" (`RoadSurface`). Each plot is dressed by the seed, clear of its pad and every way out: cover rocks round the plot and on the ways into it, outcrops, one crag, a steel frame, scattered stones and stony patches (the rocks from the Fab pack "Rock Pack Vol 01" and the frame from "Construction Prop", by `Tools/import_rocks.py`), and trees and bushes of the Fab pack "Trees and bush Pack LOWPOLY" (`Tools/import_trees.py`). A rock is solid; a tree has a trunk that is what a hero or a shot meets, and a bush stops nothing. Past each plot's rim, out to the shore, are more rocks, stony patches and groves. How many of each stand on a plot is in `Config/DefaultGame.ini`. Round the arena is a country of mountains and a lake: the Fab model "Trident Lakes 211111 01 Scene Output 4096", 11 km square, brought into `Content/Scenery/Land` by `Tools/import_land.py` (the terrain cut to 64,000 triangles, the water to 38,294; colour and normal maps at 2048, occlusion and roughness at 1024; no collision, no Nanite). It is set 2.6 times its size (`LandScale`) with its biggest lake's widest open water where the arena stands, so the arena's land rises out of that lake and the mountains stand round it. It is seen and nothing else, and a match server with no screen does not load it. The map's haze lies on the lake and a fall ends 1.5 m under its surface; both are put there when the map is made, so after changing `Land` or `WaterDepth` make the map again (`Tools/build_arena.py`); the arena check fails on a map made for another depth. The arena is in the phone build and draws at 31 frames a second on the test phone, a Galaxy A16 (`Config/DefaultDeviceProfiles.ini` says what it costs and what was cut for it). Without a match the plots hold their weapon crates and dressing; in a match each holds its faction's built structures on its pad (`Source/Sennite/Public/SenniteMatchBases.h`).
- Weapon crates (`Source/Sennite/Public/SenniteCrate.h`): two stand by where heroes start on each base of an arena. A hero starts with nothing to shoot with. What a crate holds is inside it; nothing lies about. E beside a closed crate opens it and the hero has what it held: every gun that is theirs and that the hero does not have yet, rounds for each (90, 20, 120 and 10), a pack of bandages and of potions. The energy rifle, the sniper, the axe and the katana are a player's only if their wallet proved it owns them, and then from the start, with no crate; a crate keeps them and their rounds from anyone else, and the HUD says which it kept are in the store. A player's game is never taken at its word for it: once in, it shows the game server a pass, and the server asks the publisher's server what that wallet owns (`ASennitePlayerState::ServerClaimGuns`, `GunsOwned`). The first new gun is in the hero's hand at once and the HUD says what the crate gave. A hero who is building, firing, reloading or healing cannot open one. 45 s later it is closed and full again. A hero that falls leaves a crate where it fell holding the weapons it had: whoever opens it has them and it is gone, unopened it goes after a minute, and heroes walk through it so it never shuts a bridge. A crate is the long green ammunition crate of the Fab pack "Wooden Ammo Crates Pack", brought into `Content/Crate` by `Tools/import_crate.py`, scaled to 150 cm long. The model is one piece with no lid of its own, so an opened crate looks as a closed one does. Its numbers are in `Config/DefaultGame.ini` under `[/Script/Sennite.SenniteCrate]`.
- A match (`Source/Sennite/Public/SenniteMatch.h`): played on the publisher's match server, a game with no player of its own, between the factions the publisher's server opened it for. A player plays for the faction their wallet is a member of: their game gets a ticket from the publisher's server and shows it to the match server; until then they have no hero, and a game that shows none is put out. Heroes start on their faction's base, clear of what is built there. A shot counts only across factions and only while the match is being played, which is from when players of two factions are in. A hero that falls returns on its base 5 s later with nothing it picked up; the weapons it had are in a crate left where it fell, for whoever comes by. A wall, ramp or floor built in a match is its builder's faction's: a shot wears it down only from another faction, while the match is being played. The match ends when a faction is first to 10 kills; or when its 10 minutes run out, for the faction with the most kills, or for nobody when the most is shared; or when a faction's base is wiped, every structure of it destroyed: that faction is out, its players watch, and the last faction left wins. The HUD shows the time left, each faction's kills and how it ended. The match server tells the publisher's server who won and closes, and the winners' prizes are paid on DSM. The match server picks the arena's seed when its address names none. The numbers are in `Config/DefaultGame.ini` under `[/Script/Sennite.SenniteMatch]`. The menus have no way into a faction match yet: a player's game is started on the match server's address (`Server/README.md`).
- Healing: `ASenniteBandage` (a pack of 5; each gives back 15 health) and `ASennitePotion` (2; each adds 20 shield), in `Source/Sennite/Public/SenniteHealing.h`. They come out of crates; 4 and 5 bring them to hand, the left mouse button uses one, which takes a little over three seconds. They have no models of their own yet: they are drawn as a plain box and a plain cylinder, named per item class in `Config/DefaultGame.ini`, and there is no animation for using one.

Third-party notices: `ThirdParty/NOTICE.md`.

"""Import the sounds the game plays into /Game/Sounds, from the Fab pack "Cyberpunk UI SFX Pack - 78
Sci-Fi Sound Effects".

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_sounds.py" -unattended -nullrhi

The pack's files are numbered, not named (sfx_001.wav to sfx_078.wav, in folders by kind), so which
file is which sound is a choice made here, from each file's length, how sharply it starts and how
high or low it is; nobody listened to them when this was written. To change a sound, change its
number below and run this again; what plays each sound is named in Config/DefaultGame.ini.

A shot is heard less the farther away it is fired (SA_Shot): at full strength within 4 m, gone by 90 m.

The pack is looked for in the Fab library folder the launcher downloads into, or SOUND_SOURCE."""
import glob
import os

import unreal

LIBRARY = "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary"
DEST = "/Game/Sounds"
# asset name: (the pack's folder, the file's number), and why that file
SOUNDS = {
    "SW_RifleShot": ("03_Weapons", 44),       # 0.17 s, starts at once, mid: a quick crack that can repeat four times a second
    "SW_ShotgunShot": ("03_Weapons", 39),     # 0.40 s, the lowest and longest of the sharp ones: a heavy blast
    "SW_EnergyShot": ("03_Weapons", 46),      # 0.18 s, the highest: a bright zap that can repeat ten times a second
    "SW_SniperShot": ("03_Weapons", 47),      # 0.28 s, sharp and bright with a tail: one big shot
    "SW_CrateOpen": ("04_Powerups", 61),      # 0.28 s, mid: something given
}

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def pack():
    named = os.environ.get("SOUND_SOURCE")
    hits = [named] if named else glob.glob(os.path.join(LIBRARY, "Cyberpunk_UI_SFX_Pack*", "*", "*", "FINAL_SFX_PACK"))
    if not hits or not os.path.isdir(hits[0]):
        raise RuntimeError("the Cyberpunk UI SFX Pack is not under " + (named or LIBRARY))
    return hits[0]


SOURCE = pack()
for name, (folder, number) in SOUNDS.items():
    path = os.path.join(SOURCE, folder, "sfx_%03d.wav" % number)
    if not os.path.isfile(path):
        raise RuntimeError("%s: %s is not in the pack" % (name, path))
    task = unreal.AssetImportTask()
    task.filename, task.destination_path, task.destination_name = path, DEST, name
    task.automated, task.replace_existing, task.save = True, True, True
    tools.import_asset_tasks([task])
    sound = eal.load_asset(DEST + "/" + name)
    if sound is None or not isinstance(sound, unreal.SoundWave):
        raise RuntimeError("%s did not import as a sound: %s" % (name, sound))
    unreal.log_warning("SOUNDS %s from %s/sfx_%03d.wav, %.2f s" % (name, folder, number, sound.get_editor_property("duration")))

# how a shot fades with distance
falloff_path = DEST + "/SA_Shot"
if eal.does_asset_exist(falloff_path):
    eal.delete_asset(falloff_path)
falloff = tools.create_asset("SA_Shot", DEST, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
settings = falloff.get_editor_property("attenuation")
settings.set_editor_property("attenuation_shape_extents", unreal.Vector(400.0, 0.0, 0.0))
settings.set_editor_property("falloff_distance", 8600.0)
falloff.set_editor_property("attenuation", settings)
if not eal.save_loaded_asset(falloff):
    raise RuntimeError("the shot's falloff did not save")
unreal.log_warning("SOUNDS SA_Shot saved")

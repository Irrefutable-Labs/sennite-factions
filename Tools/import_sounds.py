"""Import the sounds the game plays into /Game/Sounds.

  python3 Tools/cut_sounds.py
  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_sounds.py" -unattended -nullrhi

Most of them are cut by Tools/cut_sounds.py out of the owner's two sound packs (the Sonniss GDC 2024
bundle and the NOX SOUND Essentials series) into Saved/SoundSource/cut, with a list of them
(sounds.json): every one in the list is imported under its name, and an ambience is set to go round.

Two are from the Fab pack "Cyberpunk UI SFX Pack - 78 Sci-Fi Sound Effects" (the energy rifle's shot and
a crate opening). That pack's files are numbered, not named (sfx_001.wav to sfx_078.wav, in folders by
kind), so which file is which sound was chosen from each file's length, how sharply it starts and how
high or low it is. It is looked for in the Fab library folder the launcher downloads into, or
SOUND_SOURCE; where it is not found those two are left as they are in the project.

What plays each sound is named in Config/DefaultGame.ini (SenniteSounds, and each gun's FireSound).

How far a sound carries: a shot at full strength within 4 m and gone by 90 m (SA_Shot); what is heard
of a player close by, steps and hands and voice, within 1.5 m and gone by 30 m (SA_Near); what lands,
breaks or is built within 3 m and gone by 70 m (SA_Far)."""
import glob
import json
import os

import unreal

LIBRARY = "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary"
DEST = "/Game/Sounds"
CUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Saved", "SoundSource", "cut")
# asset name: (the Cyberpunk pack's folder, the file's number), and why that file
NUMBERED = {
    "SW_EnergyShot": ("03_Weapons", 46),      # 0.18 s, the highest: a bright zap that can repeat ten times a second
    "SW_CrateOpen": ("04_Powerups", 61),      # 0.28 s, mid: something given
}
# name: (full strength within, gone by) in cm
FALLOFFS = {"SA_Shot": (400.0, 9000.0), "SA_Near": (150.0, 3000.0), "SA_Far": (300.0, 7000.0)}

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def bring(path, name, loop=False):
    if not os.path.isfile(path):
        raise RuntimeError("%s: %s is not there" % (name, path))
    task = unreal.AssetImportTask()
    task.filename, task.destination_path, task.destination_name = path, DEST, name
    task.automated, task.replace_existing, task.save = True, True, True
    tools.import_asset_tasks([task])
    sound = eal.load_asset(DEST + "/" + name)
    if sound is None or not isinstance(sound, unreal.SoundWave):
        raise RuntimeError("%s did not import as a sound: %s" % (name, sound))
    if sound.get_editor_property("looping") != loop:
        sound.set_editor_property("looping", loop)
        if not eal.save_loaded_asset(sound):
            raise RuntimeError("%s did not save" % name)
    return sound


with open(os.path.join(CUT, "sounds.json")) as listing:
    cut = json.load(listing)
for each in cut:
    sound = bring(os.path.join(CUT, each["file"]), each["asset"], each["loop"])
    unreal.log_warning("SOUNDS %s from %s, %.2f s%s" % (each["asset"], each["from"], sound.get_editor_property("duration"),
                                                        ", goes round" if each["loop"] else ""))

named = os.environ.get("SOUND_SOURCE")
packs = [named] if named else glob.glob(os.path.join(LIBRARY, "Cyberpunk_UI_SFX_Pack*", "*", "*", "FINAL_SFX_PACK"))
if packs and os.path.isdir(packs[0]):
    for name, (folder, number) in NUMBERED.items():
        sound = bring(os.path.join(packs[0], folder, "sfx_%03d.wav" % number), name)
        unreal.log_warning("SOUNDS %s from %s/sfx_%03d.wav, %.2f s" % (name, folder, number, sound.get_editor_property("duration")))
else:
    for name in NUMBERED:
        if not eal.does_asset_exist(DEST + "/" + name):
            raise RuntimeError("%s is not in the project and the Cyberpunk UI SFX Pack is not under %s" % (name, named or LIBRARY))
    unreal.log_warning("SOUNDS the Cyberpunk UI SFX Pack is not here: %s left as they are" % ", ".join(NUMBERED))

# how each kind of sound fades with distance
for name, (full, gone) in FALLOFFS.items():
    path = DEST + "/" + name
    falloff = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(name, DEST, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    if falloff is None or not isinstance(falloff, unreal.SoundAttenuation):
        raise RuntimeError("%s is not a falloff: %s" % (name, falloff))
    settings = falloff.get_editor_property("attenuation")
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(full, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", gone - full)
    falloff.set_editor_property("attenuation", settings)
    if not eal.save_loaded_asset(falloff):
        raise RuntimeError("%s did not save" % name)
    unreal.log_warning("SOUNDS %s saved: full within %.0f cm, gone by %.0f cm" % (name, full, gone))
unreal.log_warning("SOUNDS done: %d cut sounds" % len(cut))

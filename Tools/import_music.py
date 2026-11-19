"""Import the menu's songs into /Game/Music, from the owner's own recordings: "Come With Me" and
"Demon Mode", as he gave them (two .wav files, 16-bit stereo).

  MUSIC_SOURCE=<folder holding the two .wav files> \\
  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_music.py" -unattended -nullrhi

Each becomes one sound, whole and unchanged; a phone is given it compressed, as it is every sound.
Which songs the menu plays, in which order and how loud, is in Config/DefaultGame.ini
([/Script/Sennite.SenniteMenuController]). The files are looked for in MUSIC_SOURCE, or in the
owner's Downloads folder, where he put them."""
import os

import unreal

SOURCE = os.environ.get("MUSIC_SOURCE", os.path.expanduser("~/Downloads"))
DEST = "/Game/Music"
# asset name: the owner's file
SONGS = {
    "SW_ComeWithMe": "Come With Me.wav",
    "SW_DemonMode": "Demon Mode.wav",
}

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

for name, file in SONGS.items():
    path = os.path.join(SOURCE, file)
    if not os.path.isfile(path):
        raise RuntimeError("%s: %s is not there" % (name, path))
    task = unreal.AssetImportTask()
    task.filename, task.destination_path, task.destination_name = path, DEST, name
    task.automated, task.replace_existing, task.save = True, True, True
    tools.import_asset_tasks([task])
    sound = eal.load_asset(DEST + "/" + name)
    if sound is None or not isinstance(sound, unreal.SoundWave):
        raise RuntimeError("%s did not import as a sound: %s" % (name, sound))
    unreal.log_warning("MUSIC %s from %s, %.1f s, %d channels at %d Hz" % (
        name, file, sound.get_editor_property("duration"), sound.get_editor_property("num_channels"), int(sound.get_editor_property("sample_rate"))))

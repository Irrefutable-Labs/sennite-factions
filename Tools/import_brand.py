"""Import the start screens into /Game/Brand, from the owner's images, shown one after the other before the
first menu, after the engine's own start screen (SenniteMenu.h):

  Brand/irrefutable_labs.png        the studio's logo, Irrefutable Labs (1024 x 1024, white on black)
  Brand/sennite_startup_screen.png  the game's title screen, Sennite Factions (1672 x 941)

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_brand.py" -unattended -nullrhi

Each becomes one texture for the screen: no mipmaps, user-interface filtering, kept sharp on a phone."""
import os

import unreal

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = "/Game/Brand"
# asset name: the owner's image
IMAGES = {
    "T_IrrefutableLabs": "irrefutable_labs.png",
    "T_SenniteStartup": "sennite_startup_screen.png",
}

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

for name, file in IMAGES.items():
    source = os.path.join(HERE, "Brand", file)
    if not os.path.isfile(source):
        raise RuntimeError("%s is not there: %s" % (name, source))
    task = unreal.AssetImportTask()
    task.filename, task.destination_path, task.destination_name = source, DEST, name
    task.automated, task.replace_existing, task.save = True, True, True
    tools.import_asset_tasks([task])
    tex = eal.load_asset("%s/%s" % (DEST, name))
    if tex is None:
        raise RuntimeError("did not import: " + name)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("never_stream", True)
    eal.save_loaded_asset(tex)
    unreal.log_warning("BRAND %s: %d x %d" % (name, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

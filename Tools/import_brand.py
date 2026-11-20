"""Import the studio's logo into /Game/Brand: the owner's Irrefutable Labs mark (Brand/irrefutable_labs.png,
1024 x 1024, white on black), shown full screen for a few seconds before the first menu (SenniteMenu.h).

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_brand.py" -unattended -nullrhi

It becomes one texture for the screen: no mipmaps, user-interface filtering, kept sharp on a phone."""
import os

import unreal

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(HERE, "Brand", "irrefutable_labs.png")
DEST = "/Game/Brand"
NAME = "T_IrrefutableLabs"

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

if not os.path.isfile(SOURCE):
    raise RuntimeError("the logo is not there: " + SOURCE)
task = unreal.AssetImportTask()
task.filename, task.destination_path, task.destination_name = SOURCE, DEST, NAME
task.automated, task.replace_existing, task.save = True, True, True
tools.import_asset_tasks([task])
tex = eal.load_asset("%s/%s" % (DEST, NAME))
if tex is None:
    raise RuntimeError("the logo did not import")
tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
tex.set_editor_property("never_stream", True)
eal.save_loaded_asset(tex)
unreal.log_warning("BRAND %s: %d x %d" % (NAME, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

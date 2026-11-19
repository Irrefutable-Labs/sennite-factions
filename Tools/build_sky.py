"""Make what the night's stars are drawn with: /Game/Sky/M_Stars.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/build_sky.py" -unattended -nullrhi

The engine's own star field (its sky's T_Sky_Stars), added to what is behind it and taking no light,
"Brightness" times as bright. USenniteSky draws it on a dome just inside the sky's own, at night
(Source/Sennite/Public/SenniteSky.h): the land hides the stars and the sky does not. STAR_TILES is how
many times the field goes round the dome and STAR_TILES_UP how many times up it: the dome's own
mapping is wider round than up, so more round keeps a star a point and not a dash."""
import os

import unreal

DEST = "/Game/Sky"
NAME = "M_Stars"
TILES = float(os.environ.get("STAR_TILES", "20"))
UP = float(os.environ.get("STAR_TILES_UP", "6"))

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

stars = unreal.load_asset("/Engine/EngineSky/T_Sky_Stars")
if stars is None:
    raise RuntimeError("the engine's star field did not load")
path = DEST + "/" + NAME
if eal.does_asset_exist(path):
    eal.delete_asset(path)
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DEST, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
mat.set_editor_property("two_sided", True)
where = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, 0)
where.set_editor_property("u_tiling", TILES)
where.set_editor_property("v_tiling", UP)
field = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -650, 0)
field.set_editor_property("texture", stars)
bright = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -650, 300)
bright.set_editor_property("parameter_name", "Brightness")
bright.set_editor_property("default_value", 1.0)
lit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, 100)
if not (mel.connect_material_expressions(where, "", field, "UVs")
        and mel.connect_material_expressions(field, "RGB", lit, "A")
        and mel.connect_material_expressions(bright, "", lit, "B")
        and mel.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)):
    raise RuntimeError("the stars' material did not wire up")
mel.recompile_material(mat)
if not eal.save_loaded_asset(mat):
    raise RuntimeError("the stars' material did not save")
unreal.log_warning("SKY saved %s, the star field %d by %d, %g times round and %g up" % (path, stars.blueprint_get_size_x(), stars.blueprint_get_size_y(), TILES, UP))

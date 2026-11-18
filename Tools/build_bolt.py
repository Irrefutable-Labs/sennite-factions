"""Make what a bullet's bolt is drawn with: /Game/Weapons/M_Bolt.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/build_bolt.py" -unattended -nullrhi

A bolt is a bright streak that takes no light: its colour ("Color", set per bullet class in
DefaultGame.ini) times "Glow", so it reads as hot on a phone, which has no bloom, as on a desktop.
AProjectileActor shapes the streak from the engine's cube and gives each bolt its colour."""
import unreal

DEST = "/Game/Weapons"
NAME = "M_Bolt"

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

path = DEST + "/" + NAME
if eal.does_asset_exist(path):
    eal.delete_asset(path)
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DEST, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
colour = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 0)
colour.set_editor_property("parameter_name", "Color")
colour.set_editor_property("default_value", unreal.LinearColor(1.0, 0.5, 0.1, 1.0))
glow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 250)
glow.set_editor_property("parameter_name", "Glow")
glow.set_editor_property("default_value", 6.0)
bright = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 100)
if not (mel.connect_material_expressions(colour, "", bright, "A")
        and mel.connect_material_expressions(glow, "", bright, "B")
        and mel.connect_material_property(bright, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)):
    raise RuntimeError("the bolt's material did not wire up")
mel.recompile_material(mat)
if not eal.save_loaded_asset(mat):
    raise RuntimeError("the bolt's material did not save")
unreal.log_warning("BOLT saved " + path)

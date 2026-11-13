"""Import the guns the game uses from the Fab pack "Low Poly Weapons VOL.1" into /Game/Weapons.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/import_weapons.py -unattended -nullrhi

Each gun comes in as one static mesh (its bolt, magazine and trigger merged in), with a simple collision
box so a player can walk over it to pick it up. The pack's models all use one texture, Textures_Guns.png.
When that file is in the pack folder the guns' material uses it; until then the material is plain gunmetal.
The pack folder is WEAPON_SOURCE, or the Fab library folder the launcher downloads into."""
import glob
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get("WEAPON_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary/Low_Poly_Weapons_VOL_1-a1dde692")
DEST = "/Game/Weapons"
GUNS = {"SK_M4_8": "SM_M4", "SK_SG_M4": "SM_Shotgun"}      # pack file -> asset name

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log("WEAPONS " + str(msg))


def find(name):
    hits = glob.glob(os.path.join(SOURCE, "**", name), recursive=True)
    return hits[0] if hits else None


# ---------------------------------------------------------------------------- the one material all guns share
texture_file = find("Textures_Guns.png")
texture = None
if texture_file:
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = texture_file, DEST, "T_Guns"
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    texture = eal.load_asset(DEST + "/T_Guns")
log("texture: " + (texture_file if texture else "Textures_Guns.png is not in the pack folder; plain gunmetal until it is"))

if eal.does_asset_exist(DEST + "/M_Guns"):
    eal.delete_asset(DEST + "/M_Guns")
mat = tools.create_asset("M_Guns", DEST, unreal.Material, unreal.MaterialFactoryNew())
if texture:
    sample = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -400, 0)
    sample.set_editor_property("texture", texture)
    mel.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    rough_value, metal_value = 0.7, 0.0
else:
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    colour.set_editor_property("constant", unreal.LinearColor(0.045, 0.047, 0.05, 1.0))
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough_value, metal_value = 0.45, 0.85
for prop, value, y in ((unreal.MaterialProperty.MP_ROUGHNESS, rough_value, 200), (unreal.MaterialProperty.MP_METALLIC, metal_value, 300)):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, y)
    node.set_editor_property("r", value)
    mel.connect_material_property(node, "", prop)
mel.recompile_material(mat)
eal.save_loaded_asset(mat)

# ---------------------------------------------------------------------------- the guns
for source_name, asset_name in GUNS.items():
    path = find(source_name + ".fbx")
    if path is None:
        raise RuntimeError(source_name + ".fbx is not under " + SOURCE)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("auto_generate_collision", True)
    data.set_editor_property("generate_lightmap_u_vs", False)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = path, DEST, asset_name
    t.automated, t.replace_existing, t.save = True, True, True
    t.options = ui
    tools.import_asset_tasks([t])
    mesh = eal.load_asset(DEST + "/" + asset_name)
    if mesh is None or not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(asset_name + " did not import as a static mesh: " + str(mesh))
    nanite = mesh.get_editor_property("nanite_settings")
    if nanite.enabled:                      # plain meshes: phones have no Nanite
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)
    for slot in range(len(mesh.get_editor_property("static_materials"))):
        mesh.set_material(slot, mat)
    eal.save_loaded_asset(mesh)
    box = mesh.get_bounding_box()
    log("%s: min (%.1f, %.1f, %.1f) max (%.1f, %.1f, %.1f) cm, %d material slots, %d triangles" % (
        asset_name, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z,
        len(mesh.get_editor_property("static_materials")), mesh.get_num_triangles(0)))

with open(os.path.join(ROOT, "Saved", "import_weapons.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

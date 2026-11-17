"""Make the building pieces' materials from the Fab pack "+10 2K PBR SciFi textures 4-FREE" in /Game/Building.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/import_building.py -unattended -nullrhi

One panel per face of a piece. The three strengths of piece the game has are shown by three panels
(weakest first), and floors have their own:
  0 CeilingPlastic   1 MilitaryHull   2 Blastwall1   floor FloorCargo
The textures come in at 1024, a phone's size, without the ambient occlusion map. The pack folder is
PANEL_SOURCE, or the Fab library folder the launcher downloads into."""
import glob
import os
import subprocess

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get("PANEL_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary/_10_2K_PBR_SciFi_textures_4-FREE-47437f69")
SMALL = os.path.join(ROOT, "Saved", "BuildingImport")
DEST = "/Game/Building"
SETS = ["CeilingPlastic", "MilitaryHull", "Blastwall1", "FloorCargo"]
MAPS = ["Albedo", "Normal", "Rough", "Metal"]
SIZE = 1024

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty


def log(msg):
    unreal.log("BUILDING " + str(msg))


def source(name):
    hits = glob.glob(os.path.join(SOURCE, "**", name), recursive=True)
    if not hits:
        raise RuntimeError(name + " is not under " + SOURCE)
    return hits[0]


# ---------------------------------------------------------------------------- textures, at a phone's size
os.makedirs(SMALL, exist_ok=True)
tasks = []
for s in SETS:
    for m in MAPS:
        name = "%s_%s.png" % (s, m)
        small = os.path.join(SMALL, name)
        subprocess.run(["/usr/bin/sips", "-Z", str(SIZE), source(name), "--out", small], check=True, capture_output=True)
        t = unreal.AssetImportTask()
        t.filename, t.destination_path, t.destination_name = small, DEST + "/Textures", "T_%s_%s" % (s, m)
        t.automated, t.replace_existing, t.save = True, True, True
        tasks.append(t)
tools.import_asset_tasks(tasks)

textures = {}
for s in SETS:
    for m in MAPS:
        tex = eal.load_asset("%s/Textures/T_%s_%s" % (DEST, s, m))
        if tex is None:
            raise RuntimeError("texture did not import: %s %s" % (s, m))
        if m == "Normal":
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif m in ("Rough", "Metal"):
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        eal.save_loaded_asset(tex)
        textures[(s, m)] = tex
log("textures: %d at %d px" % (len(textures), SIZE))


def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


# ---------------------------------------------------------------------------- one material, an instance per panel
fresh(DEST + "/M_Panel")
panel = tools.create_asset("M_Panel", DEST, unreal.Material, unreal.MaterialFactoryNew())
samplers = {"Albedo": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "Normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
            "Rough": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "Metal": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}
pins = {"Albedo": ("RGB", P.MP_BASE_COLOR), "Normal": ("RGB", P.MP_NORMAL), "Rough": ("R", P.MP_ROUGHNESS), "Metal": ("R", P.MP_METALLIC)}
for i, m in enumerate(MAPS):
    node = mel.create_material_expression(panel, unreal.MaterialExpressionTextureSampleParameter2D, -500, i * 260)
    node.set_editor_property("parameter_name", m)
    node.set_editor_property("texture", textures[(SETS[0], m)])
    node.set_editor_property("sampler_type", samplers[m])
    if not mel.connect_material_property(node, pins[m][0], pins[m][1]):
        raise RuntimeError("panel material: could not connect " + m)
# pieces are drawn a tile an instance (UInstancedStaticMeshComponent); a cooked game cannot add the
# usage itself and would draw the engine's default material instead
panel.set_editor_property("used_with_instanced_static_meshes", True)
mel.recompile_material(panel)
eal.save_loaded_asset(panel)

for s in SETS:
    fresh("%s/MI_Panel_%s" % (DEST, s))
    mi = tools.create_asset("MI_Panel_" + s, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, panel)
    values = []
    for m in MAPS:
        info = unreal.MaterialParameterInfo()
        info.set_editor_property("name", m)
        value = unreal.TextureParameterValue()
        value.set_editor_property("parameter_info", info)
        value.set_editor_property("parameter_value", textures[(s, m)])
        values.append(value)
    mi.set_editor_property("texture_parameter_values", values)
    mel.update_material_instance(mi)
    for m in MAPS:
        if mel.get_material_instance_texture_parameter_value(mi, m) != textures[(s, m)]:
            raise RuntimeError("panel %s: %s did not take" % (s, m))
    eal.save_loaded_asset(mi)
    log("panel " + s)

# ---------------------------------------------------------------------------- the preview: a see-through blue piece
fresh(DEST + "/M_PiecePreview")
preview = tools.create_asset("M_PiecePreview", DEST, unreal.Material, unreal.MaterialFactoryNew())
preview.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
preview.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
colour = mel.create_material_expression(preview, unreal.MaterialExpressionConstant3Vector, -400, 0)
colour.set_editor_property("constant", unreal.LinearColor(0.10, 0.45, 1.0, 1.0))
opacity = mel.create_material_expression(preview, unreal.MaterialExpressionConstant, -400, 200)
opacity.set_editor_property("r", 0.35)
if not (mel.connect_material_property(colour, "", P.MP_EMISSIVE_COLOR) and mel.connect_material_property(opacity, "", P.MP_OPACITY)):
    raise RuntimeError("preview material did not wire up")
# the see-through tiles of a piece being edited are instances too
preview.set_editor_property("used_with_instanced_static_meshes", True)
mel.recompile_material(preview)
eal.save_loaded_asset(preview)
for made in (panel, preview):
    if not made.get_editor_property("used_with_instanced_static_meshes"):
        raise RuntimeError(made.get_name() + ": instanced use did not take")
log("done")

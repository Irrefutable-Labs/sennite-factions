"""Import the scenery the arena is dressed with, from Fab packs in the launcher's library folder,
into /Game/Scenery.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/import_scenery.py -unattended -nullrhi

Each piece comes in as one static mesh with no collision (the arena's blocks are what a player stands
on; scenery is only seen), with its colour, normal and roughness textures at 1024, a phone's size.
  Cliff        Desert Western Cliff Layered XXL 02   the canyon's walls
  RockyGround  Forest Rocky Ground                   scattered on floors
What each measures is written to Saved/import_scenery.txt. The library folder is SCENERY_SOURCE."""
import glob
import os
import subprocess

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get("SCENERY_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
DEST = "/Game/Scenery"
SIZE = 1024
PIECES = {
    "Cliff": "Desert_Western_Cliff_Layered_XXL_02-*/fbx/mid/*/",
    "RockyGround": "Forest_Rocky_Ground-*/fbx/mid/*/",
}

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
lines = []


def one(pattern):
    hits = glob.glob(os.path.join(SOURCE, pattern))
    if not hits:
        raise RuntimeError("nothing matches " + pattern + " under " + SOURCE)
    return hits[0]


def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


def texture(piece, folder, kind):
    """One of a piece's maps, shrunk to a phone's size and imported."""
    src = one(os.path.join(PIECES[piece], "*_" + kind + ".jpg"))
    small = os.path.join(SMALL, "%s_%s.jpg" % (piece, kind))
    subprocess.run(["/usr/bin/sips", "-Z", str(SIZE), src, "--out", small], check=True, capture_output=True)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = small, folder, "T_%s_%s" % (piece, kind)
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    tex = eal.load_asset("%s/T_%s_%s" % (folder, piece, kind))
    if tex is None:
        raise RuntimeError("texture did not import: %s %s" % (piece, kind))
    if kind == "Normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "Roughness":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


os.makedirs(SMALL, exist_ok=True)
for piece in PIECES:
    folder = "%s/%s" % (DEST, piece)
    maps = {kind: texture(piece, folder, kind) for kind in ("BaseColor", "Normal", "Roughness")}

    fresh("%s/M_%s" % (folder, piece))
    mat = tools.create_asset("M_" + piece, folder, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    for i, (kind, sampler, pin, prop) in enumerate((
            ("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "RGB", P.MP_BASE_COLOR),
            ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "RGB", P.MP_NORMAL),
            ("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "R", P.MP_ROUGHNESS))):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -500, i * 260)
        node.set_editor_property("texture", maps[kind])
        node.set_editor_property("sampler_type", sampler)
        if not mel.connect_material_property(node, pin, prop):
            raise RuntimeError("%s material: could not connect %s" % (piece, kind))
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)

    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("generate_lightmap_u_vs", False)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = one(os.path.join(PIECES[piece], "*.fbx")), folder, "SM_" + piece
    t.automated, t.replace_existing, t.save = True, True, True
    t.options = ui
    tools.import_asset_tasks([t])
    mesh = eal.load_asset("%s/SM_%s" % (folder, piece))
    if mesh is None or not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(piece + " did not import as a static mesh: " + str(mesh))
    nanite = mesh.get_editor_property("nanite_settings")
    if nanite.enabled:                      # plain meshes: phones have no Nanite
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)
    for slot in range(len(mesh.get_editor_property("static_materials"))):
        mesh.set_material(slot, mat)
    eal.save_loaded_asset(mesh)
    box = mesh.get_bounding_box()
    lines.append("%s: x %.0f to %.0f, y %.0f to %.0f, z %.0f to %.0f cm; %d triangles; %d material slots" % (
        piece, box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z, mesh.get_num_triangles(0),
        len(mesh.get_editor_property("static_materials"))))

with open(os.path.join(ROOT, "Saved", "import_scenery.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

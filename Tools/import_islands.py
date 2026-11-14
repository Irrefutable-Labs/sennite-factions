"""Bring a flying island from the Fab pack "Flying Islands Free Set" into /Game/Scenery/Island, cut down
for a phone.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/import_islands.py -unattended -nullrhi

Each island in the pack is one mesh of about 1.5 million triangles with about 45 MB of textures,
modelled about a metre across: a rock with terraced ground, a few trees and boulders on top. The
arena uses it as the rock under a plot, which is hundreds of times that size, so its top is cut off
just under its lowest ground (GROUND, as a share of its height from the bottom) and only the rock
below is kept; its flat cut top sits under the plot's floor. Here it is imported once into a scratch
folder, cut, simplified to TRIANGLES with the engine's own geometry scripting, moved so that the cut
is at height 0 and its middle is over the origin, and written as a new static mesh without
collision; its colour, normal and roughness-metal textures are taken out of the file and shrunk to
1024. The scratch folder is deleted, so only the small result is kept.
What it measures is written to Saved/import_islands.txt. The pack folder is ISLAND_SOURCE; ISLAND is
which of the pack's files (0, 1 or 2)."""
import glob
import json
import os
import struct
import subprocess
import traceback

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get("ISLAND_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary/Flying_Islands_Free_Set__-6f608004")
WHICH = int(os.environ.get("ISLAND", "0"))
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
SCRATCH = "/Game/ZZ_ImportScratch"
DEST = "/Game/Scenery/Island"
TRIANGLES = 12000
# The first island's ground starts half way up it (measured: its lowest terrace is at 0.50 of its height).
GROUND = float(os.environ.get("ISLAND_GROUND", "0.49"))
SIZE = 1024

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
lines = []


def note(msg):
    lines.append(str(msg))
    with open(os.path.join(ROOT, "Saved", "import_islands.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


def glb_images(path):
    """The file's textures by what the material uses them for, written out and shrunk."""
    with open(path, "rb") as f:
        f.read(12)
        length, _kind = struct.unpack("<II", f.read(8))
        doc = json.loads(f.read(length))
        blob_length, _kind = struct.unpack("<II", f.read(8))
        blob_start = f.tell()
        material = doc["materials"][0]
        pbr = material.get("pbrMetallicRoughness", {})
        uses = {"BaseColor": pbr.get("baseColorTexture"), "Normal": material.get("normalTexture"), "RoughMetal": pbr.get("metallicRoughnessTexture")}
        out = {}
        for use, ref in uses.items():
            if ref is None:
                continue
            image = doc["images"][doc["textures"][ref["index"]]["source"]]
            view = doc["bufferViews"][image["bufferView"]]
            f.seek(blob_start + view.get("byteOffset", 0))
            data = f.read(view["byteLength"])
            ext = "png" if image.get("mimeType") == "image/png" else "jpg"
            raw = os.path.join(SMALL, "Island_%s_full.%s" % (use, ext))
            with open(raw, "wb") as fh:
                fh.write(data)
            small = os.path.join(SMALL, "Island_%s.png" % use)
            subprocess.run(["/usr/bin/sips", "-s", "format", "png", "-Z", str(SIZE), raw, "--out", small], check=True, capture_output=True)
            os.remove(raw)
            out[use] = small
        return out


def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


try:
    os.makedirs(SMALL, exist_ok=True)
    files = sorted(glob.glob(os.path.join(SOURCE, "glb", "*", "*.glb")))
    if not files:
        raise RuntimeError("no .glb under " + SOURCE)
    source = files[WHICH % len(files)]
    note("source: %s (%.0f MB)" % (os.path.basename(source), os.path.getsize(source) / 1e6))

    # ------------------------------------------------------------------ textures, at a phone's size
    textures = {}
    for use, path in glb_images(source).items():
        t = unreal.AssetImportTask()
        t.filename, t.destination_path, t.destination_name = path, DEST, "T_Island_" + use
        t.automated, t.replace_existing, t.save = True, True, True
        tools.import_asset_tasks([t])
        tex = eal.load_asset("%s/T_Island_%s" % (DEST, use))
        if tex is None:
            raise RuntimeError("texture did not import: " + use)
        if use == "Normal":
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif use == "RoughMetal":
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        eal.save_loaded_asset(tex)
        textures[use] = tex
    note("textures: " + ", ".join(sorted(textures)))

    fresh(DEST + "/M_Island")
    mat = tools.create_asset("M_Island", DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    wiring = (("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, (("RGB", P.MP_BASE_COLOR),)),
              ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, (("RGB", P.MP_NORMAL),)),
              # glTF: roughness in green, metal in blue
              ("RoughMetal", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, (("G", P.MP_ROUGHNESS), ("B", P.MP_METALLIC))))
    for i, (use, sampler, pins) in enumerate(wiring):
        if use not in textures:
            continue
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -500, i * 260)
        node.set_editor_property("texture", textures[use])
        node.set_editor_property("sampler_type", sampler)
        for pin, prop in pins:
            if not mel.connect_material_property(node, pin, prop):
                raise RuntimeError("island material: could not connect " + use)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)

    # ------------------------------------------------------------------ the mesh, once, into a scratch folder
    if eal.does_directory_exist(SCRATCH):
        eal.delete_directory(SCRATCH)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path = source, SCRATCH
    t.automated, t.replace_existing, t.save = True, True, False
    tools.import_asset_tasks([t])
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    heavy = None
    for data in registry.get_assets_by_path(SCRATCH, recursive=True):
        if str(data.asset_class_path.asset_name) == "StaticMesh":
            heavy = data.get_asset()
    if heavy is None:
        raise RuntimeError("the island did not import as a static mesh")
    box = heavy.get_bounding_box()
    note("as imported: %d triangles; x %.1f to %.1f, y %.1f to %.1f, z %.1f to %.1f cm" % (
        heavy.get_num_triangles(0), box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z))

    # ------------------------------------------------------------------ cut down, and written as a new mesh
    work = unreal.DynamicMesh()
    copied = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(heavy, work, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    note("copied: " + str(copied[1] if isinstance(copied, tuple) else copied))
    # the top comes off: everything above the plane goes, and the cut is closed
    cut_at = box.min.z + (box.max.z - box.min.z) * GROUND
    cut = unreal.GeometryScriptMeshPlaneCutOptions()
    cut.set_editor_property("fill_holes", True)
    unreal.GeometryScript_MeshBooleans.apply_mesh_plane_cut(work, unreal.Transform(location=unreal.Vector(0.0, 0.0, cut_at)), cut)
    kept = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(work)
    if kept.max.z > cut_at + 0.5:
        raise RuntimeError("the cut kept the top, not the rock: z %.1f to %.1f, cut at %.1f" % (kept.min.z, kept.max.z, cut_at))
    unreal.GeometryScript_MeshSimplification.apply_simplify_to_triangle_count(work, TRIANGLES, unreal.GeometryScriptSimplifyMeshOptions())
    kept = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(work)
    unreal.GeometryScript_MeshTransforms.translate_mesh(work, unreal.Vector(-(kept.min.x + kept.max.x) * 0.5, -(kept.min.y + kept.max.y) * 0.5, -kept.max.z))
    fresh(DEST + "/SM_Island")
    made = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(work, DEST + "/SM_Island", unreal.GeometryScriptCreateNewStaticMeshAssetOptions())
    mesh = made[0] if isinstance(made, tuple) else made
    if mesh is None or not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError("the cut-down island was not written: " + str(made))
    nanite = mesh.get_editor_property("nanite_settings")
    if nanite.enabled:                      # plain meshes: phones have no Nanite
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)
    for slot in range(max(1, len(mesh.get_editor_property("static_materials")))):
        mesh.set_material(slot, mat)
    unreal.EditorStaticMeshLibrary.remove_collisions(mesh)
    eal.save_loaded_asset(mesh)
    box = mesh.get_bounding_box()
    note("cut down: %d triangles; x %.1f to %.1f, y %.1f to %.1f, z %.1f to %.1f cm; %d material slots" % (
        mesh.get_num_triangles(0), box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z, len(mesh.get_editor_property("static_materials"))))

    eal.delete_directory(SCRATCH)
    note("done")
except Exception:
    note("FAILED:\n" + traceback.format_exc())
    raise

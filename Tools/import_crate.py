"""Import the weapon crate's model from the Fab pack "Wooden Ammo Crates Pack" (JMFeria) into /Game/Crate.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_crate.py" -unattended -nullrhi

The pack has four crates. The game uses the first, the long green one with a carrying handle: the size
of crate a rifle comes in. It comes in as one static mesh, SM_AmmoCrate (its handle and latches merged
in), with a simple collision shape so it is solid, and no Nanite: phones have none. Its three parts
keep the pack's own textures, each set shrunk to a phone's size: the crate's at 1024, the handle's and
the latches' at 512. The pack's Unreal texture sets are used as they are: colour, normal, and one map
holding occlusion, roughness and metal.

The model is one piece: it has no lid of its own to swing back.

The pack folder is CRATE_SOURCE, or the Fab library folder the launcher downloads into."""
import glob
import os
import subprocess

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary"
SMALL = os.path.join(ROOT, "Saved", "CrateImport")
DEST = "/Game/Crate"
MODEL = "AmmoCrate01"
# The model's material slots, by the names the pack gave them, and for each the texture set it wears
# and the size that set comes in at.
PARTS = {
    "Paredes_Piso": ("Crate", 1024),
    "EscalerasVarandas": ("Handle", 512),
    "Columnas1_2Piso": ("Padlocks", 512),
}

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log("CRATE " + str(msg))


def pack():
    named = os.environ.get("CRATE_SOURCE")
    hits = [named] if named else glob.glob(os.path.join(LIBRARY, "Wooden_Ammo_Crates_Pack-*", "fbx", "*", "JMFeria_AmmoCrates"))
    if not hits or not os.path.isdir(hits[0]):
        raise RuntimeError("the Wooden Ammo Crates Pack is not under " + (named or LIBRARY))
    return hits[0]


SOURCE = pack()
os.makedirs(SMALL, exist_ok=True)


def texture(part, kind, size):
    """One map of one part's texture set, shrunk and imported."""
    src = os.path.join(SOURCE, "Textures", "Ammo_Crate_01", "Textures_UE", "%s_%s_%s.png" % (MODEL, part, kind))
    if not os.path.isfile(src):
        raise RuntimeError("the pack has no " + src)
    small = os.path.join(SMALL, "%s_%s.png" % (part, kind))
    subprocess.run(["/usr/bin/sips", "-Z", str(size), src, "--out", small], check=True, capture_output=True)
    name = "T_Crate_%s_%s" % (part, kind)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = small, DEST, name
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    tex = eal.load_asset("%s/%s" % (DEST, name))
    if tex is None:
        raise RuntimeError("texture did not import: " + name)
    if kind == "Normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "OcclusionRoughnessMetallic":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


def material(part, size):
    """One part's material: the pack's colour, normal, and occlusion / roughness / metal."""
    name = "M_Crate_" + part
    path = "%s/%s" % (DEST, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mat = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())

    def sample(kind, sampler, y):
        read = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -500, y)
        read.set_editor_property("texture", texture(part, kind, size))
        read.set_editor_property("sampler_type", sampler)
        return read

    colour = sample("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -300)
    normal = sample("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 0)
    packed = sample("OcclusionRoughnessMetallic", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 300)
    for src, out, prop in ((colour, "RGB", P.MP_BASE_COLOR), (normal, "RGB", P.MP_NORMAL), (packed, "R", P.MP_AMBIENT_OCCLUSION),
                           (packed, "G", P.MP_ROUGHNESS), (packed, "B", P.MP_METALLIC)):
        if not mel.connect_material_property(src, out, prop):
            raise RuntimeError("could not connect a map of " + name)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


# ---------------------------------------------------------------------------- the model
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
t.filename, t.destination_path, t.destination_name = os.path.join(SOURCE, "Models", MODEL + ".fbx"), DEST, "SM_AmmoCrate"
t.automated, t.replace_existing, t.save = True, True, True
t.options = ui
tools.import_asset_tasks([t])
mesh = eal.load_asset(DEST + "/SM_AmmoCrate")
if mesh is None or not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError("SM_AmmoCrate did not import as a static mesh: " + str(mesh))
nanite = mesh.get_editor_property("nanite_settings")
if nanite.enabled:
    nanite.enabled = False
    mesh.set_editor_property("nanite_settings", nanite)

slots = [str(slot.get_editor_property("material_slot_name")) for slot in mesh.get_editor_property("static_materials")]
log("slots: " + ", ".join(slots))
for index, slot in enumerate(slots):
    if slot not in PARTS:
        raise RuntimeError("the model has a material slot this script does not know: %s (it knows %s)" % (slot, ", ".join(PARTS)))
    part, size = PARTS[slot]
    mesh.set_material(index, material(part, size))
    log("slot %d %s: the pack's %s textures at %d px" % (index, slot, part, size))
eal.save_loaded_asset(mesh)

box = mesh.get_bounding_box()
log("SM_AmmoCrate: min (%.1f, %.1f, %.1f) max (%.1f, %.1f, %.1f) cm, %d triangles" % (
    box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z, mesh.get_num_triangles(0)))

with open(os.path.join(ROOT, "Saved", "import_crate.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

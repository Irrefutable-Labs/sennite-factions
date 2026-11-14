"""Import the ground a base of the arena is drawn with, from a Fab texture set in the launcher's
library folder, into /Game/Scenery/Grass.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/import_ground.py -unattended -nullrhi

  Grass  Uncut Grass (Quixel Megascans, a 1 m scan)

Its colour, normal and roughness maps come in at 1024, a phone's size. The material M_Grass lays
them over the world, not over a shape's own coordinates, so a base of any size is covered evenly and
two shapes that meet show no seam. The colour is read twice, at two sizes, and the two are mixed, so
the same square of grass is not seen again and again across a base. A face that is not a top (the rim
of a base) is drawn as earth. The library folder is SCENERY_SOURCE."""
import glob
import os
import subprocess

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get("SCENERY_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
FOLDER = "/Game/Scenery/Grass"
NAME = "Grass"
MAPS = "Uncut_Grass-*/texture-set/mid/*/"
SIZE = 1024
# how much ground one copy of the maps covers, in cm: close up, and for the wider variation in colour
NEAR = 200.0
FAR = 1700.0
EARTH = unreal.LinearColor(0.10, 0.07, 0.04, 1.0)

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty


def one(pattern):
    hits = glob.glob(os.path.join(SOURCE, pattern))
    if not hits:
        raise RuntimeError("nothing matches " + pattern + " under " + SOURCE)
    return hits[0]


def texture(kind):
    """One of the maps, shrunk to a phone's size and imported."""
    src = one(os.path.join(MAPS, "*_" + kind + ".jpg"))
    small = os.path.join(SMALL, "%s_%s.jpg" % (NAME, kind))
    subprocess.run(["/usr/bin/sips", "-Z", str(SIZE), src, "--out", small], check=True, capture_output=True)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = small, FOLDER, "T_%s_%s" % (NAME, kind)
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    tex = eal.load_asset("%s/T_%s_%s" % (FOLDER, NAME, kind))
    if tex is None:
        raise RuntimeError("texture did not import: " + kind)
    if kind == "Normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "Roughness":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


def node(mat, kind, x, y):
    return mel.create_material_expression(mat, kind, x, y)


def wire(src, out, dst, pin):
    if not mel.connect_material_expressions(src, out, dst, pin):
        raise RuntimeError("could not connect %s.%s to %s.%s" % (src.get_name(), out, dst.get_name(), pin))


os.makedirs(SMALL, exist_ok=True)
maps = {kind: texture(kind) for kind in ("BaseColor", "Normal", "Roughness")}

path = "%s/M_%s" % (FOLDER, NAME)
if eal.does_asset_exist(path):
    eal.delete_asset(path)
mat = tools.create_asset("M_" + NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("used_with_instanced_static_meshes", True)

# where a point is on the ground, as the place to read the maps at: two sizes
where = node(mat, unreal.MaterialExpressionWorldPosition, -1500, 0)
flat = node(mat, unreal.MaterialExpressionComponentMask, -1300, 0)
flat.set_editor_property("r", True)
flat.set_editor_property("g", True)
flat.set_editor_property("b", False)
flat.set_editor_property("a", False)
wire(where, "", flat, "")


def at(size, y):
    over = node(mat, unreal.MaterialExpressionDivide, -1100, y)
    over.set_editor_property("const_b", size)
    wire(flat, "", over, "A")
    return over


def sample(kind, sampler, uv, y):
    read = node(mat, unreal.MaterialExpressionTextureSample, -850, y)
    read.set_editor_property("texture", maps[kind])
    read.set_editor_property("sampler_type", sampler)
    wire(uv, "", read, "UVs")
    return read


near, far = at(NEAR, -100), at(FAR, 100)
colour_near = sample("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, near, -400)
colour_far = sample("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, far, -140)
normal = sample("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, near, 120)
rough = sample("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, near, 380)

mixed = node(mat, unreal.MaterialExpressionLinearInterpolate, -550, -300)
mixed.set_editor_property("const_alpha", 0.5)
wire(colour_near, "RGB", mixed, "A")
wire(colour_far, "RGB", mixed, "B")

# a top is grass; any other face is earth
facing = node(mat, unreal.MaterialExpressionVertexNormalWS, -1100, 640)
up = node(mat, unreal.MaterialExpressionComponentMask, -900, 640)
up.set_editor_property("r", False)
up.set_editor_property("g", False)
up.set_editor_property("b", True)
up.set_editor_property("a", False)
wire(facing, "", up, "")
less = node(mat, unreal.MaterialExpressionSubtract, -750, 640)
less.set_editor_property("const_b", 0.5)
wire(up, "", less, "A")
sharp = node(mat, unreal.MaterialExpressionMultiply, -600, 640)
sharp.set_editor_property("const_b", 20.0)
wire(less, "", sharp, "A")
top = node(mat, unreal.MaterialExpressionSaturate, -450, 640)
wire(sharp, "", top, "")
earth = node(mat, unreal.MaterialExpressionConstant3Vector, -550, -60)
earth.set_editor_property("constant", EARTH)
colour = node(mat, unreal.MaterialExpressionLinearInterpolate, -300, -200)
wire(earth, "", colour, "A")
wire(mixed, "", colour, "B")
wire(top, "", colour, "Alpha")

for src, out, prop in ((colour, "", P.MP_BASE_COLOR), (normal, "RGB", P.MP_NORMAL), (rough, "R", P.MP_ROUGHNESS)):
    if not mel.connect_material_property(src, out, prop):
        raise RuntimeError("could not connect a map to the material")
mel.recompile_material(mat)
eal.save_loaded_asset(mat)

with open(os.path.join(ROOT, "Saved", "import_ground.txt"), "w") as fh:
    fh.write("M_%s: %s, maps at %d px, one copy over %.0f cm and %.0f cm\n" % (NAME, path, SIZE, NEAR, FAR))

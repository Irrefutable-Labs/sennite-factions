"""Import the surfaces the arena's ground is drawn with, from Fab texture sets in the launcher's
library folder, into /Game/Scenery.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_ground.py" -unattended -nullrhi

  Grass    Uncut Grass (Quixel Megascans, a 1 m scan)    the ground of a base
  Asphalt  Asphalt Road (Quixel Megascans, a 2 m scan)   the traffic deck and the bridge spans

Each one's colour, normal and roughness maps come in at 1024, a phone's size. Its material (M_Grass,
M_Asphalt) lays them over the world, not over a shape's own coordinates, so a shape of any size is
covered evenly and two shapes that meet show no seam. The colour is read twice, at two sizes, and
the two are mixed, so the same square is not seen again and again. A face that is not a top (the rim
of a base, the edge of a deck) is drawn in one plain colour: earth, or dark stone.

The library folder is SCENERY_SOURCE. GROUND names the ones to bring in ("Grass", "Asphalt",
"Grass Asphalt"); left out, all of them."""
import glob
import os
import subprocess

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get("SCENERY_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
DEST = "/Game/Scenery"
SIZE = 1024
# Each surface: where its maps are, how much ground one copy of them covers in cm (close up, and for
# the wider variation in colour), and the colour of a face that is not a top.
SURFACES = {
    "Grass": ("Uncut_Grass-*/texture-set/mid/*/", 200.0, 1700.0, unreal.LinearColor(0.10, 0.07, 0.04, 1.0)),
    "Asphalt": ("Asphalt_Road-*/texture-set/mid/*/", 300.0, 2300.0, unreal.LinearColor(0.03, 0.03, 0.035, 1.0)),
}
WANTED = os.environ.get("GROUND", " ".join(SURFACES)).split()

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty


def one(pattern):
    hits = glob.glob(os.path.join(SOURCE, pattern))
    if not hits:
        raise RuntimeError("nothing matches " + pattern + " under " + SOURCE)
    return hits[0]


def texture(name, pattern, folder, kind):
    """One of a surface's maps, shrunk to a phone's size and imported."""
    src = one(os.path.join(pattern, "*_" + kind + ".jpg"))
    small = os.path.join(SMALL, "%s_%s.jpg" % (name, kind))
    subprocess.run(["/usr/bin/sips", "-Z", str(SIZE), src, "--out", small], check=True, capture_output=True)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = small, folder, "T_%s_%s" % (name, kind)
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    tex = eal.load_asset("%s/T_%s_%s" % (folder, name, kind))
    if tex is None:
        raise RuntimeError("texture did not import: %s %s" % (name, kind))
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
lines = []


def surface(name, pattern, near_size, far_size, side):
    folder = "%s/%s" % (DEST, name)
    maps = {kind: texture(name, pattern, folder, kind) for kind in ("BaseColor", "Normal", "Roughness")}

    path = "%s/M_%s" % (folder, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mat = tools.create_asset("M_" + name, folder, unreal.Material, unreal.MaterialFactoryNew())
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


    near, far = at(near_size, -100), at(far_size, 100)
    colour_near = sample("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, near, -400)
    colour_far = sample("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, far, -140)
    normal = sample("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, near, 120)
    rough = sample("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, near, 380)

    mixed = node(mat, unreal.MaterialExpressionLinearInterpolate, -550, -300)
    mixed.set_editor_property("const_alpha", 0.5)
    wire(colour_near, "RGB", mixed, "A")
    wire(colour_far, "RGB", mixed, "B")

    # a top is the surface; any other face is one plain colour
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
    earth.set_editor_property("constant", side)
    colour = node(mat, unreal.MaterialExpressionLinearInterpolate, -300, -200)
    wire(earth, "", colour, "A")
    wire(mixed, "", colour, "B")
    wire(top, "", colour, "Alpha")

    for src, out, prop in ((colour, "", P.MP_BASE_COLOR), (normal, "RGB", P.MP_NORMAL), (rough, "R", P.MP_ROUGHNESS)):
        if not mel.connect_material_property(src, out, prop):
            raise RuntimeError("could not connect a map to the material")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    lines.append("M_%s: %s, maps at %d px, one copy over %.0f cm and %.0f cm" % (name, path, SIZE, near_size, far_size))


for wanted in WANTED:
    if wanted not in SURFACES:
        raise RuntimeError("no surface " + wanted + ": " + ", ".join(SURFACES))
    surface(wanted, *SURFACES[wanted])

with open(os.path.join(ROOT, "Saved", "import_ground.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

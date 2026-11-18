"""Import the surfaces the arena's ground is drawn with, from Fab texture sets in the launcher's
library folder, into /Game/Scenery.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_ground.py" -unattended -nullrhi

  Grass    Uncut Grass (Quixel Megascans, a 1 m scan)                  the ground of the land
  Asphalt  Asphalt Road (Quixel Megascans, a 2 m scan)                 the traffic deck and the bridge spans
  Dirt     Ground (ambientCG Ground024, a tileable forest floor)       under trees and round the plots' rocks
  Gravel   Terrain Textures - FREE ("Gravel 01" of MicroVerse Extras)  the paths from each pad to its ways out

Each one's colour and normal maps come in at 1024, a phone's size, and the roughness map where the
set has one (Gravel's smoothness hardly varies, so the ground material uses one number for it).

Grass and Asphalt each get a material of their own (M_Grass, M_Asphalt) that lays them over the
world, not over a shape's own coordinates, so a shape of any size is covered evenly and two shapes
that meet show no seam. The colour is read twice, at two sizes, and the two are mixed, so the same
square is not seen again and again. A face that is not a top (the edge of a deck) is drawn in one
plain colour: earth, or dark stone.

M_Ground is the land's: grass, with dirt and gravel laid over it where the arena's plan says. The
arena paints every point of its ground with how much of each it wants (its vertex colour: red is
gravel, green is dirt, alpha is how dry that plot's grass is), and the material sharpens those soft
weights by the detail of each layer's own picture, so an edge reads as stones and earth rather than
as a blur across the 5 m between the ground's points.

The Megascans and MicroVerse normal maps point green up (the OpenGL way, measured against each set's
own height map); Unreal reads green down, so theirs are imported with green flipped. Ground024's
already point green down.

The library folder is SCENERY_SOURCE. GROUND names what to make ("Grass", "Asphalt", "Dirt",
"Gravel", "Ground"; several, space-separated); left out, all of them. "Ground" is M_Ground, made
from the Grass, Dirt and Gravel textures already imported."""
import glob
import os
import subprocess
import tarfile

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.environ.get("SCENERY_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
DEST = "/Game/Scenery"
SIZE = 1024
UNITY_PACKAGE = "Terrain_Textures_-_FREE-*/unity/terrain_textures_free.unitypackage"
GRAVEL = "Assets/MicroVerse-Extras/Terrain Textures/Textures/Gravel 01/Gravel 01_%s.png"

# Each texture set: where each of its maps is (a pattern under SOURCE, or a path inside the Unity
# package), whether its normal map points green up, and for a set with a material of its own, how
# much ground one copy of its maps covers in cm (close up, and for the wider variation in colour)
# and the colour of a face that is not a top.
SETS = {
    "Grass": {
        "maps": {kind: "Uncut_Grass-*/texture-set/mid/*/*_%s.jpg" % kind for kind in ("BaseColor", "Normal", "Roughness")},
        "green_up": True,
        "material": (200.0, 1700.0, unreal.LinearColor(0.10, 0.07, 0.04, 1.0)),
    },
    "Asphalt": {
        "maps": {kind: "Asphalt_Road-*/texture-set/mid/*/*_%s.jpg" % kind for kind in ("BaseColor", "Normal", "Roughness")},
        "green_up": True,
        "material": (300.0, 2300.0, unreal.LinearColor(0.03, 0.03, 0.035, 1.0)),
    },
    "Dirt": {
        "maps": {
            "BaseColor": "Ground-*/fbx/*/textures/Ground024_2K_Color.jpg",
            "Normal": "Ground-*/fbx/*/textures/Ground024_2K_Normal.jpg",
            "Roughness": "Ground-*/fbx/*/textures/Ground024_2K_Roughness.jpg",
        },
        "green_up": False,
    },
    "Gravel": {
        "unity": {kind: GRAVEL % kind for kind in ("BaseColor", "Normal")},
        "green_up": True,
    },
}
# Where M_Ground reads each layer, in cm of ground per copy of its maps.
GRASS_NEAR, GRASS_FAR, DIRT_SIZE, GRAVEL_SIZE = 200.0, 1700.0, 250.0, 200.0
# How rough each layer is, one number each: Uncut Grass's roughness map averages 0.79, Ground024's
# 0.52 and Gravel 01's smoothness 0.38-0.49 (all with little spread).
GRASS_ROUGH, DIRT_ROUGH, GRAVEL_ROUGH = 0.79, 0.55, 0.57
# A plot's grass from lush (alpha 0) to dry (alpha 1).
LUSH, DRY = unreal.LinearColor(1.0, 1.0, 1.0, 1.0), unreal.LinearColor(1.12, 0.98, 0.60, 1.0)
# Ground024 is a pale grey forest floor and Gravel 01 a bright gravel: on the arena's open grass, in
# its sun, both read as white smears, so the dirt is drawn as darker earth and the gravel less bright.
DIRT_TINT, GRAVEL_TINT = unreal.LinearColor(0.70, 0.60, 0.50, 1.0), unreal.LinearColor(0.80, 0.76, 0.70, 1.0)
WANTED = os.environ.get("GROUND", " ".join(list(SETS) + ["Ground"])).split()

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType


def one(pattern):
    hits = glob.glob(os.path.join(SOURCE, pattern))
    if not hits:
        raise RuntimeError("nothing matches " + pattern + " under " + SOURCE)
    return hits[0]


def unity_files(package, wanted):
    """The files a .unitypackage holds at the given project paths, written out under SMALL. A Unity
    package is a gzip tar of folders named by asset id, each holding 'pathname' (the file's path in
    the project) and 'asset' (its bytes). Only the bytes are read; nothing in the package chooses
    where anything is written."""
    named = {}
    with tarfile.open(package, "r:gz") as tar:
        for member in tar:
            if member.isfile() and member.name.endswith("/pathname"):
                folder = member.name.split("/")[-2]
                named[folder] = tar.extractfile(member).read().decode("utf-8").splitlines()[0].strip()
    want = {folder: path for folder, path in named.items() if path in wanted}
    found = {}
    with tarfile.open(package, "r:gz") as tar:
        for member in tar:
            if member.isfile() and member.name.endswith("/asset") and member.name.split("/")[-2] in want:
                path = want[member.name.split("/")[-2]]
                out = os.path.join(SMALL, "unity_" + os.path.basename(path).replace(" ", "_"))
                with open(out, "wb") as fh:
                    fh.write(tar.extractfile(member).read())
                found[path] = out
    missing = [path for path in wanted if path not in found]
    if missing:
        raise RuntimeError("not in " + package + ": " + ", ".join(missing))
    return found


def texture(name, src, folder, kind, green_up):
    """One of a set's maps, shrunk to a phone's size and imported."""
    ext = os.path.splitext(src)[1].lower()
    small = os.path.join(SMALL, "%s_%s%s" % (name, kind, ext))
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
        tex.set_editor_property("flip_green_channel", green_up)
    elif kind == "Roughness":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


def maps_of(name):
    """A set's maps, imported into /Game/Scenery/<name>."""
    spec = SETS[name]
    folder = "%s/%s" % (DEST, name)
    sources = {kind: one(pattern) for kind, pattern in spec.get("maps", {}).items()}
    if "unity" in spec:
        unpacked = unity_files(one(UNITY_PACKAGE), set(spec["unity"].values()))
        sources.update({kind: unpacked[path] for kind, path in spec["unity"].items()})
    return {kind: texture(name, src, folder, kind, spec["green_up"]) for kind, src in sources.items()}


def imported(name, kind):
    tex = eal.load_asset("%s/%s/T_%s_%s" % (DEST, name, name, kind))
    if tex is None:
        raise RuntimeError("T_%s_%s is not imported yet: run with GROUND naming %s first" % (name, kind, name))
    return tex


def node(mat, kind, x, y):
    return mel.create_material_expression(mat, kind, x, y)


def wire(src, out, dst, pin):
    if not mel.connect_material_expressions(src, out, dst, pin):
        raise RuntimeError("could not connect %s.%s to %s.%s" % (src.get_name(), out, dst.get_name(), pin))


def fresh_material(folder, name):
    path = "%s/%s" % (folder, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())


def world_place(mat):
    """Where a point is on the ground, east and north, as the place to read the maps at."""
    where = node(mat, unreal.MaterialExpressionWorldPosition, -1700, 0)
    flat = node(mat, unreal.MaterialExpressionComponentMask, -1500, 0)
    for channel, on in (("r", True), ("g", True), ("b", False), ("a", False)):
        flat.set_editor_property(channel, on)
    wire(where, "", flat, "")
    return flat


def at(mat, flat, size, y):
    over = node(mat, unreal.MaterialExpressionDivide, -1300, y)
    over.set_editor_property("const_b", size)
    wire(flat, "", over, "A")
    return over


def sample(mat, tex, sampler, uv, y):
    read = node(mat, unreal.MaterialExpressionTextureSample, -1050, y)
    read.set_editor_property("texture", tex)
    read.set_editor_property("sampler_type", sampler)
    wire(uv, "", read, "UVs")
    return read


def lerp(mat, a, a_out, b, b_out, alpha, alpha_out, x, y):
    mix = node(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    wire(a, a_out, mix, "A")
    wire(b, b_out, mix, "B")
    wire(alpha, alpha_out, mix, "Alpha")
    return mix


def constant(mat, value, x, y):
    c = node(mat, unreal.MaterialExpressionConstant, x, y)
    c.set_editor_property("r", value)
    return c


def surface(name, maps, near_size, far_size, side):
    """M_Grass or M_Asphalt: the set's maps laid over the world, read at two sizes."""
    folder = "%s/%s" % (DEST, name)
    mat = fresh_material(folder, "M_" + name)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    flat = world_place(mat)
    near, far = at(mat, flat, near_size, -100), at(mat, flat, far_size, 100)
    colour_near = sample(mat, maps["BaseColor"], S.SAMPLERTYPE_COLOR, near, -400)
    colour_far = sample(mat, maps["BaseColor"], S.SAMPLERTYPE_COLOR, far, -140)
    normal = sample(mat, maps["Normal"], S.SAMPLERTYPE_NORMAL, near, 120)
    rough = sample(mat, maps["Roughness"], S.SAMPLERTYPE_MASKS, near, 380)
    mixed = node(mat, unreal.MaterialExpressionLinearInterpolate, -750, -300)
    mixed.set_editor_property("const_alpha", 0.5)
    wire(colour_near, "RGB", mixed, "A")
    wire(colour_far, "RGB", mixed, "B")

    # a top is the surface; any other face is one plain colour
    facing = node(mat, unreal.MaterialExpressionVertexNormalWS, -1300, 640)
    up = node(mat, unreal.MaterialExpressionComponentMask, -1100, 640)
    for channel, on in (("r", False), ("g", False), ("b", True), ("a", False)):
        up.set_editor_property(channel, on)
    wire(facing, "", up, "")
    less = node(mat, unreal.MaterialExpressionSubtract, -950, 640)
    less.set_editor_property("const_b", 0.5)
    wire(up, "", less, "A")
    sharp = node(mat, unreal.MaterialExpressionMultiply, -800, 640)
    sharp.set_editor_property("const_b", 20.0)
    wire(less, "", sharp, "A")
    top = node(mat, unreal.MaterialExpressionSaturate, -650, 640)
    wire(sharp, "", top, "")
    earth = node(mat, unreal.MaterialExpressionConstant3Vector, -750, -60)
    earth.set_editor_property("constant", side)
    colour = lerp(mat, earth, "", mixed, "", top, "", -500, -200)

    for src, out, prop in ((colour, "", P.MP_BASE_COLOR), (normal, "RGB", P.MP_NORMAL), (rough, "R", P.MP_ROUGHNESS)):
        if not mel.connect_material_property(src, out, prop):
            raise RuntimeError("could not connect a map to M_" + name)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    lines.append("M_%s: %s/M_%s, maps at %d px, one copy over %.0f cm and %.0f cm" % (name, folder, name, SIZE, near_size, far_size))


def mean_light(texture_asset_name):
    """How light a layer's picture is on average, as the shader reads it (linear): the picture shrunk to
    one pixel by sips, taken out of sRGB, weighted as the eye weighs the colours."""
    src = os.path.join(SMALL, texture_asset_name)
    one_pixel = os.path.join(SMALL, "mean_" + os.path.splitext(texture_asset_name)[0] + ".bmp")
    subprocess.run(["/usr/bin/sips", "-s", "format", "bmp", "-z", "1", "1", src, "--out", one_pixel], check=True, capture_output=True)
    with open(one_pixel, "rb") as fh:
        data = fh.read()
    at = int.from_bytes(data[10:14], "little")
    blue, green, red = data[at], data[at + 1], data[at + 2]

    def linear(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return 0.299 * linear(red) + 0.587 * linear(green) + 0.114 * linear(blue)


def sharpened(mat, weight_out, paint, layer, light, x, y):
    """A layer's soft weight from the ground's paint, made crisp by the layer's own picture: where the
    weight is half way, the picture's lighter and darker parts, measured against the picture's own mean
    light, decide which side of the edge a point falls on; where it is 0 or 1 nothing changes."""
    edge = node(mat, unreal.MaterialExpressionCustom, x, y)
    edge.set_editor_property("code", """
float h = dot(Detail, float3(0.299, 0.587, 0.114));
float between = saturate(W * (1.0 - W) * 4.0);
return saturate(W + (h / %.4f - 1.0) * 0.6 * between);
""" % max(light, 0.01))
    edge.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    edge.set_editor_property("description", "layer weight sharpened by its detail")
    pins = []
    for pin_name in ("W", "Detail"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    edge.set_editor_property("inputs", pins)
    wire(paint, weight_out, edge, "W")
    wire(layer, "RGB", edge, "Detail")
    return edge


def ground():
    """M_Ground: grass, with dirt and gravel where the ground's paint asks for them."""
    folder = DEST + "/Ground"
    mat = fresh_material(folder, "M_Ground")
    flat = world_place(mat)
    grass_colour = imported("Grass", "BaseColor")
    grass_near = sample(mat, grass_colour, S.SAMPLERTYPE_COLOR, at(mat, flat, GRASS_NEAR, -600), -700)
    grass_far = sample(mat, grass_colour, S.SAMPLERTYPE_COLOR, at(mat, flat, GRASS_FAR, -450), -500)
    dirt = sample(mat, imported("Dirt", "BaseColor"), S.SAMPLERTYPE_COLOR, at(mat, flat, DIRT_SIZE, -300), -300)
    gravel = sample(mat, imported("Gravel", "BaseColor"), S.SAMPLERTYPE_COLOR, at(mat, flat, GRAVEL_SIZE, -150), -100)
    grass_normal = sample(mat, imported("Grass", "Normal"), S.SAMPLERTYPE_NORMAL, at(mat, flat, GRASS_NEAR, 150), 150)
    dirt_normal = sample(mat, imported("Dirt", "Normal"), S.SAMPLERTYPE_NORMAL, at(mat, flat, DIRT_SIZE, 350), 350)
    gravel_normal = sample(mat, imported("Gravel", "Normal"), S.SAMPLERTYPE_NORMAL, at(mat, flat, GRAVEL_SIZE, 550), 550)

    paint = node(mat, unreal.MaterialExpressionVertexColor, -1300, 900)
    grass_mixed = node(mat, unreal.MaterialExpressionLinearInterpolate, -750, -650)
    grass_mixed.set_editor_property("const_alpha", 0.5)
    wire(grass_near, "RGB", grass_mixed, "A")
    wire(grass_far, "RGB", grass_mixed, "B")
    lush = node(mat, unreal.MaterialExpressionConstant3Vector, -950, -900)
    lush.set_editor_property("constant", LUSH)
    dry = node(mat, unreal.MaterialExpressionConstant3Vector, -950, -800)
    dry.set_editor_property("constant", DRY)
    tint = lerp(mat, lush, "", dry, "", paint, "A", -750, -850)
    grass = node(mat, unreal.MaterialExpressionMultiply, -550, -700)
    wire(grass_mixed, "", grass, "A")
    wire(tint, "", grass, "B")

    dirt_light = mean_light("Dirt_BaseColor.jpg")
    gravel_light = mean_light("Gravel_BaseColor.png")
    dirt_weight = sharpened(mat, "G", paint, dirt, dirt_light, -750, 900)
    gravel_weight = sharpened(mat, "R", paint, gravel, gravel_light, -750, 1100)

    def tinted(read, colour, y):
        tint = node(mat, unreal.MaterialExpressionConstant3Vector, -750, y)
        tint.set_editor_property("constant", colour)
        times = node(mat, unreal.MaterialExpressionMultiply, -550, y)
        wire(read, "RGB", times, "A")
        wire(tint, "", times, "B")
        return times

    earth = tinted(dirt, DIRT_TINT, -300)
    stones = tinted(gravel, GRAVEL_TINT, -150)
    colour = lerp(mat, lerp(mat, grass, "", earth, "", dirt_weight, "", -350, -600), "", stones, "", gravel_weight, "", -150, -500)
    normal = lerp(mat, lerp(mat, grass_normal, "RGB", dirt_normal, "RGB", dirt_weight, "", -350, 300), "", gravel_normal, "RGB", gravel_weight, "", -150, 400)
    rough = lerp(mat, lerp(mat, constant(mat, GRASS_ROUGH, -550, 700), "", constant(mat, DIRT_ROUGH, -550, 760), "", dirt_weight, "", -350, 720), "",
                 constant(mat, GRAVEL_ROUGH, -350, 800), "", gravel_weight, "", -150, 760)
    for src, prop in ((colour, P.MP_BASE_COLOR), (normal, P.MP_NORMAL), (rough, P.MP_ROUGHNESS)):
        if not mel.connect_material_property(src, "", prop):
            raise RuntimeError("could not connect M_Ground's outputs")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    lines.append("M_Ground: %s/M_Ground: grass at %.0f and %.0f cm, dirt at %.0f cm by vertex green (mean light %.3f), gravel at %.0f cm by vertex red (mean light %.3f), dry grass by vertex alpha"
                 % (folder, GRASS_NEAR, GRASS_FAR, DIRT_SIZE, dirt_light, GRAVEL_SIZE, gravel_light))


os.makedirs(SMALL, exist_ok=True)
lines = []
for wanted in WANTED:
    if wanted == "Ground":
        continue
    if wanted not in SETS:
        raise RuntimeError("nothing named " + wanted + ": " + ", ".join(list(SETS) + ["Ground"]))
    maps = maps_of(wanted)
    lines.append("%s: %s, at %d px%s" % (wanted, ", ".join("T_%s_%s" % (wanted, kind) for kind in maps), SIZE,
                                         ", normal green flipped" if SETS[wanted]["green_up"] else ""))
    if "material" in SETS[wanted]:
        surface(wanted, maps, *SETS[wanted]["material"])
if "Ground" in WANTED:
    ground()

with open(os.path.join(ROOT, "Saved", "import_ground.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

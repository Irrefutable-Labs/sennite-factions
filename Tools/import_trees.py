"""Bring the trees and bushes that stand on the arena's bases into /Game/Scenery/Trees, from the Fab
pack "Trees and bush Pack LOWPOLY": four trees and seven bushes in one file, each a few crossed
planes carrying a photograph with its outline cut out. Between 8 and 52 triangles each: a phone's size.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_trees.py" -unattended -nullrhi

Each model comes in as its own static mesh, SM_Tree_* or SM_Bush_*, with its middle over its origin
and the lowest thing that is drawn of it at its own height 0. That is not the bottom of its planes:
the foot of a plane is mostly cut away, so the script walks each triangle over its picture and takes
the lowest point whose picture is not cut away (CLIP, the materials' own mark). All eleven keep the
sizes they have beside one another in the pack; the whole pack is scaled by one number so the
tallest tree is TALLEST cm. None has collision of any kind or Nanite: what stops a hero at a tree
is the trunk block the arena lays.

A picture of the pack larger than 1024 is shrunk to it; a smaller one comes in as it is. Each makes
one material: its colour, cut out
by its own alpha, seen from both sides and lit as if it faced up, so a plane is as bright from one
side as from the other. Each model wears the picture the pack gives it.

What it made is written to Saved/import_trees.txt. The pack folder is TREE_SOURCE, or the Fab
library folder the launcher downloads into."""
import glob
import math
import os
import struct
import subprocess
import traceback
import zlib

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary"
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
SCRATCH = "/Game/ZZ_ImportScratch"
DEST = "/Game/Scenery/Trees"
TALLEST = 2000.0
SIZE = 1024
CLIP = 0.3333       # a material's opacity mask cuts away what is under this; the engine's own default
# The pack's models, by the names it gives them: what each is called here, and the picture the pack
# puts on it (read from the file: every material of a model uses one picture).
MODELS = {
    "pine1": ("SM_Tree_PineTall", "pine2"),
    "Pine2": ("SM_Tree_Pine", "pine1"),
    "tree2": ("SM_Tree_Broadleaf", "tree2"),
    "TreeSmall": ("SM_Tree_Small", "tree1"),
    "BushBig2": ("SM_Bush_Big", "bushnormal"),
    "BushMed2": ("SM_Bush_Mid", "bushnormal"),
    "BushSmall2": ("SM_Bush_Small", "bushnormal"),
    "BushFlowerSmall": ("SM_Bush_Flowers", "bushflowers"),
    "BushMed": ("SM_Bush_LeafMid", "bushbig"),
    "BushSmall": ("SM_Bush_LeafSmall", "bushbig"),
    "MushBig": ("SM_Bush_LeafBig", "bushbig"),
}

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
lines = []


def note(msg):
    lines.append(str(msg))
    with open(os.path.join(ROOT, "Saved", "import_trees.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


def read_png(path):
    """A picture's size and its alpha, row by row from the top (an 8-bit PNG with alpha, not interlaced)."""
    raw = open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(path + " is not a PNG")
    at, packed, head = 8, b"", None
    while at < len(raw):
        length, kind = struct.unpack(">I4s", raw[at:at + 8])
        body = raw[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            packed += body
    width, height, depth, colour, _compression, _filter, interlace = head
    if depth != 8 or colour not in (4, 6) or interlace != 0:
        raise RuntimeError("%s: only 8-bit pictures with alpha, not interlaced, are read (depth %d, colour %d)" % (path, depth, colour))
    per = 4 if colour == 6 else 2
    stride = width * per
    data = zlib.decompress(packed)
    rows, above, at = [], bytearray(stride), 0
    for _row in range(height):
        how = data[at]
        line = bytearray(data[at + 1:at + 1 + stride])
        at += 1 + stride
        if how == 1:
            for i in range(per, stride):
                line[i] = (line[i] + line[i - per]) & 255
        elif how == 2:
            for i in range(stride):
                line[i] = (line[i] + above[i]) & 255
        elif how == 3:
            for i in range(stride):
                line[i] = (line[i] + (((line[i - per] if i >= per else 0) + above[i]) >> 1)) & 255
        elif how == 4:
            for i in range(stride):
                a = line[i - per] if i >= per else 0
                b = above[i]
                c = above[i - per] if i >= per else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(bytes(line[per - 1::per]))
        above = line
    return width, height, rows


def kept_runs(picture_path):
    """For each row of a picture, the runs of columns the mask keeps: (first, last) pairs, left to right."""
    width, height, rows = read_png(picture_path)
    mark = int(CLIP * 255.0) + 1
    out = []
    for row in rows:
        runs, start = [], None
        for x in range(width):
            if row[x] >= mark:
                if start is None:
                    start = x
            elif start is not None:
                runs.append((start, x - 1))
                start = None
        if start is not None:
            runs.append((start, width - 1))
        out.append(runs)
    return width, height, out


def drawn_floor(work, picture):
    """The height of the lowest point of a mesh whose picture is not cut away. Each triangle is walked
    row by row over the picture; along a row height runs straight, so the lowest kept point of a row
    is at the first or the last kept column the triangle covers."""
    width, height, runs = picture
    lowest = None
    for triangle in range(work.get_triangle_count()):
        # each call gives three corners and a word on whether they are good; the corners are picked out by what they are
        corners = [c for c in unreal.GeometryScript_MeshQueries.get_triangle_positions(work, triangle) if isinstance(c, unreal.Vector)]
        uvs = [u for u in unreal.GeometryScript_MeshQueries.get_triangle_u_vs(work, 0, triangle) if isinstance(u, unreal.Vector2D)]
        if len(corners) != 3 or len(uvs) != 3:
            raise RuntimeError("triangle %d could not be read" % triangle)
        points = [(uvs[i].x, uvs[i].y, corners[i].z) for i in range(3)]
        # a triangle may lie past an edge of its picture: the picture repeats there
        for row in range(math.floor(min(p[1] for p in points) * height), math.floor(max(p[1] for p in points) * height) + 1):
            v = (row + 0.5) / height
            crossings = []
            for a, b in ((points[0], points[1]), (points[1], points[2]), (points[2], points[0])):
                if (a[1] <= v < b[1]) or (b[1] <= v < a[1]):
                    along = (v - a[1]) / (b[1] - a[1])
                    crossings.append((a[0] + (b[0] - a[0]) * along, a[2] + (b[2] - a[2]) * along))
            if len(crossings) != 2:
                continue
            (u0, z0), (u1, z1) = sorted(crossings)
            first, last = math.ceil(u0 * width - 0.5), math.floor(u1 * width - 0.5)
            for lap in range(first // width, last // width + 1):
                lap_first, lap_last = max(first, lap * width) - lap * width, min(last, lap * width + width - 1) - lap * width
                for start, end in runs[row % height]:
                    if end < lap_first or start > lap_last:
                        continue
                    for x in (max(start, lap_first) + lap * width, min(end, lap_last) + lap * width):
                        z = z0 if u1 - u0 < 1e-9 else z0 + (z1 - z0) * ((x + 0.5) / width - u0) / (u1 - u0)
                        lowest = z if lowest is None else min(lowest, z)
    if lowest is None:
        raise RuntimeError("nothing of the mesh is drawn")
    return lowest


pictures = {}


def material(pack, picture):
    """One picture of the pack as a material: its colour, cut out by its alpha, seen from both sides."""
    src = os.path.join(pack, "textures", picture + ".png")
    if not os.path.isfile(src):
        raise RuntimeError("the pack has no " + src)
    measured = subprocess.run(["/usr/bin/sips", "-g", "pixelWidth", "-g", "pixelHeight", src], check=True, capture_output=True, text=True).stdout
    longest = max(int(line.split(":")[1]) for line in measured.splitlines() if "pixel" in line)
    # sips writes an 8-bit picture with alpha either way; -Z would enlarge a smaller picture, so it is only asked to shrink
    small = os.path.join(SMALL, "Trees_%s.png" % picture)
    subprocess.run(["/usr/bin/sips", "-s", "format", "png"] + (["-Z", str(SIZE)] if longest > SIZE else []) + [src, "--out", small], check=True, capture_output=True)
    pictures[picture] = kept_runs(small)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = small, DEST, "T_Trees_" + picture
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    tex = eal.load_asset("%s/T_Trees_%s" % (DEST, picture))
    if tex is None:
        raise RuntimeError("texture did not import: " + picture)
    name = "M_Trees_" + picture
    fresh("%s/%s" % (DEST, name))
    mat = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    read = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -500, 0)
    read.set_editor_property("texture", tex)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 300)
    rough.set_editor_property("r", 0.9)
    # Lit as if it faced up, whichever way a plane faces: a normal given in the world's space is not
    # turned round for a plane's back, so a plane is as bright from one side as from the other.
    # Lit by its own facing, the side away from the sun is black.
    mat.set_editor_property("tangent_space_normal", False)
    up = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -500, 450)
    up.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    if not (mel.connect_material_property(read, "RGB", P.MP_BASE_COLOR) and mel.connect_material_property(read, "A", P.MP_OPACITY_MASK)
            and mel.connect_material_property(rough, "", P.MP_ROUGHNESS) and mel.connect_material_property(up, "", P.MP_NORMAL)):
        raise RuntimeError("could not wire " + name)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


try:
    os.makedirs(SMALL, exist_ok=True)
    named = os.environ.get("TREE_SOURCE")
    packs = [named] if named else glob.glob(os.path.join(LIBRARY, "Trees_and_bush_Pack_LOWPOLY-*", "fbx", "*_extracted"))
    if not packs or not os.path.isfile(os.path.join(packs[0], "source", "TreesBushLOW.fbx")):
        raise RuntimeError("the Trees and bush Pack is not under " + (named or LIBRARY))
    pack = packs[0]
    materials = {picture: material(pack, picture) for picture in sorted({picture for _name, picture in MODELS.values()})}
    note("pictures: " + ", ".join("%s %d x %d" % (name, pictures[name][0], pictures[name][1]) for name in sorted(materials)))

    # ------------------------------------------------------------------ the models, once, into a scratch folder
    if eal.does_directory_exist(SCRATCH):
        eal.delete_directory(SCRATCH)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("combine_meshes", False)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("generate_lightmap_u_vs", False)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path = os.path.join(pack, "source", "TreesBushLOW.fbx"), SCRATCH
    t.automated, t.replace_existing, t.save = True, True, False
    t.options = ui
    tools.import_asset_tasks([t])
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    heavy = {}
    for found in registry.get_assets_by_path(SCRATCH, recursive=True):
        if str(found.asset_class_path.asset_name) != "StaticMesh":
            continue
        asset = str(found.asset_name)
        for model in MODELS:
            if asset == model or asset.endswith("_" + model):
                heavy[model] = found.get_asset()
    missing = sorted(set(MODELS) - set(heavy))
    if missing:
        raise RuntimeError("the pack's file did not give: " + ", ".join(missing))

    # one scale for the whole pack: the tallest model's planes are TALLEST high
    scale = TALLEST / max(mesh.get_bounding_box().max.z - mesh.get_bounding_box().min.z for mesh in heavy.values())
    note("the pack is scaled by %.3f, so its tallest tree's planes are %.0f cm high" % (scale, TALLEST))

    for model, (name, picture) in MODELS.items():
        work = unreal.DynamicMesh()
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(heavy[model], work, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        box = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(work)
        floor = drawn_floor(work, pictures[picture])
        unreal.GeometryScript_MeshTransforms.translate_mesh(work, unreal.Vector(-(box.min.x + box.max.x) * 0.5, -(box.min.y + box.max.y) * 0.5, -floor))
        unreal.GeometryScript_MeshTransforms.scale_mesh(work, unreal.Vector(scale, scale, scale))
        fresh("%s/%s" % (DEST, name))
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property("enable_recompute_tangents", True)
        options.set_editor_property("enable_collision", False)
        made = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(work, "%s/%s" % (DEST, name), options)
        out = made[0] if isinstance(made, tuple) else made
        if out is None or not isinstance(out, unreal.StaticMesh):
            raise RuntimeError(name + " was not written: " + str(made))
        nanite = out.get_editor_property("nanite_settings")
        if nanite.enabled:
            nanite.enabled = False
            out.set_editor_property("nanite_settings", nanite)
        for slot in range(max(1, len(out.get_editor_property("static_materials")))):
            out.set_material(slot, materials[picture])
        # no collision of any kind: no simple shapes, and no section's triangles in the body
        unreal.EditorStaticMeshLibrary.remove_collisions(out)
        body = out.get_editor_property("body_setup")
        if body is not None:
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        for section in range(out.get_num_sections(0)):
            unreal.EditorStaticMeshLibrary.enable_section_collision(out, False, 0, section)
            if unreal.EditorStaticMeshLibrary.is_section_collision_enabled(out, 0, section):
                raise RuntimeError("%s: section %d still has collision" % (name, section))
        eal.save_loaded_asset(out)
        made_box = out.get_bounding_box()
        note("%s (the pack's %s, picture %s): %d triangles; %.0f by %.0f cm; drawn from height 0 to %.0f, its planes' cut-away feet reaching %.0f below" % (
            name, model, picture, out.get_num_triangles(0), made_box.max.x - made_box.min.x, made_box.max.y - made_box.min.y, made_box.max.z, -made_box.min.z))

    eal.delete_directory(SCRATCH)
    note("done")
except Exception:
    note("FAILED:\n" + traceback.format_exc())
    raise

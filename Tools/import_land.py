"""Bring the land that lies far below the arena into /Game/Scenery/Land, from the Fab model
"Trident Lakes 211111 01 Scene Output 4096": one terrain of mountains and lakes, 11 km square, made
in World Machine, with its water as a second mesh.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_land.py" -unattended -nullrhi

The arena floats over the model's biggest lake. This script finds where: it lays the terrain and the
water over a grid of 50 m cells, takes as lake every cell whose water is above its ground, and in
the biggest lake takes the cell farthest from any shore. Both meshes are moved so that place is over
the origin with the lake's surface at height 0, so the arena has only to set them at a depth.

The terrain is simplified to TERRAIN_TRIANGLES with the engine's own geometry scripting. The water
is 179 separate sheets, lakes and rivers, each ending at its shore; triangles that lie in one plane
to within WATER_ANGLE degrees are merged, which leaves each sheet's outline where the model has it.
Neither mesh has collision of any kind, simple or by triangle, or Nanite: the land is seen and
nothing else, and phones have no Nanite. The terrain keeps the model's own colour and normal maps,
shrunk to 2048, and its occlusion and roughness map, to 1024. (2048, where the rest of the game's
scenery is at 1024: this one picture is spread over 11 km.) The water is the plain colour and gloss
the model gives it; here it is not see-through.

What it found and made is written to Saved/import_land.txt. The model's folder is LAND_SOURCE, or
the Fab library folder the launcher downloads into."""
import glob
import json
import math
import os
import struct
import subprocess
import traceback

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary"
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
SCRATCH = "/Game/ZZ_ImportScratch"
DEST = "/Game/Scenery/Land"
TERRAIN_TRIANGLES = 64000
WATER_ANGLE = 1.0
SIZES = {"BaseColor": 2048, "Normal": 2048, "OcclusionRoughness": 1024}
CELL = 50.0     # metres: the grid the lakes are found on

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
lines = []


def note(msg):
    lines.append(str(msg))
    with open(os.path.join(ROOT, "Saved", "import_land.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


class Glb:
    """The model's file: its description, and the numbers and pictures packed after it."""

    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.read(12)
        length, _kind = struct.unpack("<II", self.f.read(8))
        self.doc = json.loads(self.f.read(length))
        _blob_length, _kind = struct.unpack("<II", self.f.read(8))
        self.start = self.f.tell()

    def numbers(self, index):
        a = self.doc["accessors"][index]
        view = self.doc["bufferViews"][a["bufferView"]]
        self.f.seek(self.start + view.get("byteOffset", 0) + a.get("byteOffset", 0))
        per = {"VEC3": 3, "VEC2": 2, "SCALAR": 1}[a["type"]]
        kind = {5126: "f", 5125: "I", 5123: "H"}[a["componentType"]]
        flat = struct.unpack("<%d%s" % (a["count"] * per, kind), self.f.read(a["count"] * per * struct.calcsize(kind)))
        return [flat[i * per:(i + 1) * per] for i in range(a["count"])] if per > 1 else list(flat)

    def mesh(self, name):
        for node in self.doc["nodes"]:
            if node.get("name") == name:
                part = self.doc["meshes"][node["mesh"]]["primitives"][0]
                return self.numbers(part["attributes"]["POSITION"]), self.numbers(part["indices"])
        raise RuntimeError("the model has no mesh named " + name)

    def bounds(self, name):
        for node in self.doc["nodes"]:
            if node.get("name") == name:
                a = self.doc["accessors"][self.doc["meshes"][node["mesh"]]["primitives"][0]["attributes"]["POSITION"]]
                return a["min"], a["max"]
        raise RuntimeError("the model has no mesh named " + name)

    def image(self, texture_ref, use):
        """One of the terrain's maps, written out and shrunk."""
        image = self.doc["images"][self.doc["textures"][texture_ref["index"]]["source"]]
        view = self.doc["bufferViews"][image["bufferView"]]
        self.f.seek(self.start + view.get("byteOffset", 0))
        ext = "png" if image.get("mimeType") == "image/png" else "jpg"
        raw = os.path.join(SMALL, "Land_%s_full.%s" % (use, ext))
        with open(raw, "wb") as fh:
            fh.write(self.f.read(view["byteLength"]))
        small = os.path.join(SMALL, "Land_%s.png" % use)
        subprocess.run(["/usr/bin/sips", "-s", "format", "png", "-Z", str(SIZES[use]), raw, "--out", small], check=True, capture_output=True)
        os.remove(raw)
        return small


def heights(positions, indices, low, cells):
    """The height of a mesh over the middle of each cell of the grid (the model is y up, in metres)."""
    grid = [[None] * cells[1] for _ in range(cells[0])]
    for t in range(0, len(indices), 3):
        a, b, c = positions[indices[t]], positions[indices[t + 1]], positions[indices[t + 2]]
        i0 = max(0, int(math.ceil((min(a[0], b[0], c[0]) - low[0]) / CELL - 0.5)))
        i1 = min(cells[0] - 1, int(math.floor((max(a[0], b[0], c[0]) - low[0]) / CELL - 0.5)))
        k0 = max(0, int(math.ceil((min(a[2], b[2], c[2]) - low[1]) / CELL - 0.5)))
        k1 = min(cells[1] - 1, int(math.floor((max(a[2], b[2], c[2]) - low[1]) / CELL - 0.5)))
        den = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
        if i0 > i1 or k0 > k1 or abs(den) < 1e-9:
            continue
        for i in range(i0, i1 + 1):
            x = low[0] + (i + 0.5) * CELL
            for k in range(k0, k1 + 1):
                z = low[1] + (k + 0.5) * CELL
                u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / den
                v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / den
                w = 1.0 - u - v
                if u >= -1e-6 and v >= -1e-6 and w >= -1e-6:
                    h = u * a[1] + v * b[1] + w * c[1]
                    if grid[i][k] is None or h > grid[i][k]:
                        grid[i][k] = h
    return grid


def open_water(model):
    """Where the arena floats: the place on the model's biggest lake farthest from any shore.
    Returns (x, z, the lake's level) in the model's own metres, and says what is around it."""
    low_corner, high_corner = model.bounds("Terrain")
    low = (low_corner[0], low_corner[2])
    cells = (int(round((high_corner[0] - low_corner[0]) / CELL)), int(round((high_corner[2] - low_corner[2]) / CELL)))
    ground = heights(*model.mesh("Terrain"), low, cells)
    water = heights(*model.mesh("Water"), low, cells)

    def middle(cell):
        return low[0] + (cell[0] + 0.5) * CELL, low[1] + (cell[1] + 0.5) * CELL

    wet = {}
    for i in range(cells[0]):
        for k in range(cells[1]):
            if water[i][k] is not None and ground[i][k] is not None and water[i][k] > ground[i][k] + 0.2:
                wet[(i, k)] = water[i][k]
    # lakes: wet cells that touch and are at one level
    seen, lakes = set(), []
    for cell in wet:
        if cell in seen:
            continue
        seen.add(cell)
        stack, lake = [cell], []
        while stack:
            at = stack.pop()
            lake.append(at)
            for step in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                beside = (at[0] + step[0], at[1] + step[1])
                if beside in wet and beside not in seen and abs(wet[beside] - wet[at]) < 1.0:
                    seen.add(beside)
                    stack.append(beside)
        lakes.append(lake)
    if not lakes:
        raise RuntimeError("the model has no lake")
    lake = max(lakes, key=len)
    level = sum(wet[cell] for cell in lake) / len(lake)
    shore = [middle((i, k)) for i in range(cells[0]) for k in range(cells[1]) if ground[i][k] is not None and ground[i][k] > level + 0.2]
    best = None
    for cell in lake:
        x, z = middle(cell)
        clear = min((x - sx) ** 2 + (z - sz) ** 2 for sx, sz in shore)
        if best is None or clear > best[0]:
            best = (clear, x, z)
    clear, x, z = math.sqrt(best[0]), best[1], best[2]
    note("the biggest lake: %.2f km2 at %.0f m; its open water is widest at x %.0f z %.0f of the model, %.0f m from any shore" % (
        len(lake) * CELL * CELL / 1e6, level, x, z, clear))
    # the model as it comes, read at the middle of each 50 m cell: the made terrain is simplified
    # from it, and the arena check measures that mesh itself
    for reach in (600, 900, 1200, 1500, 2000, 3000, 5000):
        ring = [ground[i][k] for i in range(cells[0]) for k in range(cells[1])
                if ground[i][k] is not None and math.hypot(middle((i, k))[0] - x, middle((i, k))[1] - z) <= reach]
        note("  within %d m of it the model's land, read every %.0f m, rises %.0f m above the lake" % (reach, CELL, max(ring) - level))
    return x, z, level


def texture(path, name, use):
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = path, DEST, name
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    tex = eal.load_asset("%s/%s" % (DEST, name))
    if tex is None:
        raise RuntimeError("texture did not import: " + name)
    if use == "Normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        # Not flipped. The glTF rule is green up, but this model's map has green pointing down the
        # picture, toward +V, which is what the engine reads: measured on the picture against the
        # mesh's own slopes and its occlusion map. Set, not left, so a second run undoes a first.
        tex.set_editor_property("flip_green_channel", False)
    elif use == "OcclusionRoughness":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


def terrain_material(model):
    described = next(m for m in model.doc["materials"] if m["name"] == "TerrainMaterial")
    pbr = described["pbrMetallicRoughness"]
    uses = (("BaseColor", pbr["baseColorTexture"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, (("RGB", P.MP_BASE_COLOR),)),
            ("Normal", described["normalTexture"], unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, (("RGB", P.MP_NORMAL),)),
            # one map: occlusion in red, roughness in green
            ("OcclusionRoughness", pbr["metallicRoughnessTexture"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
             (("R", P.MP_AMBIENT_OCCLUSION), ("G", P.MP_ROUGHNESS))))
    fresh(DEST + "/M_Land")
    mat = tools.create_asset("M_Land", DEST, unreal.Material, unreal.MaterialFactoryNew())
    for row, (use, ref, sampler, pins) in enumerate(uses):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -500, row * 260)
        node.set_editor_property("texture", texture(model.image(ref, use), "T_Land_" + use, use))
        node.set_editor_property("sampler_type", sampler)
        for pin, prop in pins:
            if not mel.connect_material_property(node, pin, prop):
                raise RuntimeError("the land's material: could not connect " + use)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def water_material(model):
    described = next(m for m in model.doc["materials"] if m["name"] == "WaterMaterial")["pbrMetallicRoughness"]
    r, g, b = described["baseColorFactor"][:3]
    fresh(DEST + "/M_LandWater")
    mat = tools.create_asset("M_LandWater", DEST, unreal.Material, unreal.MaterialFactoryNew())
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    colour.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", described["roughnessFactor"])
    if not (mel.connect_material_property(colour, "", P.MP_BASE_COLOR) and mel.connect_material_property(rough, "", P.MP_ROUGHNESS)):
        raise RuntimeError("the water's material did not wire up")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def axis(low, high, model_low, model_high):
    """Which of the model's two ground axes an engine axis is, and its sign: the engine's range of
    it, in cm, is one of the model's ranges, in metres, one way round or the other."""
    for name in ("x", "z"):
        for sign in (1.0, -1.0):
            a, b = sorted((sign * model_low[name] * 100.0, sign * model_high[name] * 100.0))
            if abs(a - low) < 100.0 and abs(b - high) < 100.0:
                return name, sign
    raise RuntimeError("the engine's range %.0f to %.0f cm is neither of the model's" % (low, high))


try:
    os.makedirs(SMALL, exist_ok=True)
    named = os.environ.get("LAND_SOURCE")
    files = sorted(glob.glob(os.path.join(named or os.path.join(LIBRARY, "Trident_Lakes_*"), "glb", "*", "source", "*.glb")))
    if not files:
        raise RuntimeError("no Trident Lakes .glb under " + (named or LIBRARY))
    source = files[0]
    note("source: %s (%.0f MB)" % (os.path.basename(source), os.path.getsize(source) / 1e6))
    model = Glb(source)
    spot_x, spot_z, level = open_water(model)

    land_material = terrain_material(model)
    lake_material = water_material(model)
    note("maps: colour and normal at %d, occlusion and roughness at %d" % (SIZES["BaseColor"], SIZES["OcclusionRoughness"]))

    # ------------------------------------------------------------------ the two meshes, once, into a scratch folder
    if eal.does_directory_exist(SCRATCH):
        eal.delete_directory(SCRATCH)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path = source, SCRATCH
    t.automated, t.replace_existing, t.save = True, True, False
    tools.import_asset_tasks([t])
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    heavy = [data.get_asset() for data in registry.get_assets_by_path(SCRATCH, recursive=True) if str(data.asset_class_path.asset_name) == "StaticMesh"]
    if len(heavy) != 2:
        raise RuntimeError("the model did not import as its two meshes: " + ", ".join(m.get_name() for m in heavy))
    # the terrain is the one that goes higher
    heavy.sort(key=lambda m: m.get_bounding_box().max.z, reverse=True)

    # where the model's place over the open water is in the engine's space: the engine turns the model
    # (y up, metres) to z up, cm; which way round its two ground axes land is read off the terrain's box
    box = heavy[0].get_bounding_box()
    model_low, model_high = model.bounds("Terrain")
    model_low = {"x": model_low[0], "z": model_low[2]}
    model_high = {"x": model_high[0], "z": model_high[2]}
    spot = {"x": spot_x, "z": spot_z}
    x_axis, x_sign = axis(box.min.x, box.max.x, model_low, model_high)
    y_axis, y_sign = axis(box.min.y, box.max.y, model_low, model_high)
    if x_axis == y_axis:
        raise RuntimeError("both of the engine's ground axes read as the model's " + x_axis)
    move = unreal.Vector(-x_sign * spot[x_axis] * 100.0, -y_sign * spot[y_axis] * 100.0, -level * 100.0)
    note("engine x is the model's %s%s, engine y its %s%s; moved by %.0f %.0f %.0f cm" % (
        "-" if x_sign < 0 else "", x_axis, "-" if y_sign < 0 else "", y_axis, move.x, move.y, move.z))

    made_meshes = []
    for mesh, name, material in ((heavy[0], "SM_Land", land_material), (heavy[1], "SM_LandWater", lake_material)):
        work = unreal.DynamicMesh()
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(mesh, work, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        before = work.get_triangle_count()
        if name == "SM_Land":
            unreal.GeometryScript_MeshSimplification.apply_simplify_to_triangle_count(work, TERRAIN_TRIANGLES, unreal.GeometryScriptSimplifyMeshOptions())
        else:
            planar = unreal.GeometryScriptPlanarSimplifyOptions()
            planar.set_editor_property("angle_threshold", WATER_ANGLE)
            unreal.GeometryScript_MeshSimplification.apply_simplify_to_planar(work, planar)
        unreal.GeometryScript_MeshTransforms.translate_mesh(work, move)
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
            out.set_material(slot, material)
        # No collision of any kind: no simple shapes, and no section's triangles in the body either,
        # so nothing is cooked or loaded for a mesh that is only looked at. (The size the saved asset
        # records for its body is taken here, while the triangles are still in memory; loaded and
        # saved again from a fresh start, each records 1,180 bytes: an empty body.)
        unreal.EditorStaticMeshLibrary.remove_collisions(out)
        body = out.get_editor_property("body_setup")
        if body is not None:
            # with no simple shapes, simple-as-complex leaves the body with nothing in it
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        for section in range(out.get_num_sections(0)):
            unreal.EditorStaticMeshLibrary.enable_section_collision(out, False, 0, section)
            if unreal.EditorStaticMeshLibrary.is_section_collision_enabled(out, 0, section):
                raise RuntimeError("%s: section %d still has collision" % (name, section))
        eal.save_loaded_asset(out)
        made_box = out.get_bounding_box()
        note("%s: %d triangles (the model's: %d); x %.0f to %.0f, y %.0f to %.0f, z %.0f to %.0f cm" % (
            name, out.get_num_triangles(0), before, made_box.min.x, made_box.max.x, made_box.min.y, made_box.max.y, made_box.min.z, made_box.max.z))
        made_meshes.append(out)

    eal.delete_directory(SCRATCH)
    note("done")
except Exception:
    note("FAILED:\n" + traceback.format_exc())
    raise

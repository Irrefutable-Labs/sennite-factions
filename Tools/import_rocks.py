"""Bring the rocks from the Fab pack "Rock Pack Vol 01" and the steel frame from "Construction Prop"
into /Game/Scenery, at a phone's size.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/import_rocks.py -unattended -nullrhi

Rock Pack Vol 01 is twelve rocks of 700 to 10,000 triangles sharing four sets of 4K textures, which
each file carries inside itself. Here each set's colour, normal and roughness are taken out of a file,
shrunk to 1024 and made into one material; each rock comes in as a static mesh without collision,
using its set's material. Rock 09 is modelled a hundred units from its own origin and is left out.
  /Game/Scenery/Rocks/SM_Rock_01 .. SM_Rock_12, M_Rock_01 .. M_Rock_04

Construction Prop is one steel frame on a concrete slab, 9 by 13 m and 3.8 m high, 19,000 triangles in
fourteen parts, packed as a .rar; FRAME_SOURCE is the folder it was unpacked to (Construct.obj,
Material_Diffuse.jpg). The parts are joined into one mesh, stood up and brought to size.
  /Game/Scenery/Frame/SM_Frame, M_Frame

What each measures is written to Saved/import_rocks.txt."""
import glob
import os
import struct
import subprocess
import traceback
import zlib

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = os.environ.get("SCENERY_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
FRAME_SOURCE = os.environ.get("FRAME_SOURCE", "")
SMALL = os.path.join(ROOT, "Saved", "SceneryImport")
SIZE = 1024
LEFT_OUT = ("09",)

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
lines = []


def note(msg):
    lines.append(str(msg))
    with open(os.path.join(ROOT, "Saved", "import_rocks.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------------- reading a binary FBX
def read_node(f, wide):
    head = f.read(25 if wide else 13)
    end, count, plen, nlen = struct.unpack("<QQQB" if wide else "<IIIB", head)
    if end == 0:
        return None
    name = f.read(nlen).decode("ascii", "replace")
    props = []
    start = f.tell()
    for _ in range(count):
        kind = f.read(1)
        if kind in b"YCIFDL":
            size = {b"Y": 2, b"C": 1, b"I": 4, b"F": 4, b"D": 8, b"L": 8}[kind]
            f.seek(size, 1)
            props.append(None)
        elif kind in b"fdlib":
            _length, _encoding, packed = struct.unpack("<III", f.read(12))
            f.seek(packed, 1)
            props.append(None)
        elif kind == b"S":
            (length,) = struct.unpack("<I", f.read(4))
            props.append(f.read(length).decode("utf-8", "replace"))
        elif kind == b"R":
            (length,) = struct.unpack("<I", f.read(4))
            props.append(f.read(length))
        else:
            raise ValueError("unknown FBX property kind %r" % kind)
    f.seek(start + plen)
    kids = []
    while f.tell() < end:
        kid = read_node(f, wide)
        if kid is None:
            break
        kids.append(kid)
    f.seek(end)
    return name, props, kids


def carried(path):
    """The material's name, and the pictures the file carries, by file name."""
    with open(path, "rb") as f:
        if f.read(21) != b"Kaydara FBX Binary  \x00":
            raise RuntimeError("not a binary FBX: " + path)
        f.read(2)
        (version,) = struct.unpack("<I", f.read(4))
        wide = version >= 7500
        material, pictures = None, {}
        while True:
            node = read_node(f, wide)
            if node is None:
                break
            if node[0] != "Objects":
                continue
            for obj in node[2]:
                if obj[0] == "Material":
                    material = obj[1][1].split("\x00")[0]
                elif obj[0] == "Video":
                    name, content = None, None
                    for kid in obj[2]:
                        if kid[0] == "RelativeFilename":
                            name = kid[1][0].replace("\\", "/").split("/")[-1]
                        elif kid[0] == "Content" and kid[1] and isinstance(kid[1][0], bytes):
                            content = kid[1][0]
                    if name and content:
                        pictures[name] = content
        return material, pictures


# ---------------------------------------------------------------------- shared pieces
def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


def texture(picture, folder, name, kind):
    """A picture on disk, shrunk to a phone's size and imported."""
    small = os.path.join(SMALL, name + ".jpg")
    subprocess.run(["/usr/bin/sips", "-s", "format", "jpeg", "-Z", str(SIZE), picture, "--out", small], check=True, capture_output=True)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = small, folder, name
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    tex = eal.load_asset("%s/%s" % (folder, name))
    if tex is None:
        raise RuntimeError("texture did not import: " + name)
    if kind == "Normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "Roughness":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)
    return tex


def material(folder, name, maps):
    fresh("%s/%s" % (folder, name))
    mat = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    for i, (kind, sampler, pin, prop) in enumerate((
            ("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "RGB", P.MP_BASE_COLOR),
            ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "RGB", P.MP_NORMAL),
            ("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "R", P.MP_ROUGHNESS))):
        if kind not in maps:
            continue
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -500, i * 260)
        node.set_editor_property("texture", maps[kind])
        node.set_editor_property("sampler_type", sampler)
        if not mel.connect_material_property(node, pin, prop):
            raise RuntimeError("%s: could not connect %s" % (name, kind))
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def mesh(source, folder, name, mat):
    """One model file as one static mesh: no collision, no Nanite, the given material on every slot."""
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = source, folder, name
    t.automated, t.replace_existing, t.save = True, True, True
    if source.lower().endswith(".fbx"):
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
        t.options = ui
    tools.import_asset_tasks([t])
    made = None
    for path in t.get_editor_property("imported_object_paths"):
        thing = eal.load_asset(str(path).split(".")[0])
        if isinstance(thing, unreal.StaticMesh):
            made = thing
    if made is None:
        made = eal.load_asset("%s/%s" % (folder, name))
    if made is None or not isinstance(made, unreal.StaticMesh):
        raise RuntimeError(name + " did not import as a static mesh")
    nanite = made.get_editor_property("nanite_settings")
    if nanite.enabled:                      # plain meshes: phones have no Nanite
        nanite.enabled = False
        made.set_editor_property("nanite_settings", nanite)
    for slot in range(max(1, len(made.get_editor_property("static_materials")))):
        made.set_material(slot, mat)
    unreal.EditorStaticMeshLibrary.remove_collisions(made)
    eal.save_loaded_asset(made)
    box = made.get_bounding_box()
    note("%s (%s): x %.0f to %.0f, y %.0f to %.0f, z %.0f to %.0f cm; %d triangles; %d material slots" % (
        name, made.get_path_name(), box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z,
        made.get_num_triangles(0), len(made.get_editor_property("static_materials"))))
    return made


def frame(source, folder, mat):
    """The steel frame. Its file holds fourteen parts in metres with y up, and comes in as fourteen
    meshes a hundredth of their size, lying on their side. Here they are imported into a scratch folder,
    joined into one mesh, stood up and brought to size, and written as SM_Frame; the scratch goes."""
    scratch = "/Game/ZZ_ImportScratch"
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path = source, scratch
    t.automated, t.replace_existing, t.save = True, True, False
    tools.import_asset_tasks([t])
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    whole = unreal.DynamicMesh()
    parts = 0
    for data in registry.get_assets_by_path(scratch, recursive=True):
        if str(data.asset_class_path.asset_name) != "StaticMesh":
            continue
        part = unreal.DynamicMesh()
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(data.get_asset(), part, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        unreal.GeometryScript_MeshEdits.append_mesh(whole, part, unreal.Transform())
        parts += 1
    if parts == 0:
        raise RuntimeError("the frame did not import")
    before = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(whole)
    # the thin way is up: the frame is 3.8 m high and 9 by 13 m on the ground
    size = before.max - before.min
    turn = unreal.Rotator(0.0, 0.0, 0.0)
    if size.y < size.z and size.y < size.x:
        turn = unreal.Rotator(roll=90.0, pitch=0.0, yaw=0.0)
    elif size.x < size.z and size.x < size.y:
        turn = unreal.Rotator(roll=0.0, pitch=90.0, yaw=0.0)
    unreal.GeometryScript_MeshTransforms.transform_mesh(whole, unreal.Transform(rotation=turn, scale=unreal.Vector(100.0, 100.0, 100.0)))
    stood = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(whole)
    # its middle over the origin, its slab on the ground
    unreal.GeometryScript_MeshTransforms.translate_mesh(whole, unreal.Vector(-(stood.min.x + stood.max.x) * 0.5, -(stood.min.y + stood.max.y) * 0.5, -stood.min.z))
    for name in ("SM_Frame",):
        fresh("%s/%s" % (folder, name))
    made = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(whole, folder + "/SM_Frame", unreal.GeometryScriptCreateNewStaticMeshAssetOptions())
    made = made[0] if isinstance(made, tuple) else made
    if made is None or not isinstance(made, unreal.StaticMesh):
        raise RuntimeError("the joined frame was not written")
    for slot in range(max(1, len(made.get_editor_property("static_materials")))):
        made.set_material(slot, mat)
    unreal.EditorStaticMeshLibrary.remove_collisions(made)
    eal.save_loaded_asset(made)
    eal.delete_directory(scratch)
    # earlier runs of this script left the fourteen parts in the frame's own folder
    for data in registry.get_assets_by_path(folder, recursive=False):
        leaf = str(data.asset_name)
        if leaf.startswith("group_") or leaf == "material":
            eal.delete_asset("%s/%s" % (folder, leaf))
    box = made.get_bounding_box()
    note("SM_Frame: %d parts joined; x %.0f to %.0f, y %.0f to %.0f, z %.0f to %.0f cm; %d triangles" % (
        parts, box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z, made.get_num_triangles(0)))


try:
    os.makedirs(SMALL, exist_ok=True)

    # ------------------------------------------------------------------ the rocks
    folder = "/Game/Scenery/Rocks"
    files = sorted(glob.glob(os.path.join(LIBRARY, "Rock_Pack_Vol_01-*", "fbx", "*", "SM_RP_Vol_01_*.fbx")))
    if not files:
        raise RuntimeError("no rocks under " + LIBRARY)
    sets = {}
    for source in files:
        number = os.path.basename(source)[len("SM_RP_Vol_01_"):-len(".fbx")]
        if number in LEFT_OUT:
            continue
        which, pictures = carried(source)
        if which is None:
            raise RuntimeError("no material named in " + source)
        if which not in sets:
            maps = {}
            for kind in ("BaseColor", "Normal", "Roughness"):
                hits = [name for name in pictures if name.endswith("_%s.jpg" % kind)]
                if not hits:
                    raise RuntimeError("%s carries no %s picture" % (source, kind))
                raw = os.path.join(SMALL, "rock_full.jpg")
                with open(raw, "wb") as fh:
                    fh.write(pictures[hits[0]])
                maps[kind] = texture(raw, folder, "T_Rock_%s_%s" % (which[-2:], kind), kind)
                os.remove(raw)
            sets[which] = material(folder, "M_Rock_" + which[-2:], maps)
        mesh(source, folder, "SM_Rock_" + number, sets[which])
    note("rocks: %d, in %d texture sets" % (len(files) - len(LEFT_OUT), len(sets)))

    # ------------------------------------------------------------------ the steel frame
    if FRAME_SOURCE:
        folder = "/Game/Scenery/Frame"
        maps = {"BaseColor": texture(os.path.join(FRAME_SOURCE, "Material_Diffuse.jpg"), folder, "T_Frame_BaseColor", "BaseColor")}
        rough = glob.glob(os.path.join(LIBRARY, "Construction_Prop-*", "obj", "*", "textures", "Material_Roughness.jpg"))
        if rough:
            maps["Roughness"] = texture(rough[0], folder, "T_Frame_Roughness", "Roughness")
        frame(os.path.join(FRAME_SOURCE, "Construct.obj"), folder, material(folder, "M_Frame", maps))
    else:
        note("FRAME_SOURCE not set: the steel frame was not imported")
    note("done")
except Exception:
    note("FAILED:\n" + traceback.format_exc())
    raise

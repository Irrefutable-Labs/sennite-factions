"""Import the rounds the guns fire into /Game/Weapons, from the owner's Fab pack "Pistol, Rifle, Shotgun Ammo":

  SM_RifleRound    the rifle's: the head of the .243 WIN round (what leaves the barrel, not its case);
  SM_SniperRound   the sniper's: the head of the .308 WIN round;
  SM_ShotgunShot   the shotgun's: the load of shot inside the fourth shotgun shell.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_ammo.py" -unattended -nullrhi

The pack is one model of fourteen rounds, each in a few flat colours. Each round here is only the faces of the parts
named below, written out as a model of its own and imported; its colours are baked into its vertex colours and it is
drawn with M_GunPaint, as the painted guns are (Tools/import_weapons.py). It is turned so its point is along +X, its
middle at the origin, and made as long as written: longer than a real round, so a phone's player sees it fly.

The owner, 2026-11-19, asked which gun fires which, chose the rifle, shotgun and sniper firing the ammo models.

The pack is looked for in the Fab library folder the launcher downloads into, or WEAPON_LIBRARY."""
import glob
import os
import tempfile

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = os.environ.get("WEAPON_LIBRARY", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
SOURCE = "Pistol___Rifle___Shotgun_Ammo-*/fbx/ammo_extracted/Ammo.obj"
DEST = "/Game/Weapons"

# each round: the pack's parts (its materials) whose faces it is made of, and how long it is made, in cm
ROUNDS = {
    "SM_RifleRound": dict(parts=("243_WIM_Bullet_Top", "243_WIM_Bullet_Tip"), length=9.0),
    "SM_SniperRound": dict(parts=("308_WIM_Bullet_Top", "308_WIM_Bullet_Tip"), length=12.0),
    "SM_ShotgunShot": dict(parts=("Shotgun_Cartridge4_Shot",), length=8.0),
}
# a part whose colour the pack leaves black is given one here: the shot is lead, dark grey and not black
COLOURS = {"Shotgun_Cartridge4_Shot": (0.24, 0.24, 0.26)}
# the pack's rounds lie along +Y with their points that way: a quarter turn puts them along +X
TURN = (0.0, 0.0, -90.0)

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log_warning("AMMO " + str(msg))


def read_colours(mtl):
    """Each material of the pack and its flat colour (Kd)."""
    colours, name = {}, None
    for line in open(mtl):
        bits = line.split()
        if not bits:
            continue
        if bits[0] == "newmtl":
            name = bits[1]
        elif bits[0] == "Kd" and name:
            colours[name] = tuple(float(x) for x in bits[1:4])
    return colours


def cut(obj, parts, into):
    """A model of only the faces of these parts: every vertex, normal and texture corner is written as it is, so the
    faces' numbers stay right; the importer keeps only what the faces use."""
    keep, kept = False, 0
    with open(obj) as src, open(into, "w") as out:
        for line in src:
            if line.startswith(("v ", "vt ", "vn ", "mtllib")):
                out.write(line)
            elif line.startswith("usemtl"):
                keep = line.split()[1] in parts
                if keep:
                    out.write(line)
            elif line.startswith("f "):
                if keep:
                    out.write(line)
                    kept += 1
    return kept


found = glob.glob(os.path.join(LIBRARY, SOURCE))
if not found:
    raise RuntimeError("%s is not under %s" % (SOURCE, LIBRARY))
obj = found[0]
colours = read_colours(os.path.splitext(obj)[0] + ".mtl")
paint = eal.load_asset(DEST + "/M_GunPaint")
if paint is None:
    raise RuntimeError("%s/M_GunPaint is not there: Tools/import_weapons.py makes it" % DEST)
work = tempfile.mkdtemp(prefix="sennite_ammo_")
with open(os.path.join(work, "Ammo.mtl"), "w") as fh:
    fh.write(open(os.path.splitext(obj)[0] + ".mtl").read())

for name, round_ in ROUNDS.items():
    source = os.path.join(work, name + ".obj")
    faces = cut(obj, round_["parts"], source)
    if faces == 0:
        raise RuntimeError("%s: the pack has no faces of %s" % (name, ", ".join(round_["parts"])))
    folder = DEST + "/Ammo_" + name
    if eal.does_directory_exist(folder):
        eal.delete_directory(folder)
    task = unreal.AssetImportTask()
    task.filename, task.destination_path = source, folder
    task.automated, task.replace_existing, task.save = True, True, False
    tools.import_asset_tasks([task])
    came = [eal.load_asset(path) for path in eal.list_assets(folder, recursive=True)]
    meshes = [each for each in came if isinstance(each, unreal.StaticMesh)]
    if len(meshes) != 1:
        raise RuntimeError("%s came in as %d meshes, not one" % (name, len(meshes)))
    dyn = unreal.DynamicMesh()
    lod = unreal.GeometryScriptMeshReadLOD()
    lod.set_editor_property("lod_type", unreal.GeometryScriptLODType.MAX_AVAILABLE)
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(meshes[0], dyn, unreal.GeometryScriptCopyMeshFromAssetOptions(), lod)
    slots = [slot.material_slot_name for slot in meshes[0].get_editor_property("static_materials")]

    # each part's colour, from the pack, into the vertex colours of its triangles
    flags = unreal.GeometryScriptColorFlags()
    unreal.GeometryScript_VertexColors.set_mesh_constant_vertex_color(dyn, unreal.LinearColor(0.3, 0.3, 0.3, 0.0), flags, clear_existing=True)
    for index, slot in enumerate(slots):
        part = next((p for p in round_["parts"] if p.lower() == str(slot).lower()), None)
        if part is None or part not in colours:
            raise RuntimeError("%s: material slot %s is not one of its parts" % (name, slot))
        r, g, b = COLOURS.get(part, colours[part])
        picked = unreal.GeometryScript_MeshSelection.select_mesh_elements_by_material_id(dyn, index, unreal.GeometryScriptMeshSelectionType.TRIANGLES)
        picked = [each for each in picked if isinstance(each, unreal.GeometryScriptMeshSelection)][0] if isinstance(picked, tuple) else picked
        unreal.GeometryScript_VertexColors.set_mesh_selection_vertex_color(dyn, picked, unreal.LinearColor(r, g, b, 0.0), flags, create_color_seam=True)
        log("%s: paint %-24s (%.2f, %.2f, %.2f)" % (name, part, r, g, b))
    unreal.GeometryScript_Materials.set_all_triangle_material_i_ds(dyn, unreal.GeometryScript_List.convert_array_to_index_list([0] * dyn.get_triangle_count()))

    roll, pitch, yaw = TURN
    unreal.GeometryScript_MeshTransforms.transform_mesh(dyn, unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(roll, pitch, yaw), unreal.Vector(1, 1, 1)))
    box = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dyn)
    if (box.max.x - box.min.x) < max(box.max.y - box.min.y, box.max.z - box.min.z):
        raise RuntimeError("%s: turned as written, it is not longest along X" % name)
    middle = (box.min + box.max) * 0.5
    unreal.GeometryScript_MeshTransforms.translate_mesh(dyn, unreal.Vector(-middle.x, -middle.y, -middle.z))
    scale = round_["length"] / (box.max.x - box.min.x)
    unreal.GeometryScript_MeshTransforms.scale_mesh(dyn, unreal.Vector(scale, scale, scale), unreal.Vector(0, 0, 0))

    path = DEST + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.set_editor_property("enable_nanite", False)       # phones have no Nanite
    options.set_editor_property("enable_collision", False)    # a round is drawn; the bullet's sphere is what hits
    made = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dyn, path, options)
    mesh = [each for each in made if isinstance(each, unreal.StaticMesh)][0] if isinstance(made, tuple) else made
    if mesh is None:
        raise RuntimeError("%s was not made" % name)
    mesh.set_material(0, paint)
    eal.save_loaded_asset(mesh)
    eal.delete_directory(folder)

    # where its point is: the end of it farther along X should be the narrower (a point, not a base)
    points = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dyn, skip_gaps=False)
    listed = [each for each in points if isinstance(each, unreal.GeometryScriptVectorList)][0]
    array = unreal.GeometryScript_List.convert_vector_list_to_array(listed)
    half = round_["length"] * 0.5
    width = lambda lo, hi: max([abs(p.y) for p in array if lo <= p.x <= hi] or [0.0])
    front, back = width(half * 0.7, half), width(-half, -half * 0.7)
    done = mesh.get_bounding_box()
    log("%s: %d faces, %d triangles, %.1f x %.1f x %.1f cm; %.2f cm wide at the front, %.2f at the back" % (
        name, faces, mesh.get_num_triangles(0), done.max.x - done.min.x, done.max.y - done.min.y, done.max.z - done.min.z, front, back))
    if name != "SM_ShotgunShot" and front >= back:
        raise RuntimeError("%s: its point is not along +X" % name)

os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
with open(os.path.join(ROOT, "Saved", "import_ammo.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

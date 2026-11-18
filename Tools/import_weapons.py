"""Import the guns the game uses into /Game/Weapons, from two Fab packs:

  the rifle    "Free Sci-Fi Rifle": one .obj of 1.5 million triangles, with no textures in the pack;
  the shotgun  "sci-fi Enrgy Rifle (3D Static Model, Game Ready, No Animations)": one .glb of half a
               million triangles, with its own two materials and their four textures.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_weapons.py" -unattended -nullrhi

Each gun becomes one static mesh a phone can draw: cut down to a few thousand triangles, no Nanite,
turned so its muzzle is along +X and its top along +Z, its middle at the origin, and scaled to its
length in cm. The energy rifle keeps the pack's materials, its textures drawn at 1024. The sci-fi
rifle has no textures to keep, so it is one dark metal all over (M_SciFiRifle).

Where each gun's grip, foregrip and muzzle are is in Config/DefaultGame.ini; this script prints a
side view of each finished gun (Saved/import_weapons.txt) to read them off.

The packs are looked for in the Fab library folder the launcher downloads into, or WEAPON_LIBRARY."""
import glob
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = os.environ.get("WEAPON_LIBRARY", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
DEST = "/Game/Weapons"

# name: where in the library, how the source is turned to put its muzzle along +X and its top along +Z
# (roll, pitch, yaw), how long it is made in cm, and how many triangles it is cut down to
GUNS = {
    "SM_SciFiRifle": dict(source="Free_Sci-Fi_Rifle-*/obj/*/Free-Rifle/Free_Rifle/Rifle.obj", folder="SciFiRifle",
                          turn=(90.0, 0.0, 90.0), length=78.0, triangles=30000),
    "SM_EnergyRifle": dict(source="sci-fi_Enrgy_Rifle*/glb/energy_weapon.glb", folder="EnergyRifle",
                           turn=(0.0, 0.0, 180.0), length=62.0, triangles=14000),
}
# what the game used before, from "Low Poly Weapons VOL.1"
OLD = ("SM_M4", "SM_Shotgun", "M_Guns", "T_Guns")

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log_warning("WEAPONS " + str(msg))


def positions(dyn):
    found = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dyn, skip_gaps=False)
    listed = [each for each in found if isinstance(each, unreal.GeometryScriptVectorList)][0]
    array = unreal.GeometryScript_List.convert_vector_list_to_array(listed)
    step = max(1, len(array) // 60000)
    return [(array[i].x, array[i].y, array[i].z) for i in range(0, len(array), step)]


def side_view(points, cols=78, rows=24):
    """The gun from its right side: +X (the muzzle) to the right, +Z up. Each column and row in cm."""
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    grid = [[0] * cols for _ in range(rows)]
    for p in points:
        c = min(cols - 1, int((p[0] - lo[0]) / (hi[0] - lo[0]) * cols))
        r = min(rows - 1, int((p[2] - lo[2]) / (hi[2] - lo[2]) * rows))
        grid[rows - 1 - r][c] += 1
    log("  X %.1f to %.1f (%.2f a column), Y %.1f to %.1f, Z %.1f to %.1f (%.2f a row)" % (
        lo[0], hi[0], (hi[0] - lo[0]) / cols, lo[1], hi[1], lo[2], hi[2], (hi[2] - lo[2]) / rows))
    for r, row in enumerate(grid):
        log("  %6.1f |%s|" % (hi[2] - (r + 0.5) * (hi[2] - lo[2]) / rows, "".join("#" if n > 3 else ("." if n > 0 else " ") for n in row)))


def plain_metal():
    """The sci-fi rifle's one material: its pack has no textures."""
    path = DEST + "/M_SciFiRifle"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mat = tools.create_asset("M_SciFiRifle", DEST, unreal.Material, unreal.MaterialFactoryNew())
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    colour.set_editor_property("constant", unreal.LinearColor(0.07, 0.085, 0.09, 1.0))
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for prop, value, y in ((unreal.MaterialProperty.MP_ROUGHNESS, 0.38, 200), (unreal.MaterialProperty.MP_METALLIC, 0.9, 300)):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, y)
        node.set_editor_property("r", value)
        mel.connect_material_property(node, "", prop)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


for old in OLD:
    if eal.does_asset_exist(DEST + "/" + old):
        eal.delete_asset(DEST + "/" + old)
        log("removed " + old)

for name, gun in GUNS.items():
    hits = glob.glob(os.path.join(LIBRARY, gun["source"]))
    if not hits:
        raise RuntimeError("%s: %s is not under %s" % (name, gun["source"], LIBRARY))
    folder = DEST + "/" + gun["folder"]
    if eal.does_directory_exist(folder):
        eal.delete_directory(folder)
    task = unreal.AssetImportTask()
    task.filename, task.destination_path = hits[0], folder
    task.automated, task.replace_existing, task.save = True, True, False
    tools.import_asset_tasks([task])
    came = [eal.load_asset(path) for path in eal.list_assets(folder, recursive=True)]
    sources = [each for each in came if isinstance(each, unreal.StaticMesh)]
    if len(sources) != 1:
        raise RuntimeError("%s: the pack came in as %d meshes, not one" % (name, len(sources)))
    source = sources[0]
    materials = [slot.material_interface for slot in source.get_editor_property("static_materials")]
    textures = [each for each in came if isinstance(each, unreal.Texture2D)]

    # every triangle of the source, cut down, turned, centred and scaled
    dyn = unreal.DynamicMesh()
    lod = unreal.GeometryScriptMeshReadLOD()
    lod.set_editor_property("lod_type", unreal.GeometryScriptLODType.MAX_AVAILABLE)
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(source, dyn, unreal.GeometryScriptCopyMeshFromAssetOptions(), lod)
    before = dyn.get_triangle_count()
    unreal.GeometryScript_MeshSimplification.apply_simplify_to_triangle_count(dyn, gun["triangles"], unreal.GeometryScriptSimplifyMeshOptions())
    roll, pitch, yaw = gun["turn"]
    unreal.GeometryScript_MeshTransforms.transform_mesh(dyn, unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(roll, pitch, yaw), unreal.Vector(1, 1, 1)))
    box = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dyn)
    middle = (box.min + box.max) * 0.5
    unreal.GeometryScript_MeshTransforms.translate_mesh(dyn, unreal.Vector(-middle.x, -middle.y, -middle.z))
    scale = gun["length"] / (box.max.x - box.min.x)
    unreal.GeometryScript_MeshTransforms.scale_mesh(dyn, unreal.Vector(scale, scale, scale), unreal.Vector(0, 0, 0))
    if (box.max.x - box.min.x) < max(box.max.y - box.min.y, box.max.z - box.min.z):
        raise RuntimeError("%s: turned as written, the gun is not longest along X" % name)

    path = DEST + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.set_editor_property("enable_nanite", False)       # phones have no Nanite
    options.set_editor_property("enable_collision", True)
    made = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dyn, path, options)
    mesh = [each for each in made if isinstance(each, unreal.StaticMesh)][0] if isinstance(made, tuple) else made
    if mesh is None:
        raise RuntimeError("%s was not made" % name)
    # a plain box around it, so a bullet that touches a gun in a hero's hands lands on that hero
    boxes = unreal.GeometryScriptCollisionFromMeshOptions()
    boxes.set_editor_property("emit_transaction", False)
    boxes.set_editor_property("method", unreal.GeometryScriptCollisionGenerationMethod.ALIGNED_BOXES)
    unreal.GeometryScript_Collision.set_static_mesh_collision_from_mesh(dyn, mesh, boxes)

    if textures:
        # the pack's own materials, their textures drawn no larger than a phone needs
        for texture in textures:
            texture.set_editor_property("max_texture_size", 1024)
            eal.save_loaded_asset(texture)
        for material in materials:
            eal.save_loaded_asset(material)
    else:
        materials = [plain_metal()] * max(1, len(materials))
    slots = len(mesh.get_editor_property("static_materials"))
    if slots != len(materials):
        raise RuntimeError("%s has %d material slots for %d materials" % (name, slots, len(materials)))
    for slot, material in enumerate(materials):
        mesh.set_material(slot, material)
    eal.save_loaded_asset(mesh)
    # the pack's own mesh is not kept: the game draws the one made from it
    eal.delete_asset(source.get_path_name())

    done = mesh.get_bounding_box()
    log("%s: %d triangles of %d, %.1f x %.1f x %.1f cm, materials %s, textures %s" % (
        name, mesh.get_num_triangles(0), before, done.max.x - done.min.x, done.max.y - done.min.y, done.max.z - done.min.z,
        [m.get_name() for m in materials], ["%s %dx%d" % (t.get_name(), t.blueprint_get_size_x(), t.blueprint_get_size_y()) for t in textures]))
    side_view(positions(dyn))

os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
with open(os.path.join(ROOT, "Saved", "import_weapons.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

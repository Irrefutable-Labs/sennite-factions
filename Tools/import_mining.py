"""Bring in what mining is drawn with (Source/Sennite/Public/SenniteMining.h): the mining droid, from
the Fab pack "FREE SciFi Drone", and the steel deposit, from the Fab pack "Slate Rock", into /Game/Mining,
at a phone's size.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/import_mining.py" -unattended -nullrhi

FREE SciFi Drone is one drone as one mesh of about 1.3 million triangles with one material and no
textures of its own. It comes in as SM_MiningDroid, cut to about DROID_TRIANGLES with the engine's own geometry
scripting, wearing M_MiningDroid: dark painted metal.

Slate Rock is one rock, sculpted, in a heavy and a light file, with no textures in the pack. The light
one comes in as SM_SteelDeposit, cut to DEPOSIT_TRIANGLES, solid by its own shape (a shot and a hero meet
it as it is drawn), wearing M_SteelOre: bare metal, so it reads as a seam of ore among the arena's rocks.

The beam the droid mines with is SM_MiningBeam: the engine's own rod (a cylinder a metre long), copied
here so it ships with the game, wearing M_MiningBeam, a colour that glows and takes no light.

Neither pack carries a picture to wear, so both materials are plain colours with no texture. The wood and
stone deposits use models the game already has (DefaultGame.ini).

The packs are looked for in MINING_SOURCE, or the Fab library folder the launcher downloads into. What
each model measures is written to Saved/import_mining.txt."""
import glob
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = os.environ.get("MINING_SOURCE", "/Users/Shared/UnrealEngine/Launcher/VaultCache/FabLibrary")
DEST = "/Game/Mining"
DROID_TRIANGLES = 6000
DEPOSIT_TRIANGLES = 3000

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
lines = []


def note(msg):
    lines.append(str(msg))
    unreal.log("MINING " + str(msg))
    with open(os.path.join(ROOT, "Saved", "import_mining.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


def found(pattern, what):
    hits = glob.glob(os.path.join(LIBRARY, pattern))
    if not hits or not os.path.isfile(hits[0]):
        raise RuntimeError("%s is not under %s (looked for %s)" % (what, LIBRARY, pattern))
    return hits[0]


def fresh(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


def plain(name, colour, metallic, roughness):
    """A material of one colour: how much of a metal it is, and how rough."""
    fresh("%s/%s" % (DEST, name))
    mat = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -200)
    tint.set_editor_property("constant", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
    metal = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 0)
    metal.set_editor_property("r", metallic)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", roughness)
    for node, pin, prop in ((tint, "", P.MP_BASE_COLOR), (metal, "", P.MP_METALLIC), (rough, "", P.MP_ROUGHNESS)):
        if not mel.connect_material_property(node, pin, prop):
            raise RuntimeError("%s: could not connect %s" % (name, prop))
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def model(source, name, mat, triangles, solid):
    """One model file as one static mesh: no Nanite, cut to a phone's size, the given material on every slot."""
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
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = source, DEST, name
    t.automated, t.replace_existing, t.save = True, True, True
    t.options = ui
    tools.import_asset_tasks([t])
    made = eal.load_asset("%s/%s" % (DEST, name))
    if made is None or not isinstance(made, unreal.StaticMesh):
        raise RuntimeError(name + " did not import as a static mesh")
    nanite = made.get_editor_property("nanite_settings")
    if nanite.enabled:                      # plain meshes: phones have no Nanite
        nanite.enabled = False
        made.set_editor_property("nanite_settings", nanite)
    before = made.get_num_triangles(0)
    if before > triangles:
        work = unreal.DynamicMesh()
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(made, work, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
        # the cut does not always reach the count asked for in one pass: it is cut again until it has
        for _ in range(4):
            unreal.GeometryScript_MeshSimplification.apply_simplify_to_triangle_count(work, triangles, unreal.GeometryScriptSimplifyMeshOptions())
            unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(work, made, unreal.GeometryScriptCopyMeshToAssetOptions(), unreal.GeometryScriptMeshWriteLOD())
            if made.get_num_triangles(0) <= triangles * 1.05:
                break
        note("%s: cut from %d to %d triangles" % (name, before, made.get_num_triangles(0)))
    for slot in range(max(1, len(made.get_editor_property("static_materials")))):
        made.set_material(slot, mat)
    unreal.EditorStaticMeshLibrary.remove_collisions(made)
    body = made.get_editor_property("body_setup")
    if body is None:
        raise RuntimeError(name + " has no body setup to collide with")
    if solid:
        # a hero and a shot meet it as it is drawn
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    else:
        # nothing walks into it; a shot meets the box around it
        unreal.EditorStaticMeshLibrary.add_simple_collisions(made, unreal.ScriptCollisionShapeType.BOX)
    made.set_editor_property("body_setup", made.get_editor_property("body_setup"))
    eal.save_loaded_asset(made)
    box = made.get_bounding_box()
    note("%s (%s): x %.0f to %.0f, y %.0f to %.0f, z %.0f to %.0f cm; %d triangles; %d material slots" % (
        name, made.get_path_name(), box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z,
        made.get_num_triangles(0), len(made.get_editor_property("static_materials"))))
    return made


def glowing(name, colour, strength):
    """A material that glows in one colour and takes no light."""
    fresh("%s/%s" % (DEST, name))
    mat = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    tint.set_editor_property("constant", unreal.LinearColor(colour[0] * strength, colour[1] * strength, colour[2] * strength, 1.0))
    if not mel.connect_material_property(tint, "", P.MP_EMISSIVE_COLOR):
        raise RuntimeError("%s: could not connect its glow" % name)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def beam():
    """The engine's rod, copied into the game's own folder, wearing the beam's glow."""
    path = DEST + "/SM_MiningBeam"
    fresh(path)
    rod = eal.duplicate_asset("/Engine/BasicShapes/Cylinder", path)
    if rod is None or not isinstance(rod, unreal.StaticMesh):
        raise RuntimeError("the engine's cylinder could not be copied to " + path)
    rod.set_material(0, glowing("M_MiningBeam", (0.25, 0.85, 1.0), 6.0))
    unreal.EditorStaticMeshLibrary.remove_collisions(rod)
    eal.save_loaded_asset(rod)
    box = rod.get_bounding_box()
    note("SM_MiningBeam (%s): x %.0f to %.0f, y %.0f to %.0f, z %.0f to %.0f cm" % (rod.get_path_name(), box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z))


droid = found(os.path.join("FREE_SciFi_Drone-*", "obj", "*", "Free-Drone", "Free-Drone", "Free_drone", "Drone.FBX"), "the FREE SciFi Drone pack's Drone.FBX")
rock = found(os.path.join("Slate_Rock-*", "fbx", "*", "Slate_Rock_Low.fbx"), "the Slate Rock pack's Slate_Rock_Low.fbx")
model(droid, "SM_MiningDroid", plain("M_MiningDroid", (0.045, 0.05, 0.06), 0.85, 0.38), DROID_TRIANGLES, False)
model(rock, "SM_SteelDeposit", plain("M_SteelOre", (0.33, 0.36, 0.42), 1.0, 0.32), DEPOSIT_TRIANGLES, True)
beam()
note("done")

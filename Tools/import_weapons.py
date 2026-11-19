"""Import the weapons the game uses into /Game/Weapons, from the owner's Fab packs:

  the rifle         "Free Sci-Fi Rifle": one .obj of 1.5 million triangles, with no textures in the pack;
  the energy rifle  "sci-fi Enrgy Rifle (3D Static Model, Game Ready, No Animations)": one .glb of half
                    a million triangles in two materials, a brushed black metal and glowing rings (the
                    pack's own materials drew as black-and-white speckle in the game, so it is painted
                    as the others are: dark metal, its rings glowing pink);
  the shotgun       "Scifi Gun": one .fbx in 41 flat colours, some glowing;
  the sniper        "Sci-Fi Weapon Sniper Gun": the gun in parts, each .fbx one placeholder colour (the
                    pack has no textures);
  the katana        "red scifi katana", and the axe "Scifi axe": one .fbx each, in flat colours and a
                    few textures.

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

# Each weapon: where its parts are in the library, how the source is turned to put its muzzle (or its
# blade's point) along +X and its top along +Z (roll, pitch, yaw), how long it is made in cm, how many
# triangles it is cut down to, and how it is painted:
#   "pack"   the pack's own materials and textures, drawn at 1024;
#   "metal"  one dark metal all over (the pack has no textures and one material);
#   "paint"  each of the pack's flat-coloured materials baked into the model's vertex colours and drawn
#            with one material (M_GunPaint); a material that glows keeps its glow. A material the pack
#            draws with a texture, or a part that has only a placeholder colour, is given its colour here.
GUNS = {
    "SM_SciFiRifle": dict(parts=["Free_Sci-Fi_Rifle-*/obj/*/Free-Rifle/Free_Rifle/Rifle.obj"], folder="SciFiRifle",
                          turn=(90.0, 0.0, 90.0), length=78.0, triangles=30000, look="metal"),
    "SM_EnergyRifle": dict(parts=["sci-fi_Enrgy_Rifle*/glb/energy_weapon.glb"], folder="EnergyRifle",
                           turn=(0.0, 0.0, 180.0), length=62.0, triangles=14000, look="paint",
                           colours={"Brushed_black_metal_brass": (0.07, 0.07, 0.08, 0.0), "Luminous_Edge": (1.0, 0.2, 0.75, 1.0)}),
    "SM_SciFiShotgun": dict(parts=["Scifi_Gun-*/fbx/*/source/scifi gun 2.fbx"], folder="SciFiShotgun",
                            turn=(0.0, 0.0, -90.0), length=70.0, triangles=12000, look="paint", colours={}),
    "SM_Sniper": dict(parts=["Sci-Fi_Weapon_Sniper_Gun-*/fbx/source_extracted/SM_Weapon_Sniper_%d.fbx" % n for n in (1, 2, 3, 4, 6)], folder="Sniper",
                      turn=(0.0, 0.0, 0.0), length=100.0, triangles=12000, look="paint",
                      colours={"M1_1": (0.10, 0.11, 0.13, 0.0), "M2": (0.55, 0.57, 0.6, 0.0), "M3": (0.55, 0.38, 0.16, 0.0),
                               "M4": (0.03, 0.03, 0.035, 0.0), "M6": (0.55, 0.57, 0.6, 0.0)}),
    "SM_Katana": dict(parts=["red_scifi_katana-*/fbx/*/source/red cyber katana.fbx"], folder="Katana",
                      turn=(0.0, 0.0, 0.0), length=105.0, triangles=6000, look="paint",
                      colours={"Grey_Marble": (0.5, 0.52, 0.56, 0.0), "Grey_Marble_001": (0.5, 0.52, 0.56, 0.0), "Grey_Natural_Marble": (0.5, 0.52, 0.56, 0.0)}),
    "SM_Axe": dict(parts=["Scifi_axe-*/fbx/*/source/scifiaxe.fbx"], folder="Axe",
                   turn=(0.0, 0.0, -90.0), length=85.0, triangles=6000, look="paint",
                   colours={"Dirrty_white_plastic": (0.8, 0.8, 0.77, 0.0), "Wood": (0.22, 0.1, 0.045, 0.0), "Metal": (0.42, 0.44, 0.47, 0.0),
                            "White_Marble": (0.85, 0.85, 0.85, 0.0)}),
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

def paint_material():
    """What a painted weapon is drawn with: its vertex colours, glowing where their alpha says."""
    path = DEST + "/M_GunPaint"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mat = tools.create_asset("M_GunPaint", DEST, unreal.Material, unreal.MaterialFactoryNew())
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -700, 0)
    glow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 300)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 5.0)
    lit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, 200)
    bright = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 200)
    ok = (mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
          and mel.connect_material_expressions(colour, "", lit, "A") and mel.connect_material_expressions(colour, "A", lit, "B")
          and mel.connect_material_expressions(lit, "", bright, "A") and mel.connect_material_expressions(glow, "", bright, "B")
          and mel.connect_material_property(bright, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR))
    if not ok:
        raise RuntimeError("the paint material did not wire up")
    for prop, value, y in ((unreal.MaterialProperty.MP_ROUGHNESS, 0.42, 450), (unreal.MaterialProperty.MP_METALLIC, 0.55, 550)):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, y)
        node.set_editor_property("r", value)
        mel.connect_material_property(node, "", prop)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def paint(dyn, materials, colours, name):
    """Bake each material's colour into the vertex colours of its triangles; the mesh is then one material."""
    flags = unreal.GeometryScriptColorFlags()
    unreal.GeometryScript_VertexColors.set_mesh_constant_vertex_color(dyn, unreal.LinearColor(0.3, 0.3, 0.3, 0.0), flags, clear_existing=True)
    for index, material in enumerate(materials):
        slot = material.get_name()
        if slot in colours:
            r, g, b, a = colours[slot]
        else:
            vectors = {str(n): mel.get_material_instance_vector_parameter_value(material, n) for n in mel.get_vector_parameter_names(material)}
            lit = vectors.get("EmissiveColor")
            base = vectors.get("DiffuseColor") or vectors.get("BaseColorFactor")
            if lit is not None and max(lit.r, lit.g, lit.b) > 0.02:
                # a glow brighter than white keeps its colour: a vertex colour holds no more than 1
                most = max(1.0, lit.r, lit.g, lit.b)
                r, g, b, a = lit.r / most, lit.g / most, lit.b / most, 1.0
            elif base is not None:
                r, g, b, a = base.r, base.g, base.b, 0.0
            else:
                raise RuntimeError("%s: material %s has no colour to paint with" % (name, slot))
        picked = unreal.GeometryScript_MeshSelection.select_mesh_elements_by_material_id(dyn, index, unreal.GeometryScriptMeshSelectionType.TRIANGLES)
        picked = [each for each in picked if isinstance(each, unreal.GeometryScriptMeshSelection)][0] if isinstance(picked, tuple) else picked
        unreal.GeometryScript_VertexColors.set_mesh_selection_vertex_color(dyn, picked, unreal.LinearColor(r, g, b, a), flags, create_color_seam=True)
        reach = unreal.GeometryScript_MeshSelectionQueries.get_mesh_selection_bounding_box(dyn, picked)
        reach = [each for each in reach if isinstance(each, unreal.Box)][0] if isinstance(reach, tuple) else reach
        log("  paint %-24s (%.2f, %.2f, %.2f)%s  spans (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f)" % (
            slot, r, g, b, " glows" if a > 0.5 else "", reach.min.x, reach.min.y, reach.min.z, reach.max.x, reach.max.y, reach.max.z))
    unreal.GeometryScript_Materials.set_all_triangle_material_i_ds(dyn, unreal.GeometryScript_List.convert_array_to_index_list([0] * dyn.get_triangle_count()))


painted = None
# the mesh-editing function that adds one mesh's triangles to another, wherever this engine keeps it
APPEND = [getattr(getattr(unreal, each), "append_mesh") for each in dir(unreal) if each.startswith("GeometryScript_") and hasattr(getattr(unreal, each), "append_mesh")][0]
for name, gun in GUNS.items():
    folder = DEST + "/" + gun["folder"]
    if eal.does_directory_exist(folder):
        eal.delete_directory(folder)
    dyn = unreal.DynamicMesh()
    materials, textures, sources = [], [], []
    lod = unreal.GeometryScriptMeshReadLOD()
    lod.set_editor_property("lod_type", unreal.GeometryScriptLODType.MAX_AVAILABLE)
    for part in gun["parts"]:
        hits = glob.glob(os.path.join(LIBRARY, part))
        if not hits:
            raise RuntimeError("%s: %s is not under %s" % (name, part, LIBRARY))
        into = folder + "/" + os.path.splitext(os.path.basename(hits[0]))[0].replace(" ", "_")
        task = unreal.AssetImportTask()
        task.filename, task.destination_path = hits[0], into
        task.automated, task.replace_existing, task.save = True, True, False
        tools.import_asset_tasks([task])
        came = [eal.load_asset(path) for path in eal.list_assets(into, recursive=True)]
        meshes = [each for each in came if isinstance(each, unreal.StaticMesh)]
        if len(meshes) != 1:
            raise RuntimeError("%s: %s came in as %d meshes, not one" % (name, part, len(meshes)))
        textures += [each for each in came if isinstance(each, unreal.Texture2D)]
        sources.append(meshes[0])
        # this part's triangles, its materials numbered after those of the parts before it
        one = unreal.DynamicMesh()
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(meshes[0], one, unreal.GeometryScriptCopyMeshFromAssetOptions(), lod)
        own = [slot.material_interface for slot in meshes[0].get_editor_property("static_materials")]
        if materials:
            unreal.GeometryScript_Materials.remap_material_i_ds(one, 0, len(materials)) if len(own) == 1 else None
            if len(own) != 1:
                raise RuntimeError("%s: a part after the first has %d materials; only one is handled" % (name, len(own)))
        APPEND(dyn, one, unreal.Transform())
        materials += own

    # every triangle of the source, painted, cut down, turned, centred and scaled
    before = dyn.get_triangle_count()
    if gun["look"] == "paint":
        paint(dyn, materials, gun.get("colours", {}), name)
    if before > gun["triangles"]:
        unreal.GeometryScript_MeshSimplification.apply_simplify_to_triangle_count(dyn, gun["triangles"], unreal.GeometryScriptSimplifyMeshOptions())
    roll, pitch, yaw = gun["turn"]
    unreal.GeometryScript_MeshTransforms.transform_mesh(dyn, unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(roll, pitch, yaw), unreal.Vector(1, 1, 1)))
    box = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(dyn)
    middle = (box.min + box.max) * 0.5
    unreal.GeometryScript_MeshTransforms.translate_mesh(dyn, unreal.Vector(-middle.x, -middle.y, -middle.z))
    scale = gun["length"] / (box.max.x - box.min.x)
    unreal.GeometryScript_MeshTransforms.scale_mesh(dyn, unreal.Vector(scale, scale, scale), unreal.Vector(0, 0, 0))
    if (box.max.x - box.min.x) < max(box.max.y - box.min.y, box.max.z - box.min.z):
        raise RuntimeError("%s: turned as written, it is not longest along X" % name)

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
    # a plain box around it, so a bullet that touches a weapon in a hero's hands lands on that hero
    boxes = unreal.GeometryScriptCollisionFromMeshOptions()
    boxes.set_editor_property("emit_transaction", False)
    boxes.set_editor_property("method", unreal.GeometryScriptCollisionGenerationMethod.ALIGNED_BOXES)
    unreal.GeometryScript_Collision.set_static_mesh_collision_from_mesh(dyn, mesh, boxes)

    if gun["look"] == "pack":
        # the pack's own materials, their textures drawn no larger than a phone needs
        for texture in textures:
            texture.set_editor_property("max_texture_size", 1024)
            eal.save_loaded_asset(texture)
        for material in materials:
            eal.save_loaded_asset(material)
        wears = materials
    elif gun["look"] == "paint":
        painted = painted or paint_material()
        wears = [painted]
    else:
        wears = [plain_metal()]
    slots = len(mesh.get_editor_property("static_materials"))
    if slots != len(wears):
        raise RuntimeError("%s has %d material slots for %d materials" % (name, slots, len(wears)))
    for slot, material in enumerate(wears):
        mesh.set_material(slot, material)
    eal.save_loaded_asset(mesh)
    # the pack's own meshes are not kept, nor what a painted or plain weapon does not use
    for source in sources:
        eal.delete_asset(source.get_path_name())
    if gun["look"] != "pack" and eal.does_directory_exist(folder):
        eal.delete_directory(folder)

    done = mesh.get_bounding_box()
    log("%s: %d triangles of %d, %.1f x %.1f x %.1f cm, %s" % (
        name, mesh.get_num_triangles(0), before, done.max.x - done.min.x, done.max.y - done.min.y, done.max.z - done.min.z, gun["look"]))
    side_view(positions(dyn), 62, 18)

os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
with open(os.path.join(ROOT, "Saved", "import_weapons.txt"), "w") as fh:
    fh.write("\n".join(lines) + "\n")

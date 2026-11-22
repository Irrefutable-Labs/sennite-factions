"""Bring the vehicles that cross the traffic road into /Game/Traffic, from the Fab pack "Vehicle
Variety Pack": a hatchback, a pickup, an SUV, a sports car and a box truck.

  UnrealEditor-Cmd Sennite.uproject -ExecutePythonScript="$PWD/Tools/import_vehicles.py" -unattended -nosplash -nullrhi

It runs in the editor itself, not as the other import scripts do (-run=pythonscript): cutting a model
down is the editor's own tool, which that way of running a script does not have. It closes the
editor when it is done. The editor, opened this way, writes Config/DefaultInput.ini out again in its
own spelling: put that file back afterwards (git checkout -- Config/DefaultInput.ini).

The pack is an Unreal one: it comes as assets, not as files a script can read, in whatever project
the launcher added it to. VEHICLE_SOURCE names that project's Content/VehicleVarietyPack folder
(by default the look-dev project's, beside this checkout). The script copies the pack's meshes,
materials and textures into this project under the pack's own path, so they find one another, makes
what the game uses from them, and takes the copy away again: nothing of the pack itself stays in the
game.

Each vehicle comes in as one static mesh, SM_Traffic_*, standing on its wheels at height 0 with its
nose along +X, as the pack has it. The pack's models are 50,000 to 106,000 triangles with four
lighter levels under that, which the engine made and keeps no shape of to copy. A road has up to
90 vehicles on it at once, on a phone, so the game's model is the pack's cut down by the engine to
the size of the pack's own lightest level, and to no more than NEAREST triangles, with two lighter
levels for farther off. No Nanite: phones have none. No collision: a vehicle is felt through the
match server's own sums (ASenniteArena::Tick), not through collision.

Each part keeps the pack's own textures, shrunk to a phone's size: colour, normal, and one map of
occlusion, roughness and metal. What a player sees most of (the body, the truck's box) is 1024; the
underside and the inside are 256. The pack's 4096 px sets come to 1.1 GB; what is kept is a few MB.
The windows are one dark glass every vehicle shares, not see-through: see-through surfaces are what
a phone draws slowest, and there are hundreds of them on a road. The pack's lamp glow maps are left out.

What it made is written to Saved/import_vehicles.txt."""
import os
import shutil
import subprocess
import traceback

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = "VehicleVarietyPack"
SOURCE = os.environ.get("VEHICLE_SOURCE") or os.path.join(os.path.dirname(ROOT), "claymation-lookdev", "Content", PACK)
COPY = os.path.join(ROOT, "Content", PACK)
SMALL = os.path.join(ROOT, "Saved", "VehicleImport")
DEST = "/Game/Traffic"
# The pack's models, by the names it gives them, and what each is called here.
MODELS = {
    "SM_Hatchback": "Hatchback",
    "SM_Pickup": "Pickup",
    "SM_SUV": "SUV",
    "SM_SportsCar": "SportsCar",
    "SM_Truck_Box": "BoxTruck",
}
# A part of a vehicle, by a whole word of the name the pack gives its material, and the size its
# textures come in at. Glass has no textures here.
PARTS = (("Glass", 0), ("Body", 1024), ("Box", 1024), ("Interior", 256), ("Under", 256), ("Chassis", 256))
# The three maps a part keeps, by a word of the pack's texture names.
MAPS = (("Colour", ("Diffuse", "BaseColor")), ("Normal", ("Normal",)), ("Packed", ("ORM",)))
# The most triangles a vehicle is drawn with, close up.
NEAREST = 8000
# How much of that the farther levels keep, and how small on the screen each takes over.
FARTHER = ((0.4, 0.3), (0.15, 0.1))

eal = unreal.EditorAssetLibrary
meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
P = unreal.MaterialProperty
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log("VEHICLES " + str(msg))


def part_of(material_name):
    # by one whole word of the name: the box truck's every material has "Box" in it
    named = [word.lower() for word in material_name.split("_")]
    for word, size in PARTS:
        if word.lower() in named:
            return word, size
    raise RuntimeError("the pack has a material this script does not know the part of: " + material_name)


def pack_textures(material):
    """The pack's textures a material uses, by the map each is: the colour, the normal, the packed one."""
    wants = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True, include_hard_package_references=True, include_searchable_names=False,
        include_soft_management_references=False, include_hard_management_references=False)
    found = {}
    for package in registry.get_dependencies(material.get_outermost().get_name(), wants) or []:
        package = str(package)
        if "/Textures/" not in package:
            continue
        # a map is named by one whole word of the texture's name: "Normal" holds the letters of "ORM"
        named = [word.lower() for word in package.rsplit("/", 1)[1].split("_")]
        for kind, words in MAPS:
            if any(word.lower() in named for word in words):
                if kind in found:
                    raise RuntimeError("%s uses two %s maps: %s and %s" % (material.get_name(), kind, found[kind], package))
                found[kind] = package
    missing = [kind for kind, _ in MAPS if kind not in found]
    if missing:
        raise RuntimeError("%s has no %s map among %s" % (material.get_name(), ", ".join(missing), sorted(found.values())))
    return found


def texture(vehicle, part, kind, package, size):
    """One of the pack's maps, written out, shrunk and brought in as the game's own."""
    source = eal.load_asset(package)
    if source is None:
        raise RuntimeError("the pack's texture did not load: " + package)
    whole = os.path.join(SMALL, "%s_%s_%s_whole.png" % (vehicle, part, kind))
    small = os.path.join(SMALL, "%s_%s_%s.png" % (vehicle, part, kind))
    task = unreal.AssetExportTask()
    task.object, task.filename, task.exporter = source, whole, unreal.TextureExporterPNG()
    task.automated, task.prompt, task.replace_identical = True, False, True
    if not unreal.Exporter.run_asset_export_task(task) or not os.path.isfile(whole):
        raise RuntimeError("the pack's texture was not written out: " + package)
    subprocess.run(["/usr/bin/sips", "-Z", str(size), whole, "--out", small], check=True, capture_output=True)
    os.remove(whole)
    name = "T_Traffic_%s_%s_%s" % (vehicle, part, kind)
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.destination_name = small, DEST, name
    t.automated, t.replace_existing, t.save = True, True, True
    tools.import_asset_tasks([t])
    made = eal.load_asset("%s/%s" % (DEST, name))
    if made is None:
        raise RuntimeError("texture did not import: " + name)
    if kind == "Normal":
        made.set_editor_property("srgb", False)
        made.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "Packed":
        made.set_editor_property("srgb", False)
        made.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(made)
    return made


def fresh_material(name):
    path = "%s/%s" % (DEST, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())


def part_material(vehicle, part, size, source_material):
    """One part's material: the pack's colour, normal, and occlusion / roughness / metal."""
    name = "M_Traffic_%s_%s" % (vehicle, part)
    maps = pack_textures(source_material)
    mat = fresh_material(name)

    def sample(kind, sampler, y):
        read = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -500, y)
        read.set_editor_property("texture", texture(vehicle, part, kind, maps[kind], size))
        read.set_editor_property("sampler_type", sampler)
        return read

    colour = sample("Colour", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -300)
    normal = sample("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 0)
    packed = sample("Packed", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 300)
    for src, out, prop in ((colour, "RGB", P.MP_BASE_COLOR), (normal, "RGB", P.MP_NORMAL), (packed, "R", P.MP_AMBIENT_OCCLUSION),
                           (packed, "G", P.MP_ROUGHNESS), (packed, "B", P.MP_METALLIC)):
        if not mel.connect_material_property(src, out, prop):
            raise RuntimeError("could not connect a map of " + name)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def glass_material():
    """The windows of every vehicle: dark, smooth, and not see-through."""
    mat = fresh_material("M_Traffic_Glass")
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -200)
    colour.set_editor_property("constant", unreal.LinearColor(0.012, 0.016, 0.022, 1.0))
    smooth = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 0)
    smooth.set_editor_property("r", 0.08)
    shine = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
    shine.set_editor_property("r", 0.6)
    for src, prop in ((colour, P.MP_BASE_COLOR), (smooth, P.MP_ROUGHNESS), (shine, P.MP_SPECULAR)):
        if not mel.connect_material_property(src, "", prop):
            raise RuntimeError("could not connect the glass")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def vehicle(pack_name, name, glass):
    source = eal.load_asset("/Game/%s/Meshes/%s" % (PACK, pack_name))
    if source is None or not isinstance(source, unreal.StaticMesh):
        raise RuntimeError("the pack's %s did not load as a static mesh" % pack_name)
    path = "%s/SM_Traffic_%s" % (DEST, name)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mesh = eal.duplicate_asset(source.get_path_name(), path)
    if mesh is None:
        raise RuntimeError("could not make " + path)
    # the engine cuts the pack's whole model down: close up to the size of the pack's lightest level
    # (NEAREST at most), and farther off to a part of that
    whole = source.get_num_triangles(0)
    nearest = min(source.get_num_triangles(source.get_num_lods() - 1), NEAREST) / float(whole)
    meshes.remove_lods(mesh)
    levels = unreal.StaticMeshReductionOptions()
    levels.auto_compute_lod_screen_size = False
    steps = [unreal.StaticMeshReductionSettings(percent_triangles=nearest, screen_size=1.0)]
    # a farther level's share is of the level the player sees close up, not of the pack's whole model
    steps += [unreal.StaticMeshReductionSettings(percent_triangles=keep, screen_size=at) for keep, at in FARTHER]
    levels.reduction_settings = steps
    if meshes.set_lods(mesh, levels) < 0:
        raise RuntimeError("could not make the levels of " + name)
    if mesh.get_num_triangles(0) > NEAREST * 1.1:
        raise RuntimeError("%s is still %d triangles close up, of the pack's %d" % (name, mesh.get_num_triangles(0), whole))
    meshes.remove_collisions(mesh)
    nanite = mesh.get_editor_property("nanite_settings")
    if nanite.enabled:
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)
    slots = mesh.get_editor_property("static_materials")
    for index, slot in enumerate(slots):
        pack_material = slot.get_editor_property("material_interface")
        if pack_material is None:
            raise RuntimeError("%s has a slot with no material: %s" % (pack_name, slot.get_editor_property("material_slot_name")))
        part, size = part_of(pack_material.get_name())
        mesh.set_material(index, glass if size == 0 else part_material(name, part, size, pack_material))
        log("  slot %d %s: %s" % (index, pack_material.get_name(), "the shared glass" if size == 0 else "%s at %d px" % (part, size)))
    eal.save_loaded_asset(mesh)
    box = mesh.get_bounding_box()
    log("SM_Traffic_%s: %.0f long, %.0f wide, %.0f high cm, from %.0f to %.0f in height; triangles by level: %s (the pack's: %s)" % (
        name, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z, box.min.z, box.max.z,
        ", ".join(str(mesh.get_num_triangles(level)) for level in range(mesh.get_num_lods())),
        ", ".join(str(source.get_num_triangles(level)) for level in range(source.get_num_lods()))))


def main():
    if meshes is None:
        raise RuntimeError("run this in the editor (-ExecutePythonScript=), not with -run=pythonscript: cutting a model down is the editor's tool")
    if not os.path.isdir(SOURCE):
        raise RuntimeError("the Vehicle Variety Pack is not at %s: set VEHICLE_SOURCE to the Content/%s folder of the project it was added to" % (SOURCE, PACK))
    os.makedirs(SMALL, exist_ok=True)
    if not os.path.isdir(COPY):
        for folder in ("Meshes", "Materials", "Textures"):
            shutil.copytree(os.path.join(SOURCE, folder), os.path.join(COPY, folder))
        registry.scan_paths_synchronous(["/Game/" + PACK], True)
    log("the pack: " + SOURCE)
    glass = glass_material()
    for pack_name, name in MODELS.items():
        vehicle(pack_name, name, glass)
    # the pack's own assets go: the game keeps only what was made from them
    if not eal.delete_directory("/Game/" + PACK):
        log("the pack's copy was not taken out of the project by the editor: its folder is removed from the disk")
    shutil.rmtree(COPY, ignore_errors=True)


try:
    main()
except Exception:
    log("FAILED\n" + traceback.format_exc())
    raise
finally:
    with open(os.path.join(ROOT, "Saved", "import_vehicles.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    unreal.SystemLibrary.quit_editor()

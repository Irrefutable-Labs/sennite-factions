"""Build the menu's map: a grass hilltop that stands out of the lake the arena floats over, rocks and
pines at its back and the mountains round it, where the character select turns the chosen hero and the
lobby stands every player's hero in a row.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/build_menu.py" -unattended -nullrhi

The camera and the heroes are the game's own (SenniteMenu.cpp): the map has the ground, the scenery,
the light and the game mode (the menu; ?game=lobby makes it the lobby). The scenery is the arena's own
(Tools/import_land.py, import_rocks.py, import_trees.py, import_scenery.py), set by hand here: the
camera looks along +X at the heroes, who stand at the origin, so what is seen behind them lies at +X.
Phone-level lighting only, as the test range: one shadowed sun, an unshadowed fill from a fixed
daylight sky. Writes /Game/Maps/Menu."""
import math
import os

import unreal

MAP = "/Game/Maps/Menu"
CYLINDER = "/Engine/BasicShapes/Cylinder"  # 100 cm across, 100 cm high, centred
GRASS = "/Game/Scenery/Grass/M_Grass"

eal = unreal.EditorAssetLibrary
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cylinder = eal.load_asset(CYLINDER)
grass = eal.load_asset(GRASS)
if cylinder is None or grass is None:
    raise RuntimeError("the cylinder or the grass did not load")


def spawn(cls, label, loc=unreal.Vector(0, 0, 0), rot=unreal.Rotator(0, 0, 0), folder=None):
    a = actors.spawn_actor_from_class(cls, loc, rot)
    a.set_actor_label(label)
    if folder:
        a.set_folder_path(folder)
    return a


# the stage: a grass hilltop 36 m across, its top at z = 0, standing out of the lake
STAGE = 1800.0   # its radius, cm
LAKE = -900.0    # the lake's surface, below the top
disc = spawn(unreal.StaticMeshActor, "stage", unreal.Vector(0, 0, -1500.0), folder="stage")
c = disc.static_mesh_component
c.set_static_mesh(cylinder)
c.set_material(0, grass)
disc.set_actor_scale3d(unreal.Vector(STAGE / 50.0, STAGE / 50.0, 30.0))
c.set_mobility(unreal.ComponentMobility.STATIC)

# the country round it: the arena's own mountains and lake (Tools/import_land.py: the lake's surface is
# the meshes' height 0, its widest water over their origin), as large as the arena sets them
for label, path in (("land", "/Game/Scenery/Land/SM_Land"), ("water", "/Game/Scenery/Land/SM_LandWater")):
    mesh = unreal.load_asset(path)
    if mesh is None:
        raise RuntimeError(path + " did not load")
    a = spawn(unreal.StaticMeshActor, label, unreal.Vector(0, 0, LAKE), folder="country")
    a.static_mesh_component.set_static_mesh(mesh)
    a.static_mesh_component.set_editor_property("cast_shadow", False)
    a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(unreal.Vector(1.5, 1.5, 1.5))
    a.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)


def stand(name, x, y, yaw=0.0, tall=None, scale=None, sink=0.0, shadow=True):
    """Stand a piece of scenery on the hilltop: tall is how high it is made, in cm (or scale, its own size
    times that); its lowest point is put sink below the grass."""
    mesh = unreal.load_asset("/Game/Scenery/" + name)
    if mesh is None:
        raise RuntimeError(name + " did not load")
    b = mesh.get_bounds()
    k = scale if scale is not None else tall / (2.0 * b.box_extent.z)
    low = (b.origin.z - b.box_extent.z) * k
    a = spawn(unreal.StaticMeshActor, name.split("/")[-1], unreal.Vector(x, y, -low - sink), unreal.Rotator(0, 0, yaw), "scenery")
    m = a.static_mesh_component
    m.set_static_mesh(mesh)
    m.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    m.set_editor_property("cast_shadow", shadow)
    a.set_actor_scale3d(unreal.Vector(k, k, k))
    m.set_mobility(unreal.ComponentMobility.STATIC)
    return a


# Behind the heroes, to the camera's left: a crag of three rocks with two pines over it.
stand("Rocks/SM_Rock_11", 1250, -950, 20, tall=560, sink=40)
stand("Rocks/SM_Rock_03", 1050, -1330, 110, tall=380, sink=30)
stand("Rocks/SM_Rock_02", 1480, -560, 250, tall=300, sink=30)
stand("Trees/SM_Tree_PineTall", 1620, -1250, 40, scale=1.0)
stand("Trees/SM_Tree_Pine", 1700, -700, 200, scale=0.8)
stand("Trees/SM_Bush_Big", 980, -620, 70, scale=0.8)
# To the right: a broadleaf, a pine and a small tree, a rock at their feet and bushes before them.
stand("Trees/SM_Tree_Broadleaf", 1450, 760, 130, scale=1.0)
stand("Trees/SM_Tree_Pine", 1680, 1250, 300, scale=0.9)
stand("Trees/SM_Tree_Small", 1150, 1180, 10, scale=1.0)
stand("Rocks/SM_Rock_04", 1020, 640, 300, tall=250, sink=25)
stand("Rocks/SM_Rock_07", 1180, 420, 160, tall=130, sink=15)
stand("Trees/SM_Bush_LeafMid", 880, 880, 40, scale=0.9)
stand("Trees/SM_Bush_Flowers", 760, 520, 0, scale=0.9)
# Between them the way is open to the lake and the mountains; low stones and stony ground lie nearer.
stand("Tundra/SM_Tundra", 620, -330, 30, scale=1.0, sink=4, shadow=False)
stand("RockyGround/SM_RockyGround", 460, 420, 200, scale=1.0, sink=3, shadow=False)
stand("Rocks/SM_Rock_10", 520, -300, 80, tall=55, sink=8)
stand("Rocks/SM_Rock_12", 380, 330, 200, tall=45, sink=6)
stand("Rocks/SM_Rock_01", 300, -420, 320, tall=38, sink=6)
stand("Trees/SM_Bush_Small", 700, -760, 100, scale=1.0)
stand("Trees/SM_Bush_LeafSmall", 640, 170, 220, scale=0.8)
# The hilltop's rim, behind and to the sides: rocks along it, so the grass ends in stone and not in a line.
RIM = ("Rocks/SM_Rock_06", "Rocks/SM_Rock_02", "Rocks/SM_Rock_05", "Rocks/SM_Rock_11", "Rocks/SM_Rock_08", "Rocks/SM_Rock_04", "Rocks/SM_Rock_07", "Rocks/SM_Rock_03")
for index in range(21):
    angle = math.radians(-100.0 + index * 10.0)
    high = (150.0, 260.0, 120.0, 210.0, 170.0)[index % 5]
    stand(RIM[index % len(RIM)], math.cos(angle) * (STAGE - 60.0), math.sin(angle) * (STAGE - 60.0), index * 67.0, tall=high, sink=high * 0.2)

# light: one shadowed sun shining from behind the camera's left shoulder onto the heroes' faces (the camera
# looks along +X and the heroes face it), and an unshadowed sky
sun = spawn(unreal.DirectionalLight, "sun", unreal.Vector(0, 0, 1000), unreal.Rotator(0, -38, 32), "lights")  # roll, pitch, yaw
sc = sun.light_component
sc.set_mobility(unreal.ComponentMobility.MOVABLE)
for key, value in (("intensity", 6.0), ("atmosphere_sun_light", True), ("dynamic_shadow_distance_movable_light", 3000.0),
                   ("dynamic_shadow_cascades", 2), ("cast_shadows", True)):
    sc.set_editor_property(key, value)

# The sky fill is the engine's daylight sky, as a fixed texture: it lights the shaded side of a hero from
# the first frame, on a phone as on a desktop (a sky captured live is black until it is captured, and a
# phone may not capture one at all), and from below as well as above, so nothing in shade is black.
daylight = unreal.load_asset("/Engine/MapTemplates/Sky/DaylightAmbientCubemap")  # by its path: the registry has not scanned the engine's templates
if daylight is None:
    raise RuntimeError("the engine's daylight sky did not load")
sky = spawn(unreal.SkyLight, "sky_fill", unreal.Vector(0, 0, 1200), folder="lights")
kc = sky.light_component
kc.set_mobility(unreal.ComponentMobility.MOVABLE)
for key, value in (("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP), ("cubemap", daylight),
                   ("real_time_capture", False), ("lower_hemisphere_is_black", False), ("cast_shadows", False), ("intensity", 2.2)):
    kc.set_editor_property(key, value)

atmosphere = spawn(unreal.SkyAtmosphere, "atmosphere", folder="lights")
# a daytime sky: the sun here is weak (phone-level lighting), so the sky it lights is brightened on its own
SKY_BRIGHTNESS = float(os.environ.get("SKY_BRIGHTNESS", "6.0"))
atmosphere.get_component_by_class(unreal.SkyAtmosphereComponent).set_editor_property(
    "sky_luminance_factor", unreal.LinearColor(SKY_BRIGHTNESS, SKY_BRIGHTNESS, SKY_BRIGHTNESS, 1.0))
# A phone draws the SkyAtmosphere only on a mesh whose material is marked as sky: the engine's own sky
# dome and its sky material, 20 km out, seen and nothing else.
dome = spawn(unreal.StaticMeshActor, "sky_dome", folder="lights")
dome_mesh = dome.static_mesh_component
dome_mesh.set_static_mesh(unreal.load_asset("/Engine/EngineSky/SM_SkySphere"))
dome_mesh.set_material(0, unreal.load_asset("/Engine/EngineSky/SkyAtmosphere/SkyAtmosphere_MaterialSkyDome"))
dome_mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
dome_mesh.set_editor_property("cast_shadow", False)
dome.set_actor_scale3d(unreal.Vector(500.0, 500.0, 500.0))
if dome_mesh.static_mesh is None or not dome_mesh.get_material(0).get_editor_property("is_sky"):
    raise RuntimeError("the sky dome did not get its mesh and sky material")
# a thin haze on the lake, thinning slowly upward, as on the arena: the hilltop is clear, the mountains pale with distance
fog = spawn(unreal.ExponentialHeightFog, "haze", unreal.Vector(0, 0, LAKE), folder="lights")
fog.component.set_editor_property("fog_density", 0.0015)
fog.component.set_editor_property("fog_height_falloff", 0.05)

ppv = spawn(unreal.PostProcessVolume, "exposure", folder="lights")
ppv.set_editor_property("unbound", True)
s = ppv.get_editor_property("settings")
for key, value in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                   ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.8),
                   ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False)):
    s.set_editor_property(key, value)
ppv.set_editor_property("settings", s)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property("default_game_mode", unreal.MainMenuGameMode)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("map did not save")
unreal.log("MENU saved " + MAP)

"""Build the menu's map: a round grass stage under the open sky, where the character select turns the
chosen hero and the lobby stands every player's hero in a row.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/build_menu.py" -unattended -nullrhi

The camera and the heroes are the game's own (SenniteMenu.cpp): the map has the ground, the light and
the game mode (the menu; ?game=lobby makes it the lobby). Phone-level lighting only, as the test range:
one shadowed sun, an unshadowed sky fill. Writes /Game/Maps/Menu."""
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


# the stage: a grass disc 40 m across, its top at z = 0, and a wider one under it to the horizon
for label, across, deep in (("stage", 4000.0, 40.0), ("field", 60000.0, 20.0)):
    disc = spawn(unreal.StaticMeshActor, label, unreal.Vector(0, 0, -deep / 2.0 - (0 if label == "stage" else 40.0)), folder="stage")
    c = disc.static_mesh_component
    c.set_static_mesh(cylinder)
    c.set_material(0, grass)
    disc.set_actor_scale3d(unreal.Vector(across / 100.0, across / 100.0, deep / 100.0))
    c.set_mobility(unreal.ComponentMobility.STATIC)

# light: one shadowed sun from ahead and to the left of the camera (which looks along +X), and an unshadowed sky
sun = spawn(unreal.DirectionalLight, "sun", unreal.Vector(0, 0, 1000), unreal.Rotator(0, -35, 150), "lights")  # roll, pitch, yaw
sc = sun.light_component
sc.set_mobility(unreal.ComponentMobility.MOVABLE)
for key, value in (("intensity", 6.0), ("atmosphere_sun_light", True), ("dynamic_shadow_distance_movable_light", 3000.0),
                   ("dynamic_shadow_cascades", 2), ("cast_shadows", True)):
    sc.set_editor_property(key, value)

sky = spawn(unreal.SkyLight, "sky_fill", unreal.Vector(0, 0, 1200), folder="lights")
kc = sky.light_component
kc.set_mobility(unreal.ComponentMobility.MOVABLE)
for key, value in (("real_time_capture", True), ("cast_shadows", False), ("intensity", 1.2)):
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
fog = spawn(unreal.ExponentialHeightFog, "haze", unreal.Vector(0, 0, 0), folder="lights")
fog.component.set_editor_property("fog_density", 0.01)

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

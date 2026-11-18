"""Build the menu's map: a round grass stage under the open sky, where the character select turns the
chosen hero and the lobby stands every player's hero in a row.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/build_menu.py" -unattended -nullrhi

The camera and the heroes are the game's own (SenniteMenu.cpp): the map has the ground, the light and
the game mode (the menu; ?game=lobby makes it the lobby). Phone-level lighting only, as the test range:
one shadowed sun, an unshadowed fill from a fixed daylight sky. Writes /Game/Maps/Menu."""
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

spawn(unreal.SkyAtmosphere, "atmosphere", folder="lights")
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

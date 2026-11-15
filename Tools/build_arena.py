"""Build the arena map: sky, light, and one ASenniteArena, which lays out the bases and the connectors
between them from a seed when a match starts. Nothing else is placed: the arena is all there is to
stand on, and below it is the drop.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/build_arena.py -unattended -nullrhi

Phone-level lighting only: one shadowed sun, an unshadowed sky fill, no ray tracing, no Lumen.
Writes /Game/Maps/Arena and the arena's block material /Game/Arena/M_ArenaBlock."""
import unreal

MAP = "/Game/Maps/Arena"
BLOCK = "/Game/Arena/M_ArenaBlock"
CELL = 480.0

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def build_block_material():
    """What every block of the arena is drawn with: a checker in world space, one square to a build cell,
    in the colour the arena gives each sort of block (the Tint parameter)."""
    if eal.does_asset_exist(BLOCK):
        eal.delete_asset(BLOCK)
    folder, name = BLOCK.rsplit("/", 1)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    pos = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, 0)
    nrm = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, 150)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, 300)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.4, 0.4, 0.4, 1.0))
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -550, 0)
    # the axis a face looks along is left out, so a face lying on a cell edge does not flicker
    node.set_editor_property("code", """
float3 c = floor(P / %.1f + 0.5);
float3 n = abs(N);
float k = n.z > 0.7 ? c.x + c.y : (n.x > 0.7 ? c.y + c.z : c.x + c.z);
float odd = abs(fmod(k, 2.0));
return T * lerp(0.78, 1.0, odd);
""" % CELL)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("description", "tinted build-grid checker")
    pins = []
    for pin_name in ("P", "N", "T"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    node.set_editor_property("inputs", pins)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -550, 300)
    rough.set_editor_property("r", 0.85)
    ok = (mel.connect_material_expressions(pos, "", node, "P") and mel.connect_material_expressions(nrm, "", node, "N")
          and mel.connect_material_expressions(tint, "", node, "T")
          and mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
          and mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS))
    if not ok:
        raise RuntimeError("arena block material did not wire up")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


build_block_material()
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def spawn(cls, label, loc=unreal.Vector(0, 0, 0), rot=unreal.Rotator(0, 0, 0), folder=None):
    a = actors.spawn_actor_from_class(cls, loc, rot)
    a.set_actor_label(label)
    if folder:
        a.set_folder_path(folder)
    return a


spawn(unreal.SenniteArena, "arena", folder="arena")

# light: one shadowed sun and an unshadowed sky
sun = spawn(unreal.DirectionalLight, "sun", unreal.Vector(0, 0, 3000), unreal.Rotator(0, -48, 115), "lights")  # roll, pitch, yaw
sc = sun.light_component
sc.set_mobility(unreal.ComponentMobility.MOVABLE)
for key, value in (("intensity", 6.0), ("atmosphere_sun_light", True), ("dynamic_shadow_distance_movable_light", 9000.0),
                   ("dynamic_shadow_cascades", 3), ("cast_shadows", True)):
    sc.set_editor_property(key, value)

sky = spawn(unreal.SkyLight, "sky_fill", unreal.Vector(0, 0, 3200), folder="lights")
kc = sky.light_component
kc.set_mobility(unreal.ComponentMobility.MOVABLE)
for key, value in (("real_time_capture", True), ("cast_shadows", False), ("intensity", 1.0)):
    kc.set_editor_property(key, value)

spawn(unreal.SkyAtmosphere, "atmosphere", folder="lights")
# Haze. With no land below, a thin dense layer just under the arena: below it is nothing but haze.
# With the land (the arena's Land setting, Tools/import_land.py), the haze lies on the land's lake,
# LandDepth below the bases, and thins slowly upward: the lake is seen through it from above, the
# mountains stand out of it, and the land's far edge is lost in it.
rules = unreal.SenniteArena.get_default_object()
has_land = rules.get_editor_property("land").export_text() not in ("", "None")
land_depth = rules.get_editor_property("land_depth")
fog = spawn(unreal.ExponentialHeightFog, "haze", unreal.Vector(0, 0, -land_depth if has_land else -2000), folder="lights")
fog.component.set_editor_property("fog_density", 0.006 if has_land else 0.004)
if has_land:
    fog.component.set_editor_property("fog_height_falloff", 0.05)
unreal.log("ARENA haze: %s" % ("on the land's lake, %.0f cm below the bases" % land_depth if has_land else "just under the arena"))

ppv = spawn(unreal.PostProcessVolume, "exposure", folder="lights")
ppv.set_editor_property("unbound", True)
s = ppv.get_editor_property("settings")
for key, value in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                   ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.4),
                   ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False)):
    s.set_editor_property(key, value)
ppv.set_editor_property("settings", s)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
# below this a player has fallen out of the arena
settings = world.get_world_settings()
settings.set_editor_property("kill_z", -3000.0)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("map did not save")

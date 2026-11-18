"""Build the test range: a flat grey-box map to run the game's systems on.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/build_test_range.py -unattended -nullrhi

Phone-level lighting only: one shadowed sun, an unshadowed fill from a fixed daylight sky, no ray tracing, no Lumen.
The floor is one build-grid cell thick and the blocks sit on the 480 cm build grid, so building pieces
line up with them. Writes /Game/Maps/TestRange."""
import unreal

MAP = "/Game/Maps/TestRange"
CELL = 480.0                      # ASenniteCharacter::BuildGridCell
CUBE = "/Engine/BasicShapes/Cube"  # 100 cm on a side
GRID = "/Game/Maps/Materials/M_RangeGrid"

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def build_grid_material():
    """Grey checker in world space, one square per build-grid cell, so pieces and distances read at a glance."""
    if eal.does_asset_exist(GRID):
        eal.delete_asset(GRID)
    folder, name = GRID.rsplit("/", 1)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    pos = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, 0)
    nrm = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, 150)
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -550, 0)
    # squares are centred on the grid points; the axis a face looks along is left out, so a face lying on a cell edge does not flicker
    node.set_editor_property("code", """
float3 c = floor(P / %.1f + 0.5);
float3 n = abs(N);
float k = n.z > 0.7 ? c.x + c.y : (n.x > 0.7 ? c.y + c.z : c.x + c.z);
float odd = abs(fmod(k, 2.0));
return lerp(float3(0.20, 0.21, 0.22), float3(0.30, 0.31, 0.32), odd);
""" % CELL)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("description", "build-grid checker")
    pins = []
    for pin_name in ("P", "N"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    node.set_editor_property("inputs", pins)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -550, 250)
    rough.set_editor_property("r", 0.9)
    ok = (mel.connect_material_expressions(pos, "", node, "P") and mel.connect_material_expressions(nrm, "", node, "N")
          and mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
          and mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS))
    if not ok:
        raise RuntimeError("grid material did not wire up")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def build_storm_material():
    """The storm's wall: a see-through violet sheet, seen from both sides, that takes no light."""
    path = "/Game/Storm/M_StormWall"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_StormWall", "/Game/Storm", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    colour.set_editor_property("constant", unreal.LinearColor(0.28, 0.05, 0.55, 1.0))
    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
    opacity.set_editor_property("r", 0.45)
    if not (mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            and mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)):
        raise RuntimeError("storm material did not wire up")
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


grid = build_grid_material()
build_storm_material()
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cube = eal.load_asset(CUBE)
if cube is None:
    raise RuntimeError("engine cube did not load")


def spawn(cls, label, loc=unreal.Vector(0, 0, 0), rot=unreal.Rotator(0, 0, 0), folder=None):
    a = actors.spawn_actor_from_class(cls, loc, rot)
    a.set_actor_label(label)
    if folder:
        a.set_folder_path(folder)
    return a


def block(label, centre, size, rot=unreal.Rotator(0, 0, 0)):
    """A grey box of the given size in cm, centred at centre."""
    a = spawn(unreal.StaticMeshActor, label, centre, rot, "range")
    c = a.static_mesh_component
    c.set_static_mesh(cube)
    c.set_material(0, grid)
    a.set_actor_scale3d(unreal.Vector(size.x / 100.0, size.y / 100.0, size.z / 100.0))
    c.set_mobility(unreal.ComponentMobility.STATIC)
    return a


# the ground: 40 x 40 build cells, its top at z = 0
half = 20 * CELL
block("ground", unreal.Vector(0, 0, -50), unreal.Vector(2 * half, 2 * half, 100))

# a low wall around the edge so nothing walks off
for i, (x, y, sx, sy) in enumerate(((half, 0, 100, 2 * half), (-half, 0, 100, 2 * half), (0, half, 2 * half, 100), (0, -half, 2 * half, 100))):
    block("edge_%d" % i, unreal.Vector(x, y, 150), unreal.Vector(sx, sy, 300))

# things to jump on and run around, on the build grid ahead of the start
block("step_low", unreal.Vector(3 * CELL, -1 * CELL, 40), unreal.Vector(CELL, CELL, 80))
block("step_high", unreal.Vector(3 * CELL, 0, 100), unreal.Vector(CELL, CELL, 200))
block("tower", unreal.Vector(5 * CELL, 2 * CELL, 480), unreal.Vector(CELL, CELL, 960))
block("long_wall", unreal.Vector(4 * CELL, -3 * CELL, 240), unreal.Vector(4 * CELL, 40, 480))
# a ramp up to the high step: 480 long, rising 200
ramp_len = (CELL ** 2 + 200.0 ** 2) ** 0.5
block("ramp", unreal.Vector(2 * CELL, 0, 100 - 10), unreal.Vector(ramp_len, CELL, 20), unreal.Rotator(0, 22.62, 0))  # roll, pitch, yaw

# light: one shadowed sun and an unshadowed sky
sun = spawn(unreal.DirectionalLight, "sun", unreal.Vector(0, 0, 1000), unreal.Rotator(0, -42, 115), "lights")  # roll, pitch, yaw: from behind and to the left of the start
sc = sun.light_component
sc.set_mobility(unreal.ComponentMobility.MOVABLE)
for key, value in (("intensity", 6.0), ("atmosphere_sun_light", True), ("dynamic_shadow_distance_movable_light", 6000.0),
                   ("dynamic_shadow_cascades", 3), ("cast_shadows", True)):
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
                   ("real_time_capture", False), ("lower_hemisphere_is_black", False), ("cast_shadows", False), ("intensity", 1.8)):
    kc.set_editor_property(key, value)

spawn(unreal.SkyAtmosphere, "atmosphere", folder="lights")
fog = spawn(unreal.ExponentialHeightFog, "haze", unreal.Vector(0, 0, 0), folder="lights")
fog.component.set_editor_property("fog_density", 0.005)

ppv = spawn(unreal.PostProcessVolume, "exposure", folder="lights")
ppv.set_editor_property("unbound", True)
s = ppv.get_editor_property("settings")
for key, value in (("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
                   ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.8),
                   ("override_auto_exposure_apply_physical_camera_exposure", True), ("auto_exposure_apply_physical_camera_exposure", False)):
    s.set_editor_property(key, value)
ppv.set_editor_property("settings", s)

# weapons to pick up, to the right of the start, lying at knee height
spawn(unreal.SenniteRifle, "rifle_pickup", unreal.Vector(0, 250, 60), unreal.Rotator(0, 0, 0), "pickups")
spawn(unreal.SenniteShotgun, "shotgun_pickup", unreal.Vector(0, 450, 60), unreal.Rotator(0, 0, 0), "pickups")
spawn(unreal.SenniteRifle, "rifle_pickup_2", unreal.Vector(0, -2 * CELL - 250, 60), unreal.Rotator(0, 0, 0), "pickups")   # to the left of the second start

# healing items to pick up, behind the start
spawn(unreal.SenniteBandage, "bandage_pickup", unreal.Vector(-300, 0, 40), folder="pickups")
spawn(unreal.SennitePotion, "potion_pickup", unreal.Vector(-500, 0, 40), folder="pickups")

# the storm: its safe circle is centred ahead of the start, so the start is well inside it and its
# wall crosses the range 2,500 behind the start
storm = spawn(unreal.StormActor, "storm", unreal.Vector(2500, 0, 0), folder="range")
storm.set_editor_property("start_radius", 5000.0)

# where the player starts: standing on the ground, facing the blocks (+X)
spawn(unreal.PlayerStart, "start", unreal.Vector(0, 0, 110), unreal.Rotator(0, 0, 0), "range")

# a second place to start, two cells to the left of the first, for a second player
spawn(unreal.PlayerStart, "start_2", unreal.Vector(0, -2 * CELL, 110), unreal.Rotator(0, 0, 0), "range")

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError("map did not save")
unreal.log("TESTRANGE saved " + MAP)

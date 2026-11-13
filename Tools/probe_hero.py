"""Print what a hero pack gives the game: its animation Blueprint's parent and variables, its own
character Blueprint's capsule and mesh placement, and the skeleton's sockets.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/probe_hero.py -unattended -nullrhi
Output goes to Saved/probe_hero.txt."""
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Saved", "probe_hero.txt")
HERO = "/Game/ParagonDekker/Characters/Heroes/Dekker"
eal = unreal.EditorAssetLibrary
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log("PROBE " + str(msg))


def attempt(label, fn):
    try:
        log(label + ": " + str(fn()))
    except Exception as exc:  # a probe reports what it cannot read and keeps going
        log(label + " FAILED: " + repr(exc))


abp = eal.load_asset(HERO + "/Dekker_AnimBlueprint")
log("anim blueprint " + str(abp))
attempt("anim parent class", lambda: abp.get_editor_property("parent_class"))
attempt("anim target skeleton", lambda: abp.get_editor_property("target_skeleton"))
gen = abp.generated_class()
log("anim generated class " + str(gen))
cdo = unreal.get_default_object(gen)
for name in sorted(dir(cdo)):
    if name.startswith("_") or name in dir(unreal.AnimInstance):
        continue
    log("  anim member " + name)

pc = eal.load_asset(HERO + "/DekkerPlayerCharacter")
log("character blueprint " + str(pc))
if pc:
    pgen = pc.generated_class()
    pcdo = unreal.get_default_object(pgen)
    attempt("character parent", lambda: pc.get_editor_property("parent_class"))
    cap = pcdo.get_editor_property("capsule_component")
    attempt("capsule radius", lambda: cap.get_editor_property("capsule_radius"))
    attempt("capsule half height", lambda: cap.get_editor_property("capsule_half_height"))
    mesh = pcdo.get_editor_property("mesh")
    attempt("mesh asset", lambda: mesh.get_editor_property("skeletal_mesh_asset"))
    attempt("mesh relative location", lambda: mesh.get_editor_property("relative_location"))
    attempt("mesh relative rotation", lambda: mesh.get_editor_property("relative_rotation"))
    attempt("mesh anim class", lambda: mesh.get_editor_property("anim_class"))
    move = pcdo.get_editor_property("character_movement")
    for prop in ("max_walk_speed", "jump_z_velocity", "air_control", "max_acceleration", "braking_deceleration_walking",
                 "orient_rotation_to_movement", "use_controller_desired_rotation", "rotation_rate"):
        attempt("movement " + prop, lambda p=prop: move.get_editor_property(p))
    for prop in ("use_controller_rotation_yaw",):
        attempt("pawn " + prop, lambda p=prop: pcdo.get_editor_property(p))

sk = eal.load_asset(HERO + "/Meshes/Dekker")
log("mesh " + str(sk))
attempt("mesh bounds", lambda: sk.get_bounds())
attempt("mesh materials", lambda: [m.get_editor_property("material_interface").get_name() for m in sk.get_editor_property("materials")])
skel = sk.get_editor_property("skeleton")
attempt("skeleton", lambda: skel.get_path_name())
attempt("mesh sockets", lambda: [(s.get_editor_property("socket_name"), s.get_editor_property("bone_name")) for s in
                                 [sk.get_socket_by_index(i) for i in range(sk.num_sockets())]])
attempt("physics asset", lambda: sk.get_editor_property("physics_asset"))

for anim in ("Idle", "Jog_Fwd", "Sprint_Fwd", "Run_Fwd", "Jump_Start", "Jump_Apex", "Jump_Land"):
    a = eal.load_asset(HERO + "/Animations/" + anim)
    attempt("anim " + anim, lambda a=a: (a.get_class().get_name(), round(a.get_play_length(), 2), a.get_editor_property("enable_root_motion")))
for bs in ("JogDirection_BS", "JogStart_BS", "JogStop_BS"):
    b = eal.load_asset(HERO + "/Animations/Blendspaces/" + bs)
    attempt("blendspace " + bs, lambda b=b: b.get_class().get_name())

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as fh:
    fh.write("\n".join(lines) + "\n")

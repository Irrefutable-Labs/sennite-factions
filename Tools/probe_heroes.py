"""Print what each hero pack gives the game: its own character Blueprint's capsule and mesh placement,
its animation Blueprint, its mesh and skeleton, and its montages with the slot each plays in.
What a hero is in the game (Config/DefaultGame.ini, [/Script/Sennite.SenniteHeroes]) is read off this.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/probe_heroes.py" -unattended -nullrhi
Output goes to Saved/probe_heroes.txt."""
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Saved", "probe_heroes.txt")
eal = unreal.EditorAssetLibrary
lines = []

# (name, folder, character Blueprint, mesh) as each pack ships them
HEROES = [(h, "/Game/Paragon%s/Characters/Heroes/%s" % (h, h), "%sPlayerCharacter" % h, None)
          for h in ("Dekker", "Crunch", "Drongo", "Howitzer", "Rampage", "Sevarog", "Wraith")]
HEROES.append(("Trooper", "/Game/SciFITrooper_Man_03", "Blueprints/BP_Character_Master", "SkeletalMesh/SK_SciFITrooper_Man_03"))


def log(msg):
    lines.append(str(msg))
    unreal.log("PROBE " + str(msg))


def attempt(label, fn):
    try:
        log(label + ": " + str(fn()))
    except Exception as exc:  # a probe reports what it cannot read and keeps going
        log(label + " FAILED: " + repr(exc))


for name, folder, character, mesh_path in HEROES:
    log("==== " + name)
    bp = eal.load_asset(folder + "/" + character)
    if bp:
        cdo = unreal.get_default_object(bp.generated_class())
        cap = cdo.get_editor_property("capsule_component")
        attempt("capsule radius", lambda: cap.get_editor_property("capsule_radius"))
        attempt("capsule half height", lambda: cap.get_editor_property("capsule_half_height"))
        mesh = cdo.get_editor_property("mesh")
        attempt("mesh asset", lambda: mesh.get_editor_property("skeletal_mesh_asset").get_path_name())
        attempt("mesh relative location", lambda: mesh.get_editor_property("relative_location"))
        attempt("mesh relative rotation", lambda: mesh.get_editor_property("relative_rotation"))
        attempt("mesh relative scale", lambda: mesh.get_editor_property("relative_scale3d"))
        attempt("mesh anim class", lambda: mesh.get_editor_property("anim_class").get_path_name())
        move = cdo.get_editor_property("character_movement")
        attempt("max walk speed", lambda: move.get_editor_property("max_walk_speed"))
    else:
        log("no character blueprint " + folder + "/" + character)
    sk = eal.load_asset(folder + "/" + (mesh_path or "Meshes/" + name))
    if sk:
        log("mesh " + sk.get_path_name())
        attempt("mesh bounds", lambda: sk.get_bounds())
        attempt("skeleton", lambda: sk.get_editor_property("skeleton").get_path_name())
        attempt("mesh sockets", lambda: [(str(s.get_editor_property("socket_name")), str(s.get_editor_property("bone_name"))) for s in
                                         [sk.get_socket_by_index(i) for i in range(sk.num_sockets())]])
    for path in eal.list_assets(folder, recursive=True, include_folder=False):
        if "/Skins/" in path:
            continue
        data = eal.find_asset_data(path)
        cls = str(data.asset_class_path.asset_name)
        if cls == "AnimMontage":
            m = eal.load_asset(path)
            attempt("montage " + path.split(".")[0], lambda m=m: (round(m.get_play_length(), 2),
                    [str(t.get_editor_property("slot_name")) for t in m.get_editor_property("slot_anim_tracks")]))
        elif cls == "AnimBlueprint":
            log("anim blueprint " + path.split(".")[0])

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as fh:
    fh.write("\n".join(lines) + "\n")

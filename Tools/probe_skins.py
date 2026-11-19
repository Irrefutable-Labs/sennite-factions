"""Print every skin the hero packs ship: each skeletal mesh under a hero's Skins folder, whether it has
the hero's own skeleton (a skin is worn only on the hero whose skeleton it has), its materials, and
its size on disk. What the shop sells as a skin (Server/world/shop.json) and which mesh each one is
(Config/DefaultGame.ini, [/Script/Sennite.SenniteBaseEditor]) are read off this.

  UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script="$PWD/Tools/probe_skins.py" -unattended -nullrhi
Output goes to Saved/probe_skins.txt."""
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Saved", "probe_skins.txt")
registry = unreal.AssetRegistryHelpers.get_asset_registry()
lines = []

# each hero's own mesh, as Config/DefaultGame.ini names it
HEROES = {
    "Dekker": "/Game/ParagonDekker/Characters/Heroes/Dekker/Meshes/Dekker",
    "Wraith": "/Game/ParagonWraith/Characters/Heroes/Wraith/Meshes/Wraith",
    "Crunch": "/Game/ParagonCrunch/Characters/Heroes/Crunch/Meshes/Crunch",
    "Drongo": "/Game/ParagonDrongo/Characters/Heroes/Drongo/Meshes/Drongo_GDC",
    "Howitzer": "/Game/ParagonHowitzer/Characters/Heroes/Howitzer/Meshes/Howitzer_GDC",
    "Rampage": "/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rampage",
    "Sevarog": "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Meshes/Sevarog",
}


def log(msg):
    lines.append(str(msg))
    unreal.log("PROBE " + str(msg))


for hero, own in HEROES.items():
    log("==== " + hero)
    mesh = unreal.EditorAssetLibrary.load_asset(own)
    skeleton = mesh.get_editor_property("skeleton") if mesh else None
    log("own mesh %s skeleton %s" % (own, skeleton.get_path_name() if skeleton else None))
    folder = "/Game/Paragon%s/Characters/Heroes/%s/Skins" % (hero, hero)
    found = registry.get_assets_by_path(folder, recursive=True)
    for data in sorted(found, key=lambda d: str(d.package_name)):
        if str(data.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        skin = data.get_asset()
        theirs = skin.get_editor_property("skeleton")
        materials = [str(m.get_editor_property("material_interface").get_name()) if m.get_editor_property("material_interface") else "None" for m in skin.get_editor_property("materials")]
        disk = os.path.join(ROOT, "Content", str(data.package_name)[len("/Game/"):] + ".uasset")
        size = os.path.getsize(disk) // 1024 if os.path.exists(disk) else -1
        log("skin %s.%s  same_skeleton=%s  %d KB  materials=%s" % (data.package_name, data.asset_name, theirs == skeleton, size, ",".join(materials)))

with open(OUT, "w") as out:
    out.write("\n".join(lines) + "\n")

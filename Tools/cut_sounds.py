#!/usr/bin/env python3
"""Cut the game's sounds out of the owner's two sound packs, ready for Tools/import_sounds.py.

  python3 Tools/cut_sounds.py [folder with the packs' files ...]

The packs are "Sonniss.com-GDC2024-GameAudioBundle1of9" (royalty-free for games, no attribution: its
"License - GDC Game Audio.pdf") and "Essentials_Series_NOX_SOUND" (CC0: its README). They are looked
for in ~/Downloads, or in the folders named; a file is found by its name, wherever it lies under them.

Every sound is named for what it is in the packs, so which file is which sound was chosen from the
names and each file's length; nobody listened to them when this was written. To change a sound, change
its file below, run this and then the import. Needs ffmpeg.

Each row: the asset's name, the pack's file, and how it is cut:
  at     where the cut starts: "start" (the file's first sound, silence before it dropped), "peak"
         (around the loudest moment: a whoosh that passes), or seconds from the file's beginning
  keep   how long it is, in seconds (None: all of it)
  loop   an ambience that goes round: kept whole, in stereo, not faded
A sound heard from a place in the world is mono (the game places it); every one-shot is brought to the
same peak level and faded out at its end.

Writes the cut files and sounds.json into Saved/SoundSource/cut."""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "Saved", "SoundSource", "cut")
ROOTS = sys.argv[1:] or [os.path.expanduser("~/Downloads/Sonniss.com-GDC2024-GameAudioBundle1of9"),
                         os.path.expanduser("~/Downloads/Essentials_Series_NOX_SOUND")]

# (asset, file, at, keep)
ONE_SHOTS = [
    # the menus
    ("SW_UiClick", "UIClick_UI Click 33_CB Sounddesign_ACTIVATION2.wav", "start", None),
    ("SW_UiSelect", "Bluezone_BC0303_futuristic_user_interface_high_tech_beep_038.wav", "start", None),
    ("SW_UiPage", "Bluezone_BC0303_futuristic_user_interface_transition_006.wav", "start", 1.0),
    ("SW_UiOpen", "UIMvmt_UI Zooms 103_CB Soudndesign_ACTIVATION2.wav", "start", None),
    ("SW_UiDenied", "UI_Noisy_Impact_09.wav", "start", 0.6),
    ("SW_UiBought", "UIMisc_Feedback 36 up_CB Sounddesign_ACTIVATION2.wav", "start", None),
    ("SW_UiCoins", "UIData_Counter mid 54_CB Sounddesign_ACTIVATION2.wav", "start", 1.2),
    ("SW_UiTick", "Bluezone_BC0301_tiny_gears_small_mechanism_click_003.wav", "start", None),
    ("SW_WalletConnected", "CREAMisc_Creature Power On 14_DDUMAIS_MCSFX.wav", "start", 1.5),
    # weapons
    # The guns are recordings of guns. The rifle and the sniper are one real rifle's shot, made full for a
    # game: the rifle its crack alone (the pack's other rifle recording is from 30 m off, thin and quiet
    # beside the rest), the sniper the whole of it with its long tail. The shotgun is a real shotgun's. The
    # energy rifle and a hero's own weapon are no real guns, so theirs are made-up gun shots, not a UI
    # pack's zaps; the own weapon's starts at its bang, 0.19 s in, after the click of its mechanism. The
    # owner, 2026-11-18, of a build whose guns had the UI pack's zaps: "Voices you pick for the guns are
    # fucking awful."
    ("SW_RifleShot", "GUNRif_SVD Dragunov 7.62×54R DESIGNED Single Shot Core Long_DRCA_DRAG_Stereo_05.wav", "start", 0.55),
    ("SW_EnergyShot", "Bluezone_BC0295_sci_fi_weapon_gun_shot_008.wav", "start", 0.45),
    ("SW_ShotgunShot", "GUNShotg_Saiga 12 Cal.12 DESIGNED Single Shot Core Long_DRCA_SG12_Stereo.wav", "start", 1.6),
    ("SW_SniperShot", "GUNRif_SVD Dragunov 7.62×54R DESIGNED Single Shot Core Long_DRCA_DRAG_Stereo_05.wav", "start", 2.2),
    ("SW_OwnBolt", "Bluezone_BC0296_steampunk_weapon_gun_shot_026_02.wav", 0.19, 0.7),
    ("SW_Swing", "WHOOSH PASS SF LOW.wav", "peak", 0.55),
    ("SW_ReloadRifle", "GUNMech_SVD Dragunov 7.62×54R SOURCE Magazine Insert Slow_DRCA_DRAG_CO-100K.wav", "start", 1.3),
    ("SW_ReloadShotgun", "GUNMech_Saiga 12 Cal.12 SOURCE Chamber Eject Bullet_DRCA_SG12_MKH416.wav", "start", 1.3),
    ("SW_WeaponSwitch", "Bluezone_BC0302_industrial_lever_switch_small_003.wav", "start", None),
    # what a shot or a blow lands on
    ("SW_BoltHit", "Bluezone_BC0297_stone_impact_015.wav", "start", 0.6),
    ("SW_BodyHit", "Noise_Punch_006.wav", "start", 0.5),
    ("SW_HitMarker", "Bluezone_BC0304_retrofuturistic_computer_button_013.wav", "start", None),
    ("SW_PieceHitWood", "Bluezone_BC0305_steampunk_machine_mechanical_texture_038.wav", "start", None),
    ("SW_PieceHitStone", "Bluezone_BC0297_stone_impact_hammer_015.wav", "start", None),
    ("SW_PieceHitSteel", "Bluezone_BC0297_stone_impact_steel_bar_01_010.wav", "start", None),
    ("SW_PieceBreakWood", "WOODCrsh_Designed Wood Crash And Debris 13_DDUMAIS_NONE.wav", "start", None),
    ("SW_PieceBreakStone", "Bluezone_BC0292_alien_tripod_debris_rock_collapse_earthquake_rumble_large_005.wav", "start", 2.0),
    ("SW_PieceBreakSteel", "DESTRCrsh_Designed Car Explosion With Metal Breaking And Glass Shattering  06_DDUMAIS_NONE.wav", "start", 2.0),
    ("SW_PiecePlace", "Bluezone_BC0305_steampunk_machine_mechanical_texture_heavy_impact_011.wav", "start", None),
    ("SW_PieceEdit", "Bluezone_BC0296_steampunk_weapon_mechanism_texture_007.wav", "start", None),
    ("SW_Harvest", "Bluezone_BC0297_stone_impact_041.wav", "start", 0.6),
    # what is picked up and used
    ("SW_PickupItem", "Backpack_Small_Polyester_PickUp_Stereo.wav", "start", None),
    ("SW_PickupWeapon", "Bluezone_BC0302_industrial_lever_switch_014.wav", "start", 0.7),
    ("SW_PickupRounds", "Household_Closet_Key_Insertion_Stereo.wav", "start", None),
    ("SW_Bandage", "FOLYMisc_BluePPEMask_Elastic09_InMotionAudio_MaskFoley.wav", "start", None),
    ("SW_Potion", "OBJCont_Flask,glass,figuredbottle,realcork,liquid,remove,squeak,slosh,alt4_EWKR.wav", "start", None),
    # the match
    ("SW_Fell", "Bluezone_BC0294_modern_cinematic_impact_percussion_009.wav", "start", 1.6),
    ("SW_MatchStart", "Mothership_Entering_hostile_territory_All_crew_members_be_on_high_alert.wav", "start", None),
    ("SW_MatchWon", "Short_PowerUp_005.wav", "start", 2.5),
    ("SW_MatchLost", "Bluezone_BC0294_modern_cinematic_impact_boom_003.wav", "start", 3.0),
    ("SW_StormAlarm", "Bluezone_BC0304_retrofuturistic_computer_alarm_005.wav", "start", 3.0),
]

# Footsteps and landings by ground, and a hero's voice: several takes of each, so no two in a row are the same.
TAKES = []
for ground, walk, run, jump, land in (
        ("Grass", "Footsteps_Walk_Grass_Mono_%02d.wav", "Footsteps_Grass_Run_%02d.wav", "Footsteps_Grass_Jump_Start_%02d.wav", "Footsteps_Grass_Jump_Land_%02d.wav"),
        ("Wood", "Footsteps_Wood_Walk_%02d.wav", "Footsteps_Wood_Run_%02d.wav", "Footsteps_Wood_Jump_Start_%02d.wav", "Footsteps_Wood_Jump_Land_%02d.wav"),
        ("Rock", "Footsteps_Rock_Walk_%02d.wav", "Footsteps_Rock_Run_%02d.wav", "Footsteps_Rock_Jump_Start_%02d.wav", "Footsteps_Rock_Jump_Land_%02d.wav"),
        ("Metal", "Footsteps_MetalV1_Walk_%02d.wav", "Footsteps_MetalV1_Run_%02d.wav", "Footsteps_MetalV1_Jump_Start_%02d.wav", "Footsteps_MetalV1_Jump_Land_%02d.wav")):
    TAKES += [("SW_Step%sWalk" % ground, walk, 6), ("SW_Step%sRun" % ground, run, 6),
              ("SW_Jump%s" % ground, jump, 2), ("SW_Land%s" % ground, land, 2)]
for who, name in (("F", "Female"), ("M", "Male")):
    TAKES += [("SW_Voice%sHit" % who, "Voice_%s_V1_Hit_Short_Mono_%%02d.wav" % name, 4),
              ("SW_Voice%sJump" % who, "Voice_%s_V1_Jump_Mono_%%02d.wav" % name, 3),
              ("SW_Voice%sLand" % who, "Voice_%s_V1_Land_Mono_%%02d.wav" % name, 3),
              ("SW_Voice%sPain" % who, "Voice_%s_V1_Pain_Mono_%%02d.wav" % name, 3)]

# (asset, file): ambiences that go round
LOOPS = [
    ("SW_AmbienceMenu", "Ambiance_Forest_Birds_Loop_Stereo.wav"),
    ("SW_AmbienceField", "Ambiance_Wind_Forest_Loop_Stereo.wav"),
    ("SW_AmbienceStorm", "Ambiance_Rain_Strong_Loop_Stereo.wav"),
]


def find(name):
    for root in ROOTS:
        for folder, _, files in os.walk(root):
            if name in files:
                return os.path.join(folder, name)
    raise SystemExit("%s is not under %s" % (name, ", ".join(ROOTS)))


def ffmpeg(*args):
    done = subprocess.run(["ffmpeg", "-hide_banner", "-nostdin", "-y"] + list(args), capture_output=True, text=True)
    if done.returncode != 0:
        raise SystemExit("ffmpeg failed: %s\n%s" % (" ".join(args), done.stderr[-600:]))
    return done.stderr


def length(path):
    said = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path],
                          capture_output=True, text=True, check=True).stdout
    return float(said.strip())


def loudest(path):
    """Seconds into the file at which it is loudest (in 50 ms windows)."""
    raw = subprocess.run(["ffmpeg", "-hide_banner", "-nostdin", "-i", path, "-ac", "1", "-ar", "8000", "-f", "s16le", "-"],
                         capture_output=True, check=True).stdout
    best, at = -1, 0
    for start in range(0, len(raw) - 800, 800):
        window = raw[start:start + 800]
        level = sum(abs(int.from_bytes(window[i:i + 2], "little", signed=True)) for i in range(0, 800, 2))
        if level > best:
            best, at = level, start
    return at / 2.0 / 8000.0


def peak_db(path, filters):
    said = ffmpeg("-i", path, "-af", filters + ",volumedetect", "-f", "null", "-")
    found = re.search(r"max_volume: (-?[0-9.]+) dB", said)
    return float(found.group(1)) if found else 0.0


def cut(asset, source, at, keep):
    path = find(source)
    chain = []
    if at == "start":
        chain.append("silenceremove=start_periods=1:start_threshold=-40dB:start_silence=0.005")
    elif at == "peak":
        chain.append("atrim=start=%.3f" % max(0.0, loudest(path) - keep * 0.45))
        chain.append("asetpts=PTS-STARTPTS")
        chain.append("afade=t=in:d=0.04")
    else:
        chain.append("atrim=start=%.3f" % at)
        chain.append("asetpts=PTS-STARTPTS")
    if keep is not None:
        chain.append("atrim=end=%.3f" % keep)
    shape = ",".join(chain)
    # how long it comes out, to fade its end
    probe = os.path.join(OUT, "_probe.wav")
    ffmpeg("-i", path, "-af", shape, "-ac", "1", "-ar", "44100", probe)
    long = length(probe)
    os.remove(probe)
    fade = min(0.12, long * 0.3)
    shape += ",afade=t=out:st=%.3f:d=%.3f" % (max(0.0, long - fade), fade)
    gain = -3.0 - peak_db(path, shape)
    out = os.path.join(OUT, asset + ".wav")
    ffmpeg("-i", path, "-af", "%s,volume=%.2fdB" % (shape, gain), "-ac", "1", "-ar", "44100", "-c:a", "pcm_s16le", out)
    return {"asset": asset, "file": asset + ".wav", "from": source, "seconds": round(length(out), 2), "loop": False}


os.makedirs(OUT, exist_ok=True)
made = []
for asset, source, at, keep in ONE_SHOTS:
    made.append(cut(asset, source, at, keep))
for asset, pattern, count in TAKES:
    for take in range(1, count + 1):
        made.append(cut("%s%d" % (asset, take), pattern % take, 0.0, None))  # the packs' takes are already cut close
for asset, source in LOOPS:
    path = find(source)
    out = os.path.join(OUT, asset + ".wav")
    ffmpeg("-i", path, "-ar", "44100", "-c:a", "pcm_s16le", out)
    made.append({"asset": asset, "file": asset + ".wav", "from": source, "seconds": round(length(out), 2), "loop": True})
with open(os.path.join(OUT, "sounds.json"), "w") as listing:
    json.dump(made, listing, indent=1)
for each in made:
    print("%-22s %6.2f s  %s" % (each["asset"], each["seconds"], each["from"]))
print("%d sounds cut into %s" % (len(made), OUT))

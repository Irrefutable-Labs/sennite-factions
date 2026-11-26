# Makes the build preview's material, /Game/Building/M_PiecePreview, alone (Tools/import_building.py makes it with
# the rest of the building materials, the same way). Run in the editor:
#   UnrealEditor-Cmd Sennite.uproject -run=pythonscript -script=Tools/make_piece_preview.py -unattended -nullrhi
#
# The preview is an outline: each tile's edges in a bright blue, its face all but clear, so a piece about to be built
# shows its shape without hiding where the player is going. The owner, 2026-11-25, of a ramp's preview: "When you go
# to build the ramp, you can't even freaking see it", and asked what was wrong: "Blocks the view".
import unreal

DEST = "/Game/Building"
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
P = unreal.MaterialProperty


def make_preview(material):
    """Wire the preview's graph into `material`: unlit, see-through, its tiles' edges bright and their faces nearly clear."""
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # a tile is the engine's cube: each face has the whole of UV 0..1, so how near a point is to a face's edge is in its UV
    uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -900, 100)
    edge = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -650, 100)
    edge.set_editor_property("code", "float2 d = min(UV, 1.0 - UV); float e = min(d.x, d.y); return 1.0 - saturate((e - 0.03) / 0.025);")
    edge.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    edge.set_editor_property("description", "Edge")
    named = unreal.CustomInput()
    named.set_editor_property("input_name", "UV")
    edge.set_editor_property("inputs", [named])
    if not mel.connect_material_expressions(uv, "", edge, "UV"):
        raise RuntimeError("preview: the edge did not take the UV")
    face = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -650, -150)
    face.set_editor_property("constant", unreal.LinearColor(0.10, 0.45, 1.0, 1.0))
    line = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -650, -300)
    line.set_editor_property("constant", unreal.LinearColor(0.55, 0.85, 1.0, 1.0))
    colour = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -350, -200)
    clear = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -650, 300)
    clear.set_editor_property("r", 0.06)
    solid = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -650, 400)
    solid.set_editor_property("r", 0.9)
    opacity = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -350, 300)
    wired = (
        mel.connect_material_expressions(face, "", colour, "A")
        and mel.connect_material_expressions(line, "", colour, "B")
        and mel.connect_material_expressions(edge, "", colour, "Alpha")
        and mel.connect_material_expressions(clear, "", opacity, "A")
        and mel.connect_material_expressions(solid, "", opacity, "B")
        and mel.connect_material_expressions(edge, "", opacity, "Alpha")
        and mel.connect_material_property(colour, "", P.MP_EMISSIVE_COLOR)
        and mel.connect_material_property(opacity, "", P.MP_OPACITY)
    )
    if not wired:
        raise RuntimeError("preview material did not wire up")
    # the see-through tiles of a piece being edited are instances too
    material.set_editor_property("used_with_instanced_static_meshes", True)
    mel.recompile_material(material)


if __name__ == "__main__":
    path = DEST + "/M_PiecePreview"
    preview = unreal.load_asset(path)
    if preview is None:
        raise RuntimeError(path + " is not there: make the building materials with Tools/import_building.py")
    mel.delete_all_material_expressions(preview)
    make_preview(preview)
    if not preview.get_editor_property("used_with_instanced_static_meshes"):
        raise RuntimeError("M_PiecePreview: instanced use did not take")
    eal.save_loaded_asset(preview)
    unreal.log("[make_piece_preview] done")

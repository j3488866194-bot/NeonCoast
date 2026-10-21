"""Editor-only asset authoring for Neon Coast visuals.

Creates (or rebuilds in place) /Game/Materials/M_Coast, the single opaque master
material used by ANCCityVisual. Parameters:
    BaseColor (vector), Roughness, Metallic, EmissiveStrength (scalars).
Emissive = BaseColor * EmissiveStrength.

Run inside the Unreal Editor (Python Editor Script Plugin):
    py "<Project>/Content/Python/build_visual_assets.py"
Safe to run repeatedly.
"""

import unreal

MATERIAL_DIR = "/Game/Materials"
MATERIAL_NAME = "M_Coast"
MATERIAL_PATH = MATERIAL_DIR + "/" + MATERIAL_NAME


def _load_or_create_material():
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(MATERIAL_PATH):
        material = eal.load_asset(MATERIAL_PATH)
        if isinstance(material, unreal.Material):
            unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
            return material
        raise RuntimeError(MATERIAL_PATH + " exists but is not a Material")

    if not eal.does_directory_exist(MATERIAL_DIR):
        eal.make_directory(MATERIAL_DIR)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(MATERIAL_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Failed to create " + MATERIAL_PATH)
    return material


def _scalar(material, name, default, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def build():
    mel = unreal.MaterialEditingLibrary
    material = _load_or_create_material()

    # Runtime HISM batches need this usage flag in cooked builds.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("two_sided", False)

    base = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, -200)
    base.set_editor_property("parameter_name", "BaseColor")
    base.set_editor_property("default_value", unreal.LinearColor(0.8, 0.74, 0.64, 1.0))

    roughness = _scalar(material, "Roughness", 0.6, -700, 0)
    metallic = _scalar(material, "Metallic", 0.0, -700, 120)
    emissive_strength = _scalar(material, "EmissiveStrength", 0.0, -700, 260)

    emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -380, 220)
    mel.connect_material_expressions(base, "", emissive, "A")
    mel.connect_material_expressions(emissive_strength, "", emissive, "B")

    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.layout_material_expressions(material)
    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_PATH, only_if_is_dirty=False)
    unreal.log("Neon Coast: built " + MATERIAL_PATH)
    return material


if __name__ == "__main__":
    build()

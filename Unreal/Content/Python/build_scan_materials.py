"""Editor-only materials for the locally licensed Renderpeople scans (Eric, Carla).

Run in UnrealEditor-Cmd with -run=pythonscript -script=<this file> after
build_realistic_characters.py has imported the texture maps. This script does
not import FBX, touch meshes, assign slots or animate; slot assignment belongs
to the character importer / C++. Renderpeople assets are NOT CC0: local game
use only, never redistribute the raw maps.

Inputs (either location is accepted; the importer currently uses Textures/):
    /Game/RealisticCharacters/<Person>/[Textures/]T_<Person>_Diffuse|Normal|Gloss|Mask01|Mask02
Outputs:
    /Game/RealisticCharacters/Materials/M_Eric, M_Carla

Each _ue4.fbx has one whole-body slot over a shared skin/cloth atlas, so the
material is one conservative DefaultLit opaque response:
    BaseColor  diffuse atlas, untinted (no global recolour)
    Normal     atlas normal; G multiplied by NormalGreenSign (default +1, no flip)
    Roughness  clamp(1 - gloss, RoughnessMin .42, RoughnessMax .88)
    Specular   .35, Metallic 0, no emissive, no subsurface
Mask01/Mask02 were inspected for Eric against the diffuse atlas: they are garment
selection masks (dark suit cloth; white shirt), not skin masks, so no skin-only
subsurface is driven from them. They only get budget-safe texture settings.

Texture budget: every map is capped at 2048 (8K sources) and streamed; the hero
shares these until a dedicated hero budget is agreed.

Existing materials are validated and reused, never cleared.
"""

from pathlib import Path
import json
import traceback
import unreal

PROJECT = Path(__file__).resolve().parents[2]
REPORT_FILE = PROJECT / "Saved" / "ScanMaterialReport.json"
ASSET_ROOT = "/Game/RealisticCharacters"
MATERIAL_DIR = ASSET_ROOT + "/Materials"
PEOPLE = ("Eric", "Carla")
REQUIRED_MAPS = ("Diffuse", "Normal", "Gloss")
OPTIONAL_MAPS = ("Mask01", "Mask02")
MAX_TEXTURE_SIZE = 2048
GRAPH_TAG = "NeonCoast.ScanMaterialGraph"
GRAPH_VERSION = "rp-scan-defaultlit-v1"
SENTINEL = "NEON_COAST_SCAN_MATERIALS_READY"

# Not verified against a Renderpeople statement or a lit render: the _ue4 package
# targets UE (DirectX tangent convention), so the default is no flip. Validate
# under grazing light on cloth folds and buttons; set -1 if relief reads inverted.
NORMAL_GREEN_SIGN = 1.0
SCALARS = {"Specular": 0.35, "RoughnessMin": 0.42, "RoughnessMax": 0.88, "NormalGreenSign": NORMAL_GREEN_SIGN}
TEXTURE_PARAMETERS = {"DiffuseTex": "Diffuse", "NormalTex": "Normal", "GlossTex": "Gloss"}
MASK_NOTES = {
    "Eric": {"Mask01": "dark suit cloth: trousers and vest panels",
             "Mask02": "white shirt panels (body, sleeves, collar, cuffs)",
             "method": "visual comparison of the 8K mask against the diffuse atlas"},
    "Carla": {"status": "not inspected; unused by the graph"},
}

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def _path(asset):
    return asset.get_path_name().split(".", 1)[0]


def _load(path, cls):
    if not EAL.does_asset_exist(path):
        return None
    asset = EAL.load_asset(path)
    if not isinstance(asset, cls):
        raise RuntimeError(f"{path} exists but is not a {cls.__name__}")
    return asset


def _save(asset):
    if not EAL.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Failed to save " + asset.get_path_name())


def _write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def _sampler_for(texture):
    """Pick the sampler type matching the texture's compression, otherwise the material will not compile."""
    st = unreal.MaterialSamplerType
    tcs = unreal.TextureCompressionSettings
    comp = texture.get_editor_property("compression_settings")
    srgb = texture.get_editor_property("srgb")
    if comp == tcs.TC_GRAYSCALE:
        return st.SAMPLERTYPE_GRAYSCALE if srgb else st.SAMPLERTYPE_LINEAR_GRAYSCALE
    if comp == tcs.TC_ALPHA:
        return st.SAMPLERTYPE_ALPHA
    if comp == tcs.TC_MASKS:
        return st.SAMPLERTYPE_MASKS
    if comp == tcs.TC_NORMALMAP:
        return st.SAMPLERTYPE_NORMAL
    return st.SAMPLERTYPE_COLOR if srgb else st.SAMPLERTYPE_LINEAR_COLOR


# ---------------------------------------------------------------- textures

def _find_texture(person, label):
    for path in (f"{ASSET_ROOT}/{person}/T_{person}_{label}", f"{ASSET_ROOT}/{person}/Textures/T_{person}_{label}"):
        texture = _load(path, unreal.Texture2D)
        if texture is not None:
            return texture
    return None


def _texture_settings(label):
    tcs, group = unreal.TextureCompressionSettings, unreal.TextureGroup
    common = {"max_texture_size": MAX_TEXTURE_SIZE, "never_stream": False,
              "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP}
    if label == "Diffuse":
        return dict(common, srgb=True, compression_settings=tcs.TC_DEFAULT, lod_group=group.TEXTUREGROUP_CHARACTER)
    if label == "Normal":
        # Any green flip lives in the material (NormalGreenSign) so validation never re-imports.
        return dict(common, srgb=False, compression_settings=tcs.TC_NORMALMAP,
                    lod_group=group.TEXTUREGROUP_CHARACTER_NORMAL_MAP, flip_green_channel=False)
    return dict(common, srgb=False, compression_settings=tcs.TC_MASKS,
                lod_group=group.TEXTUREGROUP_CHARACTER_SPECULAR)


def _apply_settings(texture, settings):
    changed = []
    for key, value in settings.items():
        if texture.get_editor_property(key) != value:
            texture.set_editor_property(key, value)
            changed.append(key)
    if changed:
        _save(texture)
    return changed


def _textures(person):
    result, missing = {}, []
    for label in REQUIRED_MAPS + OPTIONAL_MAPS:
        texture = _find_texture(person, label)
        if texture is None:
            if label in REQUIRED_MAPS:
                missing.append(label)
            continue
        changed = _apply_settings(texture, _texture_settings(label))
        result[label] = {"asset": texture, "path": _path(texture), "changed_settings": changed}
    if missing:
        raise RuntimeError(f"{person}: imported maps missing {missing}; run build_realistic_characters.py first")
    return result


# ---------------------------------------------------------------- graph

def _node(material, cls, x, y, **props):
    node = MEL.create_material_expression(material, cls, x, y)
    if node is None:
        raise RuntimeError("Could not create " + cls.__name__)
    for key, value in props.items():
        node.set_editor_property(key, value)
    return node


def _link(src, src_out, dst, inputs):
    for name in inputs:
        if MEL.connect_material_expressions(src, src_out, dst, name):
            return
    raise RuntimeError("Failed to connect %s.%s -> %s%s" % (src.get_name(), src_out, dst.get_name(), list(inputs)))


def _build_graph(material, textures):
    def sample(name, label, y):
        texture = textures[label]["asset"]
        return _node(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, y, parameter_name=name,
                     texture=texture, sampler_type=_sampler_for(texture), group="Textures")

    def scalar(name, y):
        return _node(material, unreal.MaterialExpressionScalarParameter, -900, y, parameter_name=name,
                     default_value=float(SCALARS[name]), group="Response")

    diffuse = sample("DiffuseTex", "Diffuse", -500)
    normal = sample("NormalTex", "Normal", -150)
    gloss = sample("GlossTex", "Gloss", 250)
    green_sign = scalar("NormalGreenSign", 50)
    rough_min = scalar("RoughnessMin", 450)
    rough_max = scalar("RoughnessMax", 550)
    specular = scalar("Specular", 680)

    # Normal = (R, G * NormalGreenSign, B).
    green = _node(material, unreal.MaterialExpressionMultiply, -600, -100)
    _link(normal, "G", green, ("A",))
    _link(green_sign, "", green, ("B",))
    red_green = _node(material, unreal.MaterialExpressionAppendVector, -450, -150)
    _link(normal, "R", red_green, ("A",))
    _link(green, "", red_green, ("B",))
    rebuilt = _node(material, unreal.MaterialExpressionAppendVector, -300, -150)
    _link(red_green, "", rebuilt, ("A",))
    _link(normal, "B", rebuilt, ("B",))

    rough = _node(material, unreal.MaterialExpressionOneMinus, -600, 300)
    _link(gloss, "R", rough, ("", "Input"))
    clamped = _node(material, unreal.MaterialExpressionClamp, -400, 350)
    _link(rough, "", clamped, ("Input", ""))
    _link(rough_min, "", clamped, ("Min",))
    _link(rough_max, "", clamped, ("Max",))
    metallic = _node(material, unreal.MaterialExpressionConstant, -400, 800, r=0.0)

    for node, output, prop in ((diffuse, "RGB", unreal.MaterialProperty.MP_BASE_COLOR),
                               (rebuilt, "", unreal.MaterialProperty.MP_NORMAL),
                               (clamped, "", unreal.MaterialProperty.MP_ROUGHNESS),
                               (specular, "", unreal.MaterialProperty.MP_SPECULAR),
                               (metallic, "", unreal.MaterialProperty.MP_METALLIC)):
        if not MEL.connect_material_property(node, output, prop):
            raise RuntimeError(f"Cannot connect {prop} on {_path(material)}")


def _validate(material, textures):
    problems = []
    expected = {"blend_mode": unreal.BlendMode.BLEND_OPAQUE, "two_sided": False,
                "shading_model": unreal.MaterialShadingModel.MSM_DEFAULT_LIT, "used_with_skeletal_mesh": True}
    problems += [key for key, value in expected.items() if material.get_editor_property(key) != value]
    texture_names = set(str(n) for n in MEL.get_texture_parameter_names(material))
    scalar_names = set(str(n) for n in MEL.get_scalar_parameter_names(material))
    missing = sorted((set(TEXTURE_PARAMETERS) - texture_names) | (set(SCALARS) - scalar_names))
    if missing:
        problems.append(f"missing parameters {missing}")
    for name, label in TEXTURE_PARAMETERS.items():
        if name in texture_names:
            current = MEL.get_material_default_texture_parameter_value(material, name)
            if current != textures[label]["asset"]:
                problems.append(f"{name} does not reference {textures[label]['path']}")
    for prop in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_NORMAL,
                 unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_SPECULAR):
        if MEL.get_material_property_input_node(material, prop) is None:
            problems.append(f"{prop} unconnected")
    for prop in (unreal.MaterialProperty.MP_EMISSIVE_COLOR, unreal.MaterialProperty.MP_OPACITY,
                 unreal.MaterialProperty.MP_SUBSURFACE_COLOR):
        if MEL.get_material_property_input_node(material, prop) is not None:
            problems.append(f"unexpected {prop} connection")
    if problems:
        raise RuntimeError(_path(material) + ": " + "; ".join(problems) + ". Existing graph preserved; rebuild it "
                           "explicitly in a controlled editor session after review.")


def _material(person, textures):
    name = "M_" + person
    path = MATERIAL_DIR + "/" + name
    material = _load(path, unreal.Material)
    if material is not None:
        recorded = EAL.get_metadata_tag(material, GRAPH_TAG)
        if recorded and recorded != GRAPH_VERSION:
            raise RuntimeError(f"{path}: graph version {recorded} differs from {GRAPH_VERSION}. Existing expressions "
                               "were preserved; rebuild explicitly in a controlled editor session.")
        _validate(material, textures)
        if not recorded:
            EAL.set_metadata_tag(material, GRAPH_TAG, GRAPH_VERSION)
            _save(material)
        return material, "reused"

    EAL.make_directory(MATERIAL_DIR)
    material = TOOLS.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Cannot create " + path)
    try:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        material.set_editor_property("two_sided", False)
        material.set_editor_property("used_with_skeletal_mesh", True)
        _build_graph(material, textures)
        MEL.recompile_material(material)
        _validate(material, textures)
        EAL.set_metadata_tag(material, GRAPH_TAG, GRAPH_VERSION)
        _save(material)
    except Exception:
        # Brand-new and unreferenced: drop the whole asset so the next run starts clean.
        EAL.delete_loaded_asset(material)
        raise
    return material, "created"


# ---------------------------------------------------------------- entry

def build():
    report = {"status": "in_progress", "graph_version": GRAPH_VERSION, "license": "Renderpeople free model, local "
              "game use only; not CC0; do not redistribute raw maps", "characters": {}}
    _write_json(REPORT_FILE, report)
    try:
        for person in PEOPLE:
            textures = _textures(person)
            material, status = _material(person, textures)
            report["characters"][person] = {
                "material": _path(material), "status": status,
                "textures": {label: {k: v for k, v in item.items() if k != "asset"} for label, item in textures.items()},
                "scalars": {name: float(MEL.get_material_default_scalar_parameter_value(material, name))
                            for name in SCALARS},
                "masks": MASK_NOTES[person]}
        report["normal_convention"] = {
            "parameter": "NormalGreenSign", "default": NORMAL_GREEN_SIGN, "verified": False,
            "note": "Default assumes the UE-targeted package is DirectX-oriented. Confirm with a grazing-light render "
                    "of cloth folds and buttons; if relief reads inverted set -1 on the material or an instance."}
        report["notes"] = [
            "One DefaultLit opaque response for the whole atlas; skin is not separable (masks are garment masks), "
            "so no subsurface profile is forced across clothing.",
            "Roughness = clamp(1 - gloss, .42, .88); Specular .35; Metallic 0; no emissive; UVs untouched.",
            f"All maps capped at {MAX_TEXTURE_SIZE} for the 64-NPC budget; the hero shares them initially.",
            "Material slot assignment is left to the character importer / C++.",
        ]
        report["status"] = "passed"
        _write_json(REPORT_FILE, report)
        unreal.log(SENTINEL + " " + json.dumps({"materials": {p: c["material"] for p, c in report["characters"].items()},
                                                "report": str(REPORT_FILE)}))
        return report
    except Exception:
        report["status"] = "failed"
        report["error"] = traceback.format_exc()
        _write_json(REPORT_FILE, report)
        raise


if __name__ == "__main__":
    build()

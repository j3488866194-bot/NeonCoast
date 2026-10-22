"""Editor-only PBR masters and palette instances for the realistic coast pass.

Run in UnrealEditor-Cmd with -run=pythonscript -script=<this file>. No actor is
spawned and nothing outside the assets below is touched.

Textures (CC0 Poly Haven, 2K JPG sources in ArtSource/PublicAssets/<id>/textures):
    /Game/PublicAssets/Textures/<id>/T_<id>_Diffuse     sRGB, TC_Default, World
    /Game/PublicAssets/Textures/<id>/T_<id>_Normal      linear, TC_Normalmap, WorldNormalMap
                                                        (Poly Haven nor_dx = UE convention, no green flip)
    /Game/PublicAssets/Textures/<id>/T_<id>_Roughness   linear, TC_Masks, World
    All streamed with mips, max size 2048. Imported only when missing; settings
    are reapplied (and saved) only when they differ.

Masters (/Game/Materials/Realism), production version 3:
    M_CoastPBRGroundV3  world XY projection and explicit world-up normal basis
                        for horizontal ground. The 2-4 cm slab edges stretch.
    M_CoastPBRWallV3    dominant-axis box projection for walls, Concrete cubes
                        and the sloped WetSand shelf; preserves geometric normals.
    Both: DefaultLit, opaque, metallic 0, no emissive, used with instanced meshes.
    Normal: the decoded DX normal is rebuilt and output directly in world space.
    Ground has no mesh tangent-basis dependency. This avoids the invalid world
    normals observed on the giant thin coast HISM in a WorldNormal buffer capture.
    Old v1/v2 assets are preserved and are never rebuilt or deleted.
    Parameters: BaseColorTex, NormalTex, RoughnessTex, TileCm, AlbedoTint,
    RoughnessScale, NormalStrength, MacroMin, MacroMax, MacroSizeCm.

Instances: /Game/Materials/Realism/MI_CoastPBR_<Palette> (path contract used by C++).

AlbedoTint = art-direction target / diffuse mean. Means come from
Saved/RealismTextureMeans.json when present ({"asphalt_02": [r, g, b]} or
{"asphalt_02": {"linear_mean": [r, g, b]}}, linear 0-1 after sRGB decode);
otherwise from conservative visual estimates recorded below. Textures with
their own natural colour (asphalt, concrete, sand, grass, brick) are matched on
luminance only so they are not doubly tinted; painted render palettes take the
full target colour.

Existing masters are validated and reused, never cleared: runtime constructor
references can root loaded expressions. Graph changes need a new version and an
explicit rebuild in a controlled editor session.

Controlled comparison (does not alter the default production build): set
NEON_COAST_REALISM_MODE=sidewalk_xy_diagnostic before this script to create V2
masters and switch ONLY the existing Sidewalk instance to direct world XY.
NEON_COAST_REALISM_MODE=restore_sidewalk_v1 restores its original parent. V2
correctly includes the decoded normal Z channel; WallV2 stays unassigned.
The diagnostic's vertical slab edges have planar-UV stretching by design.
"""

from pathlib import Path
import json
import os
import traceback
import unreal

PROJECT = Path(__file__).resolve().parents[2]
SOURCE = PROJECT / "ArtSource" / "PublicAssets"
MANIFEST = SOURCE / "manifest.json"
MEANS_FILE = PROJECT / "Saved" / "RealismTextureMeans.json"
REPORT_FILE = PROJECT / "Saved" / "RealismMaterialReport.json"
TEXTURE_ROOT = "/Game/PublicAssets/Textures"
MATERIAL_DIR = "/Game/Materials/Realism"
V1_PATHS = {"Ground": MATERIAL_DIR + "/M_CoastPBRGround", "Wall": MATERIAL_DIR + "/M_CoastPBRWall"}
MASTER_PATHS = {"Ground": MATERIAL_DIR + "/M_CoastPBRGroundV3", "Wall": MATERIAL_DIR + "/M_CoastPBRWallV3"}
INSTANCE_PREFIX = "MI_CoastPBR_"
NOISE_TEXTURE = "/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq"
GRAPH_TAG = "NeonCoast.RealismMaterialGraph"
GRAPH_VERSIONS = {"Ground": "coast-pbr-ground-v3-flat-world-normal", "Wall": "coast-pbr-wall-v3-box-world-normal"}
V2_PATHS = {"Ground": MATERIAL_DIR + "/M_CoastPBRGroundV2", "Wall": MATERIAL_DIR + "/M_CoastPBRWallV2"}
V2_VERSIONS = {"Ground": "coast-pbr-ground-v2-direct-xy-diagnostic-nz",
               "Wall": "coast-pbr-wall-v2-vertex-box-nz"}
DIAGNOSTIC_REPORT = PROJECT / "Saved" / "RealismSidewalkDiagnosticReport.json"
SENTINEL = "NEON_COAST_REALISM_MATERIALS_READY"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# Physical repeat per the Poly Haven API (mm / 10). The manifest wins if it differs.
# estimated_mean: linear RGB read by eye from the 2K diffuse; replaced by measured means.
TEXTURE_SETS = {
    "asphalt_02": {"tile_cm": 300.0, "estimated_mean": (0.080, 0.080, 0.076)},
    "concrete_pavement": {"tile_cm": 180.0, "estimated_mean": (0.205, 0.180, 0.145)},
    "coast_sand_02": {"tile_cm": 1500.0, "estimated_mean": (0.290, 0.215, 0.130)},
    "white_plaster_02": {"tile_cm": 100.0, "estimated_mean": (0.200, 0.192, 0.160)},
    "red_brick_plaster_patch_02": {"tile_cm": 150.0, "estimated_mean": (0.240, 0.110, 0.060)},
    "grass_path_2": {"tile_cm": 100.0, "estimated_mean": (0.180, 0.160, 0.095)},
}
SOURCE_SUFFIX = {"Diffuse": "diff_2k.jpg", "Normal": "nor_dx_2k.jpg", "Roughness": "rough_2k.jpg"}

# (palette, master, texture set, target linear albedo mean, chroma weight, roughness scale, normal strength)
# Chroma weight 0 keeps the photographed hue and matches luminance only; 1 applies the full target colour.
PALETTES = (
    ("Asphalt", "Ground", "asphalt_02", (0.10, 0.10, 0.10), 0.0, 1.0, 1.0),
    ("Sidewalk", "Ground", "concrete_pavement", (0.32, 0.32, 0.32), 0.0, 1.0, 1.0),
    ("Concrete", "Wall", "concrete_pavement", (0.28, 0.28, 0.28), 0.0, 1.05, 1.0),
    ("Promenade", "Ground", "concrete_pavement", (0.42, 0.33, 0.27), 0.6, 1.0, 1.0),
    ("Lawn", "Ground", "grass_path_2", (0.09, 0.13, 0.05), 0.5, 1.0, 1.0),
    ("Sand", "Ground", "coast_sand_02", (0.42, 0.42, 0.42), 0.0, 1.0, 1.0),
    ("WetSand", "Wall", "coast_sand_02", (0.20, 0.20, 0.20), 0.0, 0.45, 0.6),
    ("DecoWhite", "Wall", "white_plaster_02", (0.66, 0.64, 0.60), 1.0, 1.0, 1.0),
    ("DecoMint", "Wall", "white_plaster_02", (0.50, 0.60, 0.53), 1.0, 1.0, 1.0),
    ("DecoPink", "Wall", "white_plaster_02", (0.68, 0.52, 0.50), 1.0, 1.0, 1.0),
    ("DecoLemon", "Wall", "white_plaster_02", (0.70, 0.63, 0.45), 1.0, 1.0, 1.0),
    ("DecoLavender", "Wall", "white_plaster_02", (0.55, 0.52, 0.62), 1.0, 1.0, 1.0),
    ("DecoPeach", "Wall", "white_plaster_02", (0.70, 0.55, 0.44), 1.0, 1.0, 1.0),
    ("DecoAqua", "Wall", "white_plaster_02", (0.46, 0.58, 0.60), 1.0, 1.0, 1.0),
    ("Stucco", "Wall", "white_plaster_02", (0.56, 0.50, 0.42), 1.0, 1.0, 1.0),
    ("Terracotta", "Wall", "red_brick_plaster_patch_02", (0.34, 0.20, 0.14), 0.25, 1.0, 1.0),
)

MASTER_DEFAULTS = {
    "Ground": {"texture_set": "asphalt_02", "MacroMin": 0.88, "MacroMax": 1.08, "MacroSizeCm": 450.0},
    "Wall": {"texture_set": "white_plaster_02", "MacroMin": 0.95, "MacroMax": 1.04, "MacroSizeCm": 700.0},
}
TEXTURE_PARAMETERS = {"BaseColorTex": "Diffuse", "NormalTex": "Normal", "RoughnessTex": "Roughness"}
SCALAR_PARAMETERS = ("TileCm", "RoughnessScale", "NormalStrength", "MacroMin", "MacroMax", "MacroSizeCm")
VECTOR_PARAMETERS = ("AlbedoTint",)
LUMA = (0.2126, 0.7152, 0.0722)
TINT_RANGE = (0.25, 4.0)


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

def _texture_settings(role):
    tcs, group = unreal.TextureCompressionSettings, unreal.TextureGroup
    common = {"max_texture_size": 2048, "never_stream": False,
              "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP}
    if role == "Diffuse":
        return dict(common, srgb=True, compression_settings=tcs.TC_DEFAULT, lod_group=group.TEXTUREGROUP_WORLD)
    if role == "Normal":
        return dict(common, srgb=False, compression_settings=tcs.TC_NORMALMAP,
                    lod_group=group.TEXTUREGROUP_WORLD_NORMAL_MAP, flip_green_channel=False)
    return dict(common, srgb=False, compression_settings=tcs.TC_MASKS, lod_group=group.TEXTUREGROUP_WORLD)


def _apply_settings(texture, settings):
    changed = []
    for key, value in settings.items():
        if texture.get_editor_property(key) != value:
            texture.set_editor_property(key, value)
            changed.append(key)
    if changed:
        _save(texture)
    return changed


def _import_texture(filename, directory, name):
    path = directory + "/" + name
    task = unreal.AssetImportTask()
    for key, value in {"filename": str(filename), "destination_path": directory, "destination_name": name,
                       "automated": True, "replace_existing": False, "save": True}.items():
        task.set_editor_property(key, value)
    task.set_editor_property("factory", unreal.TextureFactory())
    TOOLS.import_asset_tasks([task])
    texture = _load(path, unreal.Texture2D)
    if texture is None:
        raise RuntimeError(f"Texture import did not produce {path} from {filename}")
    return texture


def _manifest():
    if not MANIFEST.is_file():
        unreal.log_warning("Neon Coast: " + str(MANIFEST) + " missing; using recorded tile sizes")
        return {}
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    return {asset["id"]: asset for asset in data.get("assets", []) if asset.get("kind") == "texture"}


def _textures():
    manifest = _manifest()
    result = {}
    for set_id, info in TEXTURE_SETS.items():
        tile = info["tile_cm"]
        entry = manifest.get(set_id, {})
        measured = entry.get("tile_size_cm")
        if measured:
            if abs(measured[0] - measured[1]) > 0.01 * measured[0]:
                unreal.log_warning(f"Neon Coast: {set_id} is not square {measured}; using its X size")
            if abs(measured[0] - tile) > 0.01 * tile:
                unreal.log_warning(f"Neon Coast: {set_id} manifest tile {measured[0]} cm overrides {tile} cm")
            tile = float(measured[0])
        hashes = {f["role"]: f.get("sha256") for f in entry.get("files", [])}
        directory = TEXTURE_ROOT + "/" + set_id
        assets = {}
        for role, suffix in SOURCE_SUFFIX.items():
            name = f"T_{set_id}_{role}"
            texture = _load(directory + "/" + name, unreal.Texture2D)
            filename = SOURCE / set_id / "textures" / f"{set_id}_{suffix}"
            imported = texture is None
            if imported:
                if not filename.is_file() or filename.stat().st_size == 0:
                    raise RuntimeError("Missing CC0 source texture: " + str(filename))
                EAL.make_directory(directory)
                texture = _import_texture(filename, directory, name)
            changed = _apply_settings(texture, _texture_settings(role))
            source_role = {"Diffuse": "base_color", "Normal": "normal_directx", "Roughness": "roughness"}[role]
            assets[role] = {"asset": texture, "path": _path(texture), "imported_now": imported,
                            "changed_settings": changed, "source": str(filename),
                            "source_sha256": hashes.get(source_role)}
        result[set_id] = {"tile_cm": tile, "assets": assets}
    return result


def _means():
    measured = {}
    if MEANS_FILE.is_file():
        data = json.loads(MEANS_FILE.read_text(encoding="utf-8-sig"))
        for set_id, value in data.items():
            value = value.get("linear_mean") if isinstance(value, dict) else value
            if (isinstance(value, (list, tuple)) and len(value) >= 3
                    and all(isinstance(v, (int, float)) and 0.005 < v <= 1.0 for v in value[:3])):
                measured[set_id] = tuple(float(v) for v in value[:3])
            else:
                unreal.log_warning(f"Neon Coast: ignoring invalid measured mean for {set_id}: {value!r}")
    return {set_id: (measured[set_id], "measured") if set_id in measured else (info["estimated_mean"], "estimated")
            for set_id, info in TEXTURE_SETS.items()}


def _luma(color):
    return sum(w * v for w, v in zip(LUMA, color))


def _tint(target, mean, chroma_weight):
    target_luma, mean_luma = max(_luma(target), 1e-4), max(_luma(mean), 1e-4)
    scale = target_luma / mean_luma
    tint = []
    for t, m in zip(target, mean):
        chroma_ratio = (t / target_luma) / max(m / mean_luma, 1e-3)
        value = scale * (1.0 + chroma_weight * (chroma_ratio - 1.0))
        tint.append(round(min(max(value, TINT_RANGE[0]), TINT_RANGE[1]), 4))
    return tint


# ---------------------------------------------------------------- graph

class _Graph:
    """Values are expression nodes or (node, output) pairs; plain floats become constant inputs."""

    def __init__(self, material):
        self.material = material
        self.count = 0

    def node(self, cls, **props):
        x, y = -2600 + 240 * (self.count % 10), -900 + 160 * (self.count // 10)
        node = MEL.create_material_expression(self.material, cls, x, y)
        if node is None:
            raise RuntimeError("Could not create " + cls.__name__)
        self.count += 1
        for key, value in props.items():
            node.set_editor_property(key, value)
        return node

    def link(self, value, dst, inputs):
        src, out = value if isinstance(value, tuple) else (value, "")
        for name in inputs:
            if MEL.connect_material_expressions(src, out, dst, name):
                return
        raise RuntimeError("Failed to connect %s.%s -> %s%s" % (src.get_name(), out, dst.get_name(), list(inputs)))

    def _feed(self, node, pins):
        for value, pin, const in pins:
            if isinstance(value, (int, float)):
                node.set_editor_property(const, float(value))
            else:
                self.link(value, node, (pin,))
        return node

    def op(self, cls, a, b):
        return self._feed(self.node(cls), ((a, "A", "const_a"), (b, "B", "const_b")))

    def lerp(self, a, b, alpha):
        return self._feed(self.node(unreal.MaterialExpressionLinearInterpolate),
                          ((a, "A", "const_a"), (b, "B", "const_b"), (alpha, "Alpha", "const_alpha")))

    def unary(self, cls, value, inputs=("", "Input"), **props):
        node = self.node(cls, **props)
        self.link(value, node, inputs)
        return node

    def mask(self, value, channels):
        return self.unary(unreal.MaterialExpressionComponentMask, value, r="r" in channels,
                          g="g" in channels, b="b" in channels, a="a" in channels)

    def append(self, a, b):
        node = self.node(unreal.MaterialExpressionAppendVector)
        self.link(a, node, ("A",))
        self.link(b, node, ("B",))
        return node

    def const3(self, x, y, z):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(x, y, z, 0.0))

    def scalar(self, name, default, group):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name,
                         default_value=float(default), group=group)

    def texture(self, name, texture, uv, group):
        node = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name,
                         texture=texture, sampler_type=_sampler_for(texture), group=group)
        self.link(uv, node, ("UVs",))
        return node


def _build_surface(material, kind, texture_set, noise, direct_xy=False, correct_normal_z=False,
                   world_space_normal=False, flat_ground_normal=False):
    defaults = MASTER_DEFAULTS[kind]
    g = _Graph(material)
    tile = g.scalar("TileCm", texture_set["tile_cm"], "Projection")
    macro_size = g.scalar("MacroSizeCm", defaults["MacroSizeCm"], "Projection")
    macro_min = g.scalar("MacroMin", defaults["MacroMin"], "Surface")
    macro_max = g.scalar("MacroMax", defaults["MacroMax"], "Surface")
    rough_scale = g.scalar("RoughnessScale", 1.0, "Surface")
    strength = g.scalar("NormalStrength", 1.0, "Surface")
    tint_param = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="AlbedoTint",
                        default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0), group="Surface")
    tint = g.mask(tint_param, "rgb")

    # Hard box projection on the dominant world axis of the vertex normal. Up faces
    # get world XY (U=+X, V=+Y); X faces U=+Y, V=-Z; Y faces U=+X, V=-Z (texture up = world up).
    world = g.node(unreal.MaterialExpressionWorldPosition)
    vertex_normal = g.const3(0, 0, 1) if flat_ground_normal else g.node(unreal.MaterialExpressionVertexNormalWS)
    if not direct_xy:
        abs_normal = g.unary(unreal.MaterialExpressionAbs, vertex_normal)
        ax, ay, az = (g.mask(abs_normal, c) for c in "rgb")
        side = g.op(unreal.MaterialExpressionMax, ax, ay)
        pick_z = g.unary(unreal.MaterialExpressionSaturate,
                         g.op(unreal.MaterialExpressionAdd,
                              g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, az, side), 1000.0), 0.5))
        pick_x = g.unary(unreal.MaterialExpressionSaturate,
                         g.op(unreal.MaterialExpressionAdd,
                              g.op(unreal.MaterialExpressionMultiply, g.op(unreal.MaterialExpressionSubtract, ax, ay), 1000.0), 0.5))
        px, py, pz = (g.mask(world, c) for c in "rgb")
        down = g.op(unreal.MaterialExpressionMultiply, pz, -1.0)
        uv_side = g.lerp(g.append(px, down), g.append(py, down), pick_x)
    # The V2 Ground diagnostic deliberately bypasses the axis selector. Only the
    # Sidewalk instance is switched to it; 3 cm vertical slab edges are a known
    # limitation until a rendered comparison establishes whether selection is
    # responsible for the streaks. Never silently apply it to vertical walls.
    uv_cm = g.mask(world, "rg") if direct_xy else g.lerp(uv_side, g.mask(world, "rg"), pick_z)
    uv = g.op(unreal.MaterialExpressionDivide, uv_cm, tile)

    assets = texture_set["assets"]
    base = g.texture("BaseColorTex", assets["Diffuse"]["asset"], uv, "Textures")
    normal = g.texture("NormalTex", assets["Normal"]["asset"], uv, "Textures")
    rough = g.texture("RoughnessTex", assets["Roughness"]["asset"], uv, "Textures")

    color = g.op(unreal.MaterialExpressionMultiply, (base, "RGB"), tint)
    if noise is not None:
        # Two octaves of the 64px engine noise stretched to metre-scale blobs.
        macro_uv = g.op(unreal.MaterialExpressionMultiply,
                        g.op(unreal.MaterialExpressionDivide, uv_cm, macro_size), 1.0 / 64.0)
        sampler = _sampler_for(noise)
        coarse = g.node(unreal.MaterialExpressionTextureSample, texture=noise, sampler_type=sampler)
        g.link(macro_uv, coarse, ("UVs",))
        fine = g.node(unreal.MaterialExpressionTextureSample, texture=noise, sampler_type=sampler)
        g.link(g.op(unreal.MaterialExpressionMultiply, macro_uv, 3.17), fine, ("UVs",))
        blend = g.lerp((coarse, "R"), (fine, "R"), 0.35)
        color = g.op(unreal.MaterialExpressionMultiply, color, g.lerp(macro_min, macro_max, blend))
    color = g.unary(unreal.MaterialExpressionSaturate, color)

    roughness = g.unary(unreal.MaterialExpressionSaturate,
                        g.op(unreal.MaterialExpressionMultiply, (rough, "R"), rough_scale))

    # DX: +G = +V. The decoded normal's B channel is already signed/unpacked
    # by SAMPLERTYPE_NORMAL, so it must weight the geometric normal directly.
    u_axis = (g.const3(1, 0, 0) if direct_xy else
              g.lerp(g.lerp(g.const3(1, 0, 0), g.const3(0, 1, 0), pick_x), g.const3(1, 0, 0), pick_z))
    v_axis = g.const3(0, 1, 0) if direct_xy else g.lerp(g.const3(0, 0, -1), g.const3(0, 1, 0), pick_z)
    nx = g.op(unreal.MaterialExpressionMultiply, (normal, "R"), strength)
    ny = g.op(unreal.MaterialExpressionMultiply, (normal, "G"), strength)
    perturb = g.op(unreal.MaterialExpressionAdd,
                   g.op(unreal.MaterialExpressionMultiply, u_axis, nx),
                   g.op(unreal.MaterialExpressionMultiply, v_axis, ny))
    base_normal = (g.op(unreal.MaterialExpressionMultiply, vertex_normal, (normal, "B"))
                   if correct_normal_z else vertex_normal)
    world_normal = g.unary(unreal.MaterialExpressionNormalize,
                           g.op(unreal.MaterialExpressionAdd, base_normal, perturb), inputs=("VectorInput", ""))
    output_normal = world_normal
    if not world_space_normal:
        output_normal = g.unary(unreal.MaterialExpressionTransform, world_normal, inputs=("Input", ""),
                                transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                                transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    metallic = g.node(unreal.MaterialExpressionConstant, r=0.0)

    for node, prop in ((color, unreal.MaterialProperty.MP_BASE_COLOR),
                       (roughness, unreal.MaterialProperty.MP_ROUGHNESS),
                       (output_normal, unreal.MaterialProperty.MP_NORMAL),
                       (metallic, unreal.MaterialProperty.MP_METALLIC)):
        if not MEL.connect_material_property(node, "", prop):
            raise RuntimeError(f"Cannot connect {prop} on {_path(material)}")
    return g.count


def _validate_master(material, world_space_normal=False):
    problems = []
    expected = {"blend_mode": unreal.BlendMode.BLEND_OPAQUE, "two_sided": False,
                "shading_model": unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
                "tangent_space_normal": not world_space_normal}
    problems += [key for key, value in expected.items() if material.get_editor_property(key) != value]
    names = {"texture": set(str(n) for n in MEL.get_texture_parameter_names(material)),
             "scalar": set(str(n) for n in MEL.get_scalar_parameter_names(material)),
             "vector": set(str(n) for n in MEL.get_vector_parameter_names(material))}
    for kind, required in (("texture", TEXTURE_PARAMETERS), ("scalar", SCALAR_PARAMETERS), ("vector", VECTOR_PARAMETERS)):
        missing = sorted(set(required) - names[kind])
        if missing:
            problems.append(f"missing {kind} parameters {missing}")
    for prop in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_NORMAL,
                 unreal.MaterialProperty.MP_ROUGHNESS):
        if MEL.get_material_property_input_node(material, prop) is None:
            problems.append(f"{prop} unconnected")
    if MEL.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR) is not None:
        problems.append("unexpected emissive connection")
    if problems:
        raise RuntimeError(_path(material) + ": " + "; ".join(problems) + ". Existing graph preserved; rebuild it "
                           "explicitly in a controlled editor session after review.")


def _parameter_defaults(material):
    return {"scalar": {str(n): float(MEL.get_material_default_scalar_parameter_value(material, n))
                       for n in MEL.get_scalar_parameter_names(material)},
            "texture": {str(n): _path(t) if (t := MEL.get_material_default_texture_parameter_value(material, n)) else None
                        for n in MEL.get_texture_parameter_names(material)}}


def _master(kind, textures, noise, v2=False):
    path = (V2_PATHS if v2 else MASTER_PATHS)[kind]
    version = (V2_VERSIONS if v2 else GRAPH_VERSIONS)[kind]
    material = _load(path, unreal.Material)
    if material is not None:
        recorded = EAL.get_metadata_tag(material, GRAPH_TAG)
        if v2 and not recorded:
            raise RuntimeError(f"{path}: existing diagnostic master has no graph provenance; preserved unchanged")
        if recorded and recorded != version:
            raise RuntimeError(f"{path}: graph version {recorded} differs from {version}. Existing expressions were "
                               "preserved; rebuild explicitly in a controlled editor session.")
        _validate_master(material, world_space_normal=not v2)
        dirty = False
        if not material.get_editor_property("used_with_instanced_static_meshes"):
            material.set_editor_property("used_with_instanced_static_meshes", True)
            dirty = True
        if not recorded:
            EAL.set_metadata_tag(material, GRAPH_TAG, version)
            dirty = True
        if dirty:
            MEL.recompile_material(material)
            _save(material)
        unreal.log(f"Neon Coast: reused {path} ({version})")
        return material, "reused", None

    EAL.make_directory(MATERIAL_DIR)
    material = TOOLS.create_asset(path.rsplit("/", 1)[1], MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Cannot create " + path)
    try:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        material.set_editor_property("two_sided", False)
        material.set_editor_property("tangent_space_normal", v2)
        material.set_editor_property("used_with_instanced_static_meshes", True)
        count = _build_surface(material, kind, textures[MASTER_DEFAULTS[kind]["texture_set"]], noise,
                               direct_xy=kind == "Ground", correct_normal_z=True,
                               world_space_normal=not v2, flat_ground_normal=not v2 and kind == "Ground")
        MEL.layout_material_expressions(material)
        MEL.recompile_material(material)
        _validate_master(material, world_space_normal=not v2)
        EAL.set_metadata_tag(material, GRAPH_TAG, version)
        _save(material)
    except Exception:
        # Brand-new and unreferenced: drop the whole asset so the next run starts clean.
        EAL.delete_loaded_asset(material)
        raise
    unreal.log(f"Neon Coast: built {path} ({version}, {count} expressions)")
    return material, "created", count


# ---------------------------------------------------------------- instances

def _instance(palette, parent, texture_set, tint, rough_scale, normal_strength):
    name = INSTANCE_PREFIX + palette
    path = MATERIAL_DIR + "/" + name
    instance = _load(path, unreal.MaterialInstanceConstant)
    created = instance is None
    if created:
        instance = TOOLS.create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant,
                                      unreal.MaterialInstanceConstantFactoryNew())
        if instance is None:
            raise RuntimeError("Cannot create " + path)
    if instance.get_editor_property("parent") != parent:
        MEL.set_material_instance_parent(instance, parent)
    for param, role in TEXTURE_PARAMETERS.items():
        MEL.set_material_instance_texture_parameter_value(instance, param, texture_set["assets"][role]["asset"])
    scalars = {"TileCm": texture_set["tile_cm"], "RoughnessScale": rough_scale, "NormalStrength": normal_strength}
    # These palettes previously inherited Ground macro variation. Preserve it
    # explicitly while using the box-projected Wall master for their vertical or
    # tilted faces; the migration must not change their authored appearance.
    if palette in ("Concrete", "WetSand"):
        scalars.update({key: MASTER_DEFAULTS["Ground"][key]
                        for key in ("MacroMin", "MacroMax", "MacroSizeCm")})
    for param, value in scalars.items():
        MEL.set_material_instance_scalar_parameter_value(instance, param, float(value))
    MEL.set_material_instance_vector_parameter_value(
        instance, "AlbedoTint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    MEL.update_material_instance(instance)
    # UE 5.8 MaterialEditingLibrary.cpp setters mutate the instance but return an
    # unchanged false local. Verify the actual values instead of that return code.
    failed = [param for param, role in TEXTURE_PARAMETERS.items()
              if MEL.get_material_instance_texture_parameter_value(instance, param) != texture_set["assets"][role]["asset"]]
    failed += [param for param, value in scalars.items()
               if abs(MEL.get_material_instance_scalar_parameter_value(instance, param) - value) > .001]
    actual_tint = MEL.get_material_instance_vector_parameter_value(instance, "AlbedoTint")
    if any(abs(a - b) > .001 for a, b in zip((actual_tint.r, actual_tint.g, actual_tint.b), tint)):
        failed.append("AlbedoTint")
    if failed:
        raise RuntimeError(f"{path}: material parameter readback failed {failed}")
    _save(instance)
    return instance, created, scalars


# ---------------------------------------------------------------- entry

def _instance_state(instance):
    """Effective values, including inherited macro defaults, for comparison."""
    tint = MEL.get_material_instance_vector_parameter_value(instance, "AlbedoTint")
    return {
        "texture": {name: _path(texture) if (texture :=
                    MEL.get_material_instance_texture_parameter_value(instance, name)) else None
                    for name in TEXTURE_PARAMETERS},
        "scalar": {name: float(MEL.get_material_instance_scalar_parameter_value(instance, name))
                   for name in SCALAR_PARAMETERS},
        "AlbedoTint": [float(tint.r), float(tint.g), float(tint.b), float(tint.a)],
    }


def _reparent_sidewalk(parent):
    path = MATERIAL_DIR + "/" + INSTANCE_PREFIX + "Sidewalk"
    instance = _load(path, unreal.MaterialInstanceConstant)
    if instance is None:
        raise RuntimeError("Diagnostic requires the existing production instance: " + path)
    old_parent = instance.get_editor_property("parent")
    if old_parent is None or _path(old_parent) not in (V1_PATHS["Ground"], V2_PATHS["Ground"], MASTER_PATHS["Ground"]):
        raise RuntimeError("Unexpected Sidewalk parent; preserved unchanged: " + str(old_parent))
    before = _instance_state(instance)
    MEL.set_material_instance_parent(instance, parent)
    MEL.update_material_instance(instance)
    after = _instance_state(instance)
    if before != after or instance.get_editor_property("parent") != parent:
        MEL.set_material_instance_parent(instance, old_parent)
        MEL.update_material_instance(instance)
        raise RuntimeError("Sidewalk parent comparison changed surface parameters; restored original parent")
    _save(instance)
    return {"instance": path, "old_parent": _path(old_parent), "new_parent": _path(parent),
            "parameters_preserved": True, "before": before, "after": after}


def diagnostic_sidewalk_xy():
    """Reversible direct-XY comparison; no other palette instance is modified."""
    report = {"status": "in_progress", "mode": "sidewalk_xy_diagnostic", "masters": {},
              "render_verified": False,
              "scope": "Only the existing Sidewalk MI parent is changed; other 15 MI assets remain untouched.",
              "known_limit": "Direct XY stretches on vertical slab edges; this is an isolation experiment."}
    _write_json(DIAGNOSTIC_REPORT, report)
    try:
        textures = _textures()
        noise = unreal.load_asset(NOISE_TEXTURE)
        masters = {}
        for kind in ("Ground", "Wall"):
            material, status, count = _master(kind, textures, noise, v2=True)
            masters[kind] = material
            report["masters"][kind] = {"path": _path(material), "status": status,
                                       "graph_version": V2_VERSIONS[kind], "expressions_created": count}
        report["comparison"] = _reparent_sidewalk(masters["Ground"])
        report["status"] = "ready_for_render_comparison"
        _write_json(DIAGNOSTIC_REPORT, report)
        unreal.log("NEON_COAST_SIDEWALK_XY_DIAGNOSTIC_READY " + json.dumps(report["comparison"]))
        return report
    except Exception:
        report["status"] = "failed"
        report["error"] = traceback.format_exc()
        _write_json(DIAGNOSTIC_REPORT, report)
        raise


def restore_sidewalk_v1():
    """Revert only the diagnostic's parent change, keeping all MI parameters."""
    parent = _load(V1_PATHS["Ground"], unreal.Material)
    if parent is None:
        raise RuntimeError("Original Ground master missing; no changes made")
    report = {"status": "restored", "mode": "restore_sidewalk_v1",
              "comparison": _reparent_sidewalk(parent)}
    _write_json(DIAGNOSTIC_REPORT, report)
    unreal.log("NEON_COAST_SIDEWALK_V1_RESTORED " + json.dumps(report["comparison"]))
    return report


def build():
    report = {"status": "in_progress", "graph_versions": GRAPH_VERSIONS, "masters": {}, "textures": {},
              "instances": {}, "means_file": str(MEANS_FILE)}
    _write_json(REPORT_FILE, report)
    try:
        textures = _textures()
        means = _means()
        noise = unreal.load_asset(NOISE_TEXTURE)
        if noise is None:
            unreal.log_warning("Neon Coast: " + NOISE_TEXTURE + " missing; masters built without macro variation")
        for set_id, data in textures.items():
            report["textures"][set_id] = {
                "tile_cm": data["tile_cm"], "linear_mean": list(means[set_id][0]), "mean_source": means[set_id][1],
                "assets": {role: {k: v for k, v in item.items() if k != "asset"} for role, item in data["assets"].items()}}

        masters = {}
        for kind in ("Ground", "Wall"):
            material, status, count = _master(kind, textures, noise)
            masters[kind] = material
            report["masters"][kind] = {"path": _path(material), "status": status, "expressions_created": count,
                                       "graph_version": GRAPH_VERSIONS[kind],
                                       "macro_variation": (noise is not None) if status == "created" else None,
                                       "defaults": _parameter_defaults(material)}

        for palette, kind, set_id, target, chroma, rough_scale, normal_strength in PALETTES:
            tint = _tint(target, means[set_id][0], chroma)
            instance, created, scalars = _instance(palette, masters[kind], textures[set_id], tint,
                                                   rough_scale, normal_strength)
            report["instances"][palette] = {
                "path": _path(instance), "created": created, "parent": _path(masters[kind]), "texture_set": set_id,
                "target_linear_albedo": list(target), "chroma_weight": chroma, "AlbedoTint": tint,
                "tint_from": means[set_id][1], **scalars}

        report["notes"] = [
            "Tile sizes are the Poly Haven physical dimensions; palettes sharing a texture share its real scale.",
            "Estimated means are visual reads of the 2K diffuse; write Saved/RealismTextureMeans.json and rerun to "
            "recompute AlbedoTint from measured values. Plaster photographs grey, so Deco tints are large by design.",
            "Box projection selects one axis per pixel (one sample per map) instead of 3-way blended triplanar; "
            "faces of rotated boxes near 45 degrees stretch up to 1.41x.",
            "Production v3 outputs world-space normals directly. Horizontal Ground uses a fixed up basis, "
            "bypassing the invalid thin-HISM tangent basis seen in the WorldNormal diagnostic capture.",
            "Concrete and WetSand use Wall box projection for vertical/tilted faces and explicitly retain "
            "their prior Ground macro settings. Ground slab edges have planar-UV stretching.",
            "Normal maps are Poly Haven DirectX (nor_dx); no green flip anywhere.",
        ]
        report["status"] = "passed"
        _write_json(REPORT_FILE, report)
        unreal.log(SENTINEL + " " + json.dumps({"masters": len(masters), "instances": len(report["instances"]),
                                                "texture_sets": len(textures), "report": str(REPORT_FILE)}))
        return report
    except Exception:
        report["status"] = "failed"
        report["error"] = traceback.format_exc()
        _write_json(REPORT_FILE, report)
        raise


if __name__ == "__main__":
    mode = os.environ.get("NEON_COAST_REALISM_MODE", "build")
    if mode == "sidewalk_xy_diagnostic":
        diagnostic_sidewalk_xy()
    elif mode == "restore_sidewalk_v1":
        restore_sidewalk_v1()
    elif mode == "build":
        build()
    else:
        raise RuntimeError("Unknown NEON_COAST_REALISM_MODE: " + mode)

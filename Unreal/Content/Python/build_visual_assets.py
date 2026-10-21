"""Editor-only asset authoring for Neon Coast visuals.

Creates (or rebuilds in place) two materials used by ANCCityVisual:

/Game/Materials/M_Coast - the single opaque master for every city HISM batch.
    BaseColor (vector), Roughness, Metallic, EmissiveStrength (scalars).
    Surface detail, all default 0 so untouched palettes render flat:
      Grain              world-space two-octave grain on colour and roughness
                         (asphalt, pavers, sand, lawn, light ripple on water)
      InstanceVariation  per-instance tint jitter so repeated facades differ
      GroundOcclusion    contact darkening over the first ~2.6m above Z=0
    Emissive = BaseColor * EmissiveStrength (unaffected by detail, so neon stays clean).

/Game/Materials/M_CoastSignText - masked font material for TextRender signs.
    Base colour and emissive come from the TextRender colour; Glow (scalar)
    is animated by the day/night cycle. Uses the RobotoDistanceField alpha.

Run inside the Unreal Editor (Python Editor Script Plugin):
    py "<Project>/Content/Python/build_visual_assets.py"
Safe to run repeatedly. If the detail network cannot be built on this engine
version, M_Coast falls back to the flat parameter graph (same parameters).
"""

import unreal

MATERIAL_DIR = "/Game/Materials"
MATERIAL_NAME = "M_Coast"
MATERIAL_PATH = MATERIAL_DIR + "/" + MATERIAL_NAME
SIGN_NAME = "M_CoastSignText"
SIGN_PATH = MATERIAL_DIR + "/" + SIGN_NAME

NOISE_TEXTURE = "/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq"
SIGN_FONT = "/Engine/EngineFonts/RobotoDistanceField"

MEL = unreal.MaterialEditingLibrary


def _load_or_create_material(name):
    eal = unreal.EditorAssetLibrary
    path = MATERIAL_DIR + "/" + name
    if eal.does_asset_exist(path):
        material = eal.load_asset(path)
        if isinstance(material, unreal.Material):
            MEL.delete_all_material_expressions(material)
            return material
        raise RuntimeError(path + " exists but is not a Material")

    if not eal.does_directory_exist(MATERIAL_DIR):
        eal.make_directory(MATERIAL_DIR)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Failed to create " + path)
    return material


def _expr(material, cls, x, y, **props):
    node = MEL.create_material_expression(material, cls, x, y)
    if node is None:
        raise RuntimeError("Could not create " + cls.__name__)
    for key, value in props.items():
        node.set_editor_property(key, value)
    return node


def _scalar(material, name, default, x, y):
    return _expr(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=default)


def _link(src, src_out, dst, dst_inputs=("", "Input")):
    """Connect trying each candidate input name (single-input nodes report '' on some versions)."""
    if isinstance(dst_inputs, str):
        dst_inputs = (dst_inputs,)
    for name in dst_inputs:
        if MEL.connect_material_expressions(src, src_out, dst, name):
            return
    raise RuntimeError("Failed to connect %s.%s -> %s%s" % (src.get_name(), src_out, dst.get_name(), list(dst_inputs)))


def _sampler_for(texture):
    """Pick the sampler type matching the texture's compression, otherwise the material will not compile."""
    st = unreal.MaterialSamplerType
    try:
        comp = texture.get_editor_property("compression_settings")
        srgb = texture.get_editor_property("srgb")
    except Exception:
        return st.SAMPLERTYPE_LINEAR_COLOR
    tcs = unreal.TextureCompressionSettings
    if comp == tcs.TC_GRAYSCALE:
        return st.SAMPLERTYPE_GRAYSCALE if srgb else st.SAMPLERTYPE_LINEAR_GRAYSCALE
    if comp == tcs.TC_ALPHA:
        return st.SAMPLERTYPE_ALPHA
    if comp == tcs.TC_MASKS:
        return st.SAMPLERTYPE_MASKS
    if comp == tcs.TC_NORMALMAP:
        return st.SAMPLERTYPE_NORMAL
    return st.SAMPLERTYPE_COLOR if srgb else st.SAMPLERTYPE_LINEAR_COLOR


def _coast_parameters(material):
    base = _expr(material, unreal.MaterialExpressionVectorParameter, -1400, -400,
                 parameter_name="BaseColor", default_value=unreal.LinearColor(0.8, 0.74, 0.64, 1.0))
    params = {
        "base": base,
        "roughness": _scalar(material, "Roughness", 0.6, -1400, -200),
        "metallic": _scalar(material, "Metallic", 0.0, -1400, -80),
        "emissive_strength": _scalar(material, "EmissiveStrength", 0.0, -1400, 40),
    }
    emissive = _expr(material, unreal.MaterialExpressionMultiply, -380, 220)
    _link(base, "", emissive, "A")
    _link(params["emissive_strength"], "", emissive, "B")
    MEL.connect_material_property(params["metallic"], "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return params


def _build_flat(material):
    p = _coast_parameters(material)
    MEL.connect_material_property(p["base"], "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(p["roughness"], "", unreal.MaterialProperty.MP_ROUGHNESS)


def _build_detailed(material):
    p = _coast_parameters(material)
    noise = unreal.load_asset(NOISE_TEXTURE)
    if noise is None:
        raise RuntimeError("Missing " + NOISE_TEXTURE)

    grain = _scalar(material, "Grain", 0.0, -1400, 200)
    variation = _scalar(material, "InstanceVariation", 0.0, -1400, 320)
    occlusion = _scalar(material, "GroundOcclusion", 0.0, -1400, 440)

    # Two octaves of tiling noise in world XY: ~4m fine grain over ~40m patches.
    world = _expr(material, unreal.MaterialExpressionWorldPosition, -1400, 600)
    xy = _expr(material, unreal.MaterialExpressionComponentMask, -1200, 600, r=True, g=True, b=False, a=False)
    _link(world, "", xy)
    uv_fine = _expr(material, unreal.MaterialExpressionDivide, -1050, 560, const_b=420.0)
    uv_coarse = _expr(material, unreal.MaterialExpressionDivide, -1050, 680, const_b=3900.0)
    _link(xy, "", uv_fine, "A")
    _link(xy, "", uv_coarse, "A")
    sampler = _sampler_for(noise)
    fine = _expr(material, unreal.MaterialExpressionTextureSample, -900, 540, texture=noise, sampler_type=sampler)
    coarse = _expr(material, unreal.MaterialExpressionTextureSample, -900, 700, texture=noise, sampler_type=sampler)
    _link(uv_fine, "", fine, "UVs")
    _link(uv_coarse, "", coarse, "UVs")
    mix = _expr(material, unreal.MaterialExpressionLinearInterpolate, -700, 600, const_alpha=0.6)
    _link(coarse, "R", mix, "A")
    _link(fine, "R", mix, "B")
    centered = _expr(material, unreal.MaterialExpressionSubtract, -560, 600, const_b=0.5)
    _link(mix, "", centered, "A")
    grain_term = _expr(material, unreal.MaterialExpressionMultiply, -420, 600)
    _link(centered, "", grain_term, "A")
    _link(grain, "", grain_term, "B")

    # Per-instance tint jitter in [-Variation, +Variation].
    rand = _expr(material, unreal.MaterialExpressionPerInstanceRandom, -1200, 820)
    rand_c = _expr(material, unreal.MaterialExpressionSubtract, -1050, 820, const_b=0.5)
    _link(rand, "", rand_c, "A")
    inst = _expr(material, unreal.MaterialExpressionMultiply, -900, 820)
    _link(rand_c, "", inst, "A")
    _link(variation, "", inst, "B")
    inst2 = _expr(material, unreal.MaterialExpressionMultiply, -760, 820, const_b=2.0)
    _link(inst, "", inst2, "A")

    grain_color = _expr(material, unreal.MaterialExpressionMultiply, -280, 600, const_b=0.9)
    _link(grain_term, "", grain_color, "A")
    jitter = _expr(material, unreal.MaterialExpressionAdd, -160, 680)
    _link(grain_color, "", jitter, "A")
    _link(inst2, "", jitter, "B")
    factor = _expr(material, unreal.MaterialExpressionAdd, -40, 680, const_b=1.0)
    _link(jitter, "", factor, "A")

    # Contact darkening: 0.74 at the ground rising to 1.0 by 2.6m, blended in by GroundOcclusion.
    z = _expr(material, unreal.MaterialExpressionComponentMask, -1200, 960, r=False, g=False, b=True, a=False)
    _link(world, "", z)
    z_norm = _expr(material, unreal.MaterialExpressionDivide, -1050, 960, const_b=260.0)
    _link(z, "", z_norm, "A")
    z_sat = _expr(material, unreal.MaterialExpressionSaturate, -900, 960)
    _link(z_norm, "", z_sat)
    ao = _expr(material, unreal.MaterialExpressionLinearInterpolate, -760, 960, const_a=0.74, const_b=1.0)
    _link(z_sat, "", ao, "Alpha")
    ao_mix = _expr(material, unreal.MaterialExpressionLinearInterpolate, -600, 960, const_a=1.0, const_b=1.0)
    _link(ao, "", ao_mix, "B")
    _link(occlusion, "", ao_mix, "Alpha")

    tinted = _expr(material, unreal.MaterialExpressionMultiply, 100, -300)
    _link(p["base"], "", tinted, "A")
    _link(factor, "", tinted, "B")
    shaded = _expr(material, unreal.MaterialExpressionMultiply, 240, -300)
    _link(tinted, "", shaded, "A")
    _link(ao_mix, "", shaded, "B")
    MEL.connect_material_property(shaded, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Grain also breaks up roughness (dry/polished patches, ripples on water).
    rough_delta = _expr(material, unreal.MaterialExpressionMultiply, -160, -120, const_b=0.35)
    _link(grain_term, "", rough_delta, "A")
    rough_sum = _expr(material, unreal.MaterialExpressionAdd, 0, -160)
    _link(p["roughness"], "", rough_sum, "A")
    _link(rough_delta, "", rough_sum, "B")
    rough_sat = _expr(material, unreal.MaterialExpressionSaturate, 140, -160)
    _link(rough_sum, "", rough_sat)
    MEL.connect_material_property(rough_sat, "", unreal.MaterialProperty.MP_ROUGHNESS)


def build():
    material = _load_or_create_material(MATERIAL_NAME)

    # Runtime HISM batches need this usage flag in cooked builds.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("two_sided", False)

    try:
        _build_detailed(material)
        unreal.log("Neon Coast: M_Coast built with surface detail")
    except Exception as exc:  # keep the game shippable on API drift
        unreal.log_warning("Neon Coast: surface detail unavailable (%s); building flat M_Coast" % exc)
        MEL.delete_all_material_expressions(material)
        _build_flat(material)

    MEL.layout_material_expressions(material)
    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(MATERIAL_PATH, only_if_is_dirty=False)
    unreal.log("Neon Coast: built " + MATERIAL_PATH)
    return material


def build_sign_text():
    font = unreal.load_asset(SIGN_FONT)
    if font is None:
        unreal.log_warning("Neon Coast: %s missing; signs use the engine text material" % SIGN_FONT)
        return None

    material = _load_or_create_material(SIGN_NAME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.5)
    material.set_editor_property("two_sided", False)

    glyphs = _expr(material, unreal.MaterialExpressionFontSampleParameter, -700, 260,
                   parameter_name="Font", font=font, font_texture_page=0)
    tint = _expr(material, unreal.MaterialExpressionVertexColor, -700, -200)
    glow = _scalar(material, "Glow", 1.0, -700, 40)
    emissive = _expr(material, unreal.MaterialExpressionMultiply, -400, 0)
    _link(tint, "", emissive, "A")
    _link(glow, "", emissive, "B")
    rough = _expr(material, unreal.MaterialExpressionConstant, -400, 140, r=0.35)

    MEL.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(glyphs, "A", unreal.MaterialProperty.MP_OPACITY_MASK)

    MEL.layout_material_expressions(material)
    MEL.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(SIGN_PATH, only_if_is_dirty=False)
    unreal.log("Neon Coast: built " + SIGN_PATH)
    return material


def build_all():
    build()
    try:
        build_sign_text()
    except Exception as exc:
        unreal.log_warning("Neon Coast: sign text material skipped (%s)" % exc)
        if unreal.EditorAssetLibrary.does_asset_exist(SIGN_PATH):
            unreal.EditorAssetLibrary.delete_asset(SIGN_PATH)


if __name__ == "__main__":
    build_all()

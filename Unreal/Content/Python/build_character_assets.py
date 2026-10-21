"""Import the selected CC0 Kenney humans using Opus's recorded art direction.

Run in UnrealEditor-Cmd with -run=pythonscript -script=<this file>. No runtime
Python is required. Existing valid mesh/animation assets are reused; texture
settings are reapplied and existing material graphs are validated, never deleted.
No actor is spawned.
"""

from pathlib import Path
import hashlib
import json
import math
import unreal


PROJECT = Path(__file__).resolve().parents[2]
REPOSITORY = PROJECT.parent
SOURCE = PROJECT / "ArtSource" / "Characters"
DIRECTION_FILE = REPOSITORY / "docs" / "UE_CHARACTER_ART_DIRECTION.json"
REPORT_FILE = PROJECT / "Saved" / "CharacterImportReport.json"
ASSET_DIR = "/Game/Characters"
MESH_PATH = ASSET_DIR + "/SK_CoastHuman"
SKELETON_PATH = ASSET_DIR + "/SK_CoastHuman_Skeleton"
SKINS = {
    "criminalMaleA.png": "CriminalMaleA",
    "skaterMaleA.png": "SkaterMaleA",
    "skaterFemaleA.png": "SkaterFemaleA",
}
CLIPS = {"Idle": ("idle.fbx", 1.0666667), "Run": ("run.fbx", .6666667), "Jump": ("jump.fbx", .5)}
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
MATERIAL_DESIGN_TAG = "NeonCoast.CharacterMaterialDesignSHA256.v1"


def _path(asset):
    return asset.get_path_name().split(".", 1)[0]


def _existing(path, expected_class):
    if not EAL.does_asset_exist(path):
        return None
    asset = EAL.load_asset(path)
    if not isinstance(asset, expected_class):
        raise RuntimeError(f"{path} exists but is not a {expected_class.__name__}")
    return asset


def _save(asset):
    if not EAL.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Failed to save " + asset.get_path_name())


def _task(filename, destination, name, options=None, factory=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(filename))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    if factory is not None:
        task.set_editor_property("factory", factory)
    TOOLS.import_asset_tasks([task])
    objects = list(task.get_objects())
    if not objects:
        objects = [EAL.load_asset(path) for path in task.get_editor_property("imported_object_paths")]
    if not any(objects):
        raise RuntimeError("Importer produced no objects from " + str(filename))
    return [asset for asset in objects if asset is not None]


def _fbx_options(animation=False, skeleton=None):
    options = unreal.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("import_mesh", not animation)
    options.set_editor_property("import_animations", animation)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("create_physics_asset", False)
    options.set_editor_property("override_full_name", True)
    kind = unreal.FBXImportType.FBXIT_ANIMATION if animation else unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.set_editor_property("mesh_type_to_import", kind)
    options.set_editor_property("original_import_type", kind)
    if skeleton is not None:
        options.set_editor_property("skeleton", skeleton)
    data = options.get_editor_property("anim_sequence_import_data" if animation else "skeletal_mesh_import_data")
    data.set_editor_property("import_uniform_scale", 1.0)
    data.set_editor_property("convert_scene", True)
    data.set_editor_property("convert_scene_unit", True)
    data.set_editor_property("force_front_x_axis", False)
    if animation:
        data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property("use_default_sample_rate", False)
        data.set_editor_property("custom_sample_rate", 0)
        data.set_editor_property("snap_to_closest_frame_boundary", True)
    else:
        data.set_editor_property("use_t0_as_ref_pose", False)
        data.set_editor_property("update_skeleton_reference_pose", False)
        data.set_editor_property("import_morph_targets", False)
    return options


def _mesh_and_skeleton():
    mesh = _existing(MESH_PATH, unreal.SkeletalMesh)
    if mesh is None:
        objects = _task(SOURCE / "Model" / "characterMedium.fbx", ASSET_DIR, "SK_CoastHuman",
                        _fbx_options(), unreal.FbxFactory())
        meshes = [asset for asset in objects if isinstance(asset, unreal.SkeletalMesh)]
        if len(meshes) != 1:
            raise RuntimeError(f"Expected one skeletal mesh, received {len(meshes)}")
        mesh = meshes[0]
        if _path(mesh) != MESH_PATH:
            if not EAL.rename_asset(_path(mesh), MESH_PATH):
                raise RuntimeError("Cannot assign canonical skeletal mesh name")
            mesh = _existing(MESH_PATH, unreal.SkeletalMesh)
    skeleton = mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("Imported human has no Skeleton")
    if _path(skeleton) != SKELETON_PATH:
        if EAL.does_asset_exist(SKELETON_PATH):
            raise RuntimeError("Canonical Skeleton already exists but is not the mesh's Skeleton")
        if not EAL.rename_asset(_path(skeleton), SKELETON_PATH):
            raise RuntimeError("Cannot assign canonical Skeleton name")
        skeleton = _existing(SKELETON_PATH, unreal.Skeleton)
    _save(mesh)
    _save(skeleton)
    return mesh, skeleton


def _animation(name, filename, expected_seconds, skeleton):
    canonical = ASSET_DIR + "/A_" + name
    animation = _existing(canonical, unreal.AnimSequence)
    if animation is None:
        destination = ASSET_DIR + "/ImportSources/" + name
        EAL.make_directory(destination)
        objects = _task(SOURCE / "Animations" / filename, destination, "Import_" + name,
                        _fbx_options(animation=True, skeleton=skeleton), unreal.FbxFactory())
        # Kenney supplies a one-frame Targeting Pose before every real motion.
        # Do not rely on returned-object order or pick the first animation.
        candidates = [asset for asset in objects
                      if isinstance(asset, unreal.AnimSequence)
                      and asset.get_play_length() > .1
                      and "target" not in asset.get_name().lower()
                      and name.lower() in asset.get_name().lower()]
        if len(candidates) != 1:
            summary = [(asset.get_name(), asset.get_play_length()) for asset in objects
                       if isinstance(asset, unreal.AnimSequence)]
            raise RuntimeError(f"Cannot select unique real {name} take: {summary}")
        if not EAL.rename_asset(_path(candidates[0]), canonical):
            raise RuntimeError("Cannot name selected motion " + canonical)
        animation = _existing(canonical, unreal.AnimSequence)
    duration = animation.get_play_length()
    if not math.isfinite(duration) or duration <= .1 or abs(duration - expected_seconds) > .12:
        raise RuntimeError(f"{canonical} has unexpected motion length {duration}; expected about {expected_seconds}")
    if animation.get_editor_property("skeleton") != skeleton:
        raise RuntimeError(canonical + " does not use the human Skeleton")
    animation.set_editor_property("enable_root_motion", False)
    _save(animation)
    return animation


def _texture(filename, label, direction):
    path = ASSET_DIR + "/T_" + label
    texture = _existing(path, unreal.Texture2D)
    if texture is None:
        objects = _task(SOURCE / "Skins" / filename, ASSET_DIR, "T_" + label,
                        factory=unreal.TextureFactory())
        texture = next((asset for asset in objects if isinstance(asset, unreal.Texture2D)), None)
        if texture is None:
            raise RuntimeError("Missing imported texture " + filename)
        if _path(texture) != path:
            if not EAL.rename_asset(_path(texture), path):
                raise RuntimeError("Cannot name imported texture " + path)
            texture = _existing(path, unreal.Texture2D)
    if direction.get("texture_filter") != "bilinear":
        raise RuntimeError("Unreviewed character texture filter in art direction")
    texture.set_editor_property("srgb", bool(direction["texture_settings"]["srgb"]))
    texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    _save(texture)
    return texture


def _material_design(texture, direction):
    """Reject unsupported design changes instead of silently keeping old art."""
    expected_material = {
        "shading_model": "DefaultLit", "blend_mode": "Opaque", "two_sided": False,
        "base_color": "skin texture sample, no tint",
        "emissive": "none (neon comes from the city, not the characters)",
        "cast_shadows": True,
    }
    expected_texture = {"srgb": True, "mipmaps": True,
                        "compression": "Default (BC1/DXT1)", "lod_group": "Character"}
    if direction.get("material") != expected_material:
        raise RuntimeError("Character material design changed; update the importer and explicitly rebuild "
                           "the affected materials in a controlled editor session. Existing graphs were preserved.")
    if direction.get("texture_settings") != expected_texture or direction.get("texture_filter") != "bilinear":
        raise RuntimeError("Character texture design changed; update the importer before rebuilding materials.")
    values = {key: float(direction[key]) for key in ("roughness", "metallic", "specular")}
    if any(not math.isfinite(value) or not 0 <= value <= 1 for value in values.values()):
        raise RuntimeError("Character material scalars must be finite values between zero and one")
    return {"graph_version": 1, "texture": _path(texture), "scalars": values,
            "material": expected_material, "texture_settings": expected_texture,
            "texture_filter": "bilinear", "used_with_skeletal_mesh": True}


def _validate_material(material, texture, design):
    """Read the real connected graph, including assets made before metadata existed."""
    problems = []
    properties = {"blend_mode": unreal.BlendMode.BLEND_OPAQUE, "two_sided": False,
                  "shading_model": unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
                  "used_with_skeletal_mesh": True}
    for key, expected in properties.items():
        if material.get_editor_property(key) != expected:
            problems.append(key)
    if MEL.get_num_material_expressions(material) != 4:
        problems.append("expected exactly four generated expressions")
    sample = MEL.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    if not isinstance(sample, unreal.MaterialExpressionTextureSample):
        problems.append("base colour is not a texture sample")
    else:
        if sample.get_editor_property("texture") != texture:
            problems.append("base-colour texture")
        if sample.get_editor_property("sampler_type") != unreal.MaterialSamplerType.SAMPLERTYPE_COLOR:
            problems.append("texture sampler type")
        if MEL.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_BASE_COLOR) != "RGB":
            problems.append("base-colour output must be RGB")
    for key, output in (("roughness", unreal.MaterialProperty.MP_ROUGHNESS),
                        ("metallic", unreal.MaterialProperty.MP_METALLIC),
                        ("specular", unreal.MaterialProperty.MP_SPECULAR)):
        node = MEL.get_material_property_input_node(material, output)
        if not isinstance(node, unreal.MaterialExpressionConstant):
            problems.append(key + " is not connected to a constant")
        elif not math.isclose(float(node.get_editor_property("r")), design["scalars"][key], abs_tol=1e-6):
            problems.append(key + " differs from the recorded design")
    if MEL.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR) is not None:
        problems.append("unexpected emissive connection")
    if problems:
        raise RuntimeError(_path(material) + ": " + "; ".join(problems)
                           + ". Existing material was preserved. Explicitly rebuild the affected material "
                           "in a controlled editor session after reviewing the art direction; this importer "
                           "will not delete or unroot loaded graph nodes.")


def _material(label, texture, direction):
    path = ASSET_DIR + "/M_" + label
    design = _material_design(texture, direction)
    design_hash = hashlib.sha256(json.dumps(design, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    material = _existing(path, unreal.Material)
    if material is not None:
        recorded_hash = EAL.get_metadata_tag(material, MATERIAL_DESIGN_TAG)
        if recorded_hash and recorded_hash != design_hash:
            raise RuntimeError(path + ": recorded character material design SHA-256 differs from the current "
                               "Opus design. Explicitly rebuild this material in a controlled editor session. "
                               "The importer preserved all existing expressions.")
        _validate_material(material, texture, design)
        if not recorded_hash:
            # First safe reuse of materials imported before this metadata was added.
            EAL.set_metadata_tag(material, MATERIAL_DESIGN_TAG, design_hash)
            _save(material)
        unreal.log("NEON_COAST_CHARACTER_MATERIAL_REUSED " + path + " " + design_hash)
        return material
    material = TOOLS.create_asset("M_" + label, ASSET_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Cannot create " + path)
    # Build only brand-new assets. Runtime constructor references can root loaded
    # expressions, making DeleteAllMaterialExpressions assert inside the engine.
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("two_sided", False)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("used_with_skeletal_mesh", True)
    sample = MEL.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, -180)
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if not MEL.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("Cannot connect character texture to base colour")
    for index, (key, output) in enumerate((("roughness", unreal.MaterialProperty.MP_ROUGHNESS),
                                         ("metallic", unreal.MaterialProperty.MP_METALLIC),
                                         ("specular", unreal.MaterialProperty.MP_SPECULAR))):
        value = design["scalars"][key]
        node = MEL.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 80 + index * 100)
        node.set_editor_property("r", value)
        if not MEL.connect_material_property(node, "", output):
            raise RuntimeError("Cannot connect character material " + key)
    MEL.recompile_material(material)
    _validate_material(material, texture, design)
    EAL.set_metadata_tag(material, MATERIAL_DESIGN_TAG, design_hash)
    _save(material)
    return material


def _report(mesh, skeleton, animations, textures, materials, direction, source_hashes):
    bounds = mesh.get_imported_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    height = float(extent.z * 2)
    if not math.isfinite(height) or not 1 < height < 10000:
        raise RuntimeError(f"Implausible skeletal mesh height {height} cm")
    # Transient and unregistered: inspect the imported skeleton without adding a level actor.
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    bones = [str(component.get_bone_name(index)) for index in range(component.get_num_bones())]
    if len(bones) < 20 or "Hips" not in bones or "Head" not in bones:
        raise RuntimeError("Imported human Skeleton is incomplete: " + repr(bones))
    vector = lambda value: [float(value.x), float(value.y), float(value.z)]
    return {
        "mesh": _path(mesh), "skeleton": _path(skeleton),
        "bounds_cm": {"origin": vector(origin), "box_extent": vector(extent),
                      "min": vector(origin - extent), "max": vector(origin + extent), "height": height},
        "bone_count": len(bones), "bone_names": bones,
        "target_height_cm": float(direction["height_cm"]),
        "recommended_uniform_scale": float(direction["height_cm"]) / height,
        "suggested_mesh_yaw_degrees": float(direction["mesh_yaw_degrees"]),
        "orientation_validated_in_game": False,
        "animations": {name: {"path": _path(asset), "seconds": asset.get_play_length()} for name, asset in animations.items()},
        "textures": {name: _path(asset) for name, asset in textures.items()},
        "materials": {name: _path(asset) for name, asset in materials.items()},
        "art_direction": str(DIRECTION_FILE), "art_direction_model": direction["model"],
        "art_direction_sha256": hashlib.sha256(DIRECTION_FILE.read_bytes()).hexdigest(),
        "material_design_sha256": {name: EAL.get_metadata_tag(asset, MATERIAL_DESIGN_TAG)
                                   for name, asset in materials.items()},
        "source_sha256": source_hashes,
        "license": "CC0-1.0", "source_page": "https://kenney.nl/assets/animated-characters-protagonists",
        "limitations": ["No authored walking clip: runtime may play Run more slowly for pedestrians.",
                        "No authored aim/reload/death clip; these require a separate animation solution."],
    }


def build():
    required = [SOURCE / "License.txt", SOURCE / "Model" / "characterMedium.fbx"]
    required += [SOURCE / "Animations" / data[0] for data in CLIPS.values()]
    required += [SOURCE / "Skins" / filename for filename in SKINS]
    for filename in required + [DIRECTION_FILE]:
        if not filename.is_file() or filename.stat().st_size == 0:
            raise RuntimeError("Missing required character source: " + str(filename))
    direction = json.loads(DIRECTION_FILE.read_text(encoding="utf-8-sig"))
    if direction.get("hero_skin") != "criminalMaleA.png":
        raise RuntimeError("Hero skin no longer matches the agreed character asset contract")
    if {entry["texture"] for entry in direction["npc_skins"]} != {"skaterMaleA.png", "skaterFemaleA.png"}:
        raise RuntimeError("NPC skin selection no longer matches the reviewed source inventory")
    if "CC0" not in (SOURCE / "License.txt").read_text(encoding="utf-8-sig"):
        raise RuntimeError("The source archive's CC0 license notice is missing")
    source_hashes = {str(path.relative_to(SOURCE)).replace("\\", "/"): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in required}
    if REPORT_FILE.is_file():
        previous = json.loads(REPORT_FILE.read_text(encoding="utf-8"))
        old_hashes = previous.get("source_sha256", {})
        changed_fbx = [name for name, value in source_hashes.items()
                       if name.endswith(".fbx") and name in old_hashes and old_hashes[name] != value]
        if changed_fbx:
            raise RuntimeError("Source FBX changed; explicitly reimport the dependent Skeleton/animations before rebuilding: " + repr(changed_fbx))
    EAL.make_directory(ASSET_DIR)
    cvar = "Interchange.FeatureFlags.Import.FBX"
    previous_fbx_flag = unreal.SystemLibrary.get_console_variable_int_value(cvar)
    unreal.SystemLibrary.execute_console_command(None, cvar + " 0")
    try:
        mesh, skeleton = _mesh_and_skeleton()
        animations = {name: _animation(name, filename, seconds, skeleton)
                      for name, (filename, seconds) in CLIPS.items()}
        textures = {label: _texture(filename, label, direction) for filename, label in SKINS.items()}
        materials = {label: _material(label, texture, direction) for label, texture in textures.items()}
        slots = list(mesh.get_editor_property("materials"))
        if not slots:
            raise RuntimeError("Imported character has no material slot")
        for slot in slots:
            slot.set_editor_property("material_interface", materials["CriminalMaleA"])
        mesh.set_editor_property("materials", slots)
        _save(mesh)
        report = _report(mesh, skeleton, animations, textures, materials, direction, source_hashes)
        for filename in (REPORT_FILE, REPOSITORY / "artifacts" / "ue-character-assets.json"):
            filename.parent.mkdir(parents=True, exist_ok=True)
            filename.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        unreal.log("NEON_COAST_CHARACTERS_READY " + json.dumps({"height_cm": report["bounds_cm"]["height"],
                   "recommended_scale": report["recommended_uniform_scale"], "bone_count": report["bone_count"]}))
        return report
    finally:
        unreal.SystemLibrary.execute_console_command(None, cvar + " " + str(previous_fbx_flag))


if __name__ == "__main__":
    build()

"""Import local licensed Renderpeople humans and retarget Epic template animation.

Run once outside UE with --prepare-template-only, then run this file through the
UE 5.8 Python commandlet. No actor or runtime visual code is changed. Original
Kenney assets are preserved. Renderpeople sources and resulting assets must not
be redistributed as a standalone asset library. Material art direction is a
separate Opus-reviewed step; this script imports the original texture maps only.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import math
import re
import shutil
import sys
import traceback

PROJECT = Path(__file__).resolve().parents[2]
REPOSITORY = PROJECT.parent
ENGINE = Path(r"D:\Work_software\UnrealEngine\UE_5.8")
SOURCE_ROOT = REPOSITORY / "artifacts/realism/renderpeople"
REPORT_FILE = PROJECT / "Saved/RealisticCharacterImportReport.json"
TEMPLATE_MANIFEST = SOURCE_ROOT / "mannequin-source-manifest.json"
ASSET_ROOT = "/Game/RealisticCharacters"
TEMPLATE_ROOT = "/Game/Characters/Mannequins"
SOURCE_MESH = TEMPLATE_ROOT + "/Meshes/SKM_Manny_Simple"
SOURCE_CLIPS = {
    "Idle": "Anims/Unarmed/MM_Idle",
    "Walk": "Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd",
    "Run": "Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd",
    "Jump": "Anims/Unarmed/Jump/MM_Jump",
}
CHARACTERS = ("Eric", "Carla")
TAG = "NeonCoast.RealisticImportSHA256.v1"
PIPELINE_VERSION = "ue58-manny-to-renderpeople-ue4-v1"
CORE_BONES = {"root", "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head"}
for _side in ("l", "r"):
    CORE_BONES.update(f"{bone}_{_side}" for bone in
                      ("clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball"))


def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def prepare_template_sources():
    """Copy just the recursively referenced official assets, preserving paths."""
    source = ENGINE / "Templates/TemplateResources/High/Characters/Content/Mannequins"
    lookup = {TEMPLATE_ROOT + "/" + f.relative_to(source).with_suffix("").as_posix(): f
              for f in source.rglob("*.uasset")}
    queue = [SOURCE_MESH] + [TEMPLATE_ROOT + "/" + value for value in SOURCE_CLIPS.values()]
    closure = set()
    while queue:
        name = queue.pop()
        if name in closure:
            continue
        closure.add(name)
        if name not in lookup:
            raise RuntimeError("Missing official template dependency: " + name)
        data = lookup[name].read_bytes()
        refs = set(value.decode("ascii") for value in
                   re.findall(rb"/Game/Characters/Mannequins/[A-Za-z0-9_/-]+", data))
        queue.extend(refs - closure)
    files = []
    for name in sorted(closure):
        source_file = lookup[name]
        destination = PROJECT / "Content" / (name.removeprefix("/Game/") + ".uasset")
        digest = _sha(source_file)
        if destination.exists():
            if _sha(destination) != digest:
                raise RuntimeError("Existing template asset differs; refusing overwrite: " + str(destination))
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, destination)
        files.append({"asset": name, "source": str(source_file), "destination": str(destination),
                      "bytes": destination.stat().st_size, "sha256": digest})
    result = {"source": str(source), "license": "Epic Unreal Engine template content; UE project use",
              "copied_dependency_closure": True, "asset_count": len(files),
              "bytes": sum(f["bytes"] for f in files), "files": files}
    _write_json(TEMPLATE_MANIFEST, result)
    return result


def _inspect_source(filename):
    script = REPOSITORY / "tools/inspect-realistic-character.py"
    spec = importlib.util.spec_from_file_location("neon_fbx_inspector", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.inspect(filename)
    bones = {bone["name"]: bone for bone in result["bones"]}
    if not CORE_BONES.issubset(bones):
        raise RuntimeError("Source does not have the inspected UE4 core skeleton: " + str(filename))
    expected = {"pelvis": "root", "spine_01": "pelvis", "spine_02": "spine_01",
                "spine_03": "spine_02", "neck_01": "spine_03", "head": "neck_01"}
    for side in ("l", "r"):
        expected.update({f"thigh_{side}": "pelvis", f"calf_{side}": f"thigh_{side}",
                         f"foot_{side}": f"calf_{side}", f"hand_{side}": f"lowerarm_{side}",
                         f"lowerarm_{side}": f"upperarm_{side}"})
    for name, parent in expected.items():
        if bones[name]["parent"] != parent:
            raise RuntimeError(f"Unexpected source hierarchy: {name} -> {bones[name]['parent']}")
    return result


def _path(asset):
    return asset.get_path_name().split(".", 1)[0]


def _load(path, cls):
    if not EAL.does_asset_exist(path):
        return None
    result = EAL.load_asset(path)
    if not isinstance(result, cls):
        raise RuntimeError(f"Expected {cls.__name__}: {path}")
    return result


def _save(asset):
    if not EAL.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Failed to save " + asset.get_path_name())


def _tag(asset, digest):
    EAL.set_metadata_tag(asset, TAG, digest)
    _save(asset)


def _assert_tag(asset, digest):
    old = EAL.get_metadata_tag(asset, TAG)
    if old != digest:
        raise RuntimeError(f"{_path(asset)} source/pipeline fingerprint changed or is untracked. "
                           "Review and rebuild in a new asset directory; assets were not overwritten.")


def _import(filename, directory, name, options=None, factory=None):
    task = unreal.AssetImportTask()
    for key, value in {"filename": str(filename), "destination_path": directory,
                       "destination_name": name, "automated": True,
                       "replace_existing": False, "save": True}.items():
        task.set_editor_property(key, value)
    if options is not None:
        task.set_editor_property("options", options)
    if factory is not None:
        task.set_editor_property("factory", factory)
    TOOLS.import_asset_tasks([task])
    objects = [asset for asset in task.get_objects() if asset is not None]
    if not objects:
        objects = [EAL.load_asset(path) for path in task.get_editor_property("imported_object_paths")]
    if not any(objects):
        raise RuntimeError("Import produced no asset: " + str(filename))
    return objects


def _mesh(filename, name, digest):
    directory = ASSET_ROOT + "/" + name
    path = directory + "/SK_" + name
    mesh = _load(path, unreal.SkeletalMesh)
    if mesh is not None:
        _assert_tag(mesh, digest)
        return mesh
    options = unreal.FbxImportUI()
    for key, value in {"automated_import_should_detect_type": False, "import_as_skeletal": True,
                       "import_mesh": True, "import_animations": False, "import_materials": False,
                       "import_textures": False, "create_physics_asset": False,
                       "override_full_name": True,
                       "mesh_type_to_import": unreal.FBXImportType.FBXIT_SKELETAL_MESH,
                       "original_import_type": unreal.FBXImportType.FBXIT_SKELETAL_MESH}.items():
        options.set_editor_property(key, value)
    data = options.get_editor_property("skeletal_mesh_import_data")
    for key, value in {"import_uniform_scale": 1.0, "convert_scene": True,
                       "convert_scene_unit": True, "force_front_x_axis": False,
                       "use_t0_as_ref_pose": False, "update_skeleton_reference_pose": False,
                       "import_morph_targets": False}.items():
        data.set_editor_property(key, value)
    meshes = [a for a in _import(filename, directory, "SK_" + name, options, unreal.FbxFactory())
              if isinstance(a, unreal.SkeletalMesh)]
    if len(meshes) != 1 or _path(meshes[0]) != path:
        raise RuntimeError("Expected one skeletal mesh with canonical path " + path)
    mesh = meshes[0]
    _tag(mesh, digest)
    skeleton = mesh.get_editor_property("skeleton")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("Imported mesh has no Skeleton")
    _save(skeleton)
    return mesh


def _mesh_report(mesh):
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    bones = [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
    if not CORE_BONES.issubset(bones):
        raise RuntimeError("Imported mesh lost required bones: " + _path(mesh))
    bounds = mesh.get_imported_bounds()
    vector = lambda v: [float(v.x), float(v.y), float(v.z)]
    height = bounds.box_extent.z * 2
    if not math.isfinite(height) or not 130 < height < 230:
        raise RuntimeError(f"Unexpected realistic human height/orientation {height} cm: {_path(mesh)}")
    return {"mesh": _path(mesh), "skeleton": _path(mesh.get_editor_property("skeleton")),
            "bone_count": len(bones), "bones": bones,
            "bone_parents": {bone: str(component.get_parent_bone(bone)) for bone in bones},
            "bounds_cm": {"origin": vector(bounds.origin), "extent": vector(bounds.box_extent),
                          "min": vector(bounds.origin - bounds.box_extent),
                          "max": vector(bounds.origin + bounds.box_extent), "height": float(height)},
            "material_slots": [str(slot.get_editor_property("material_slot_name"))
                               for slot in mesh.get_editor_property("materials")]}


def _rig(mesh, path, digest):
    rig = _load(path, unreal.IKRigDefinition)
    if rig is None:
        directory, name = path.rsplit("/", 1)
        rig = TOOLS.create_asset(name, directory, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
        controller = unreal.IKRigController.get_controller(rig)
        if not controller.set_skeletal_mesh(mesh):
            raise RuntimeError("IK Rig rejected mesh " + _path(mesh))
        if not controller.apply_auto_generated_retarget_definition():
            raise RuntimeError("UE did not recognize the inspected mannequin skeleton: " + _path(mesh))
        if not controller.apply_auto_fbik():
            raise RuntimeError("UE could not generate full-body IK: " + _path(mesh))
        _tag(rig, digest)
    else:
        _assert_tag(rig, digest)
        controller = unreal.IKRigController.get_controller(rig)
        if controller.get_skeletal_mesh() != mesh:
            raise RuntimeError("IK Rig references a different mesh: " + path)
    chains = {}
    for chain in controller.get_retarget_chains():
        name = str(chain.get_editor_property("chain_name"))
        chains[name] = {"start": str(controller.get_retarget_chain_start_bone(name)),
                        "end": str(controller.get_retarget_chain_end_bone(name)),
                        "goal": str(chain.get_editor_property("ik_goal_name"))}
    required = {"Spine", "LeftArm", "RightArm", "LeftLeg", "RightLeg", "Head"}
    if not required.issubset(chains):
        raise RuntimeError("Auto-generated IK rig lacks core chains: " + repr(chains))
    return rig, {"asset": path, "pelvis": str(controller.get_retarget_root()), "chains": chains}


def _retargeter(source_rig, target_rig, source_mesh, target_mesh, name, digest, chains):
    path = ASSET_ROOT + "/" + name + "/RTG_Manny_" + name
    asset = _load(path, unreal.IKRetargeter)
    if asset is None:
        directory, basename = path.rsplit("/", 1)
        asset = TOOLS.create_asset(basename, directory, unreal.IKRetargeter, unreal.IKRetargetFactory())
        controller = unreal.IKRetargeterController.get_controller(asset)
        controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
        controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
        controller.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, source_mesh)
        controller.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target_mesh)
        controller.add_default_ops()
        controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
        controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_rig)
        controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
        controller.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET,
                                        unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
        _tag(asset, digest)
    else:
        _assert_tag(asset, digest)
        controller = unreal.IKRetargeterController.get_controller(asset)
    mapping = {chain: str(controller.get_source_chain(chain)) for chain in chains}
    for chain in ("Spine", "Head", "LeftArm", "RightArm", "LeftLeg", "RightLeg"):
        if mapping.get(chain) != chain:
            raise RuntimeError(f"Retarget core chain not mapped: {chain}: {mapping.get(chain)}")
    return asset, {"asset": path, "chain_mapping": mapping,
                   "ops": [str(controller.get_op_name(i)) for i in range(controller.get_num_retarget_ops())]}


def _pose_report(animation, mesh, clip):
    seconds = float(animation.get_play_length())
    if not math.isfinite(seconds) or seconds <= .1:
        raise RuntimeError("Empty retargeted animation: " + _path(animation))
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    # The editor API defaults to ignoring root lock. Explicitly evaluate the
    # in-place playback policy used by AnimationSingleNode + CharacterMovement.
    options.set_editor_property("incorporate_root_motion_into_pose", False)
    options.set_editor_property("extract_root_motion", False)
    points = []
    for step in range(21):
        fraction = step / 20.0
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, seconds * fraction, options)
        if not unreal.AnimPoseExtensions.is_valid(pose):
            raise RuntimeError("Retargeted animation does not evaluate: " + _path(animation))
        sample = {}
        for bone in ("root", "pelvis", "head", "hand_l", "hand_r", "foot_l", "foot_r"):
            transform = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
            translation = transform.translation
            coords = [float(translation.x), float(translation.y), float(translation.z)]
            if not all(math.isfinite(value) for value in coords):
                raise RuntimeError("Animation evaluated a non-finite bone transform")
            sample[bone] = coords
        points.append(sample)
    root_displacement = max(math.dist(points[0]["root"], point["root"]) for point in points)
    if root_displacement > .05:
        raise RuntimeError(f"{clip} root still moves during in-place playback: {root_displacement} cm")
    relative = [{bone: [value[i] - point["root"][i] for i in range(3)]
                 for bone, value in point.items() if bone != "root"} for point in points]
    displacement = max(math.dist(points[0][bone], point[bone])
                       for point in points[1:] for bone in points[0])
    relative_displacement = max(math.dist(relative[0][bone], point[bone])
                                for point in relative[1:] for bone in relative[0])
    if clip in ("Walk", "Run", "Jump") and relative_displacement < .25:
        raise RuntimeError(f"{clip} is effectively static relative to root ({relative_displacement} cm)")
    # Retain the authored root track only as stride-speed evidence, never as
    # gameplay translation. Runtime derives playback rate from this same track.
    options.set_editor_property("incorporate_root_motion_into_pose", True)
    raw_root = []
    for time in (0.0, seconds):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, time, options)
        value = unreal.AnimPoseExtensions.get_bone_pose(pose, "root", unreal.AnimPoseSpaces.WORLD).translation
        raw_root.append([float(value.x), float(value.y), float(value.z)])
    authored_speed = math.dist(raw_root[0][:2], raw_root[1][:2]) / seconds
    return {"path": _path(animation), "seconds": seconds,
            "root_motion_enabled": bool(animation.get_editor_property("enable_root_motion")),
            "force_root_lock": bool(animation.get_editor_property("force_root_lock")),
            "root_lock": str(animation.get_editor_property("root_motion_root_lock")),
            "root_translation_range_cm": root_displacement,
            "authored_root_speed_cm_s": authored_speed,
            "sampled_max_bone_displacement_cm": displacement,
            "sampled_max_root_relative_bone_displacement_cm": relative_displacement,
            "sampled_component_space_poses": points}


def _animations(name, mesh, source_mesh, retargeter, sources, digest):
    directory = ASSET_ROOT + "/" + name + "/Animations"
    result = {}
    for clip, source in sources.items():
        basename = f"A_{name}_{clip}"
        path = directory + "/" + basename
        asset = _load(path, unreal.AnimSequence)
        if asset is None:
            inputs = unreal.IKRetargetBatchOperationInputs()
            for key, value in {"assets_to_retarget": [EAL.find_asset_data(_path(source))],
                               "source_mesh": source_mesh, "target_mesh": mesh,
                               "ik_retarget_asset": retargeter, "search": source.get_name(),
                               "replace": basename, "target_path": directory,
                               "use_source_path": False, "include_referenced_assets": False,
                               "overwrite_existing_files": False, "retain_additive_flags": True}.items():
                inputs.set_editor_property(key, value)
            unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
            asset = _load(path, unreal.AnimSequence)
            if asset is None:
                raise RuntimeError("Retarget batch did not produce " + path)
            _tag(asset, digest)
        else:
            _assert_tag(asset, digest)
        if asset.get_editor_property("skeleton") != mesh.get_editor_property("skeleton"):
            raise RuntimeError("Retargeted clip has the wrong skeleton: " + path)
        if abs(float(source.get_play_length()) - float(asset.get_play_length())) > .05:
            raise RuntimeError("Retargeting changed clip duration: " + path)
        # Safe in-place update for new and already-generated clips. Do not
        # reimport meshes, delete rooted objects or discard the source root track.
        asset.set_editor_property("enable_root_motion", False)
        asset.set_editor_property("force_root_lock", True)
        asset.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
        EAL.set_metadata_tag(asset, "NeonCoast.PlaybackPolicy.v1", "in-place-force-first-frame")
        _save(asset)
        result[clip] = _pose_report(asset, mesh, clip)
    return result


def _textures(name):
    """Color/data map interpretation only; no invented material art direction."""
    result = {}
    names = {"dif": "Diffuse", "norm": "Normal", "gloss": "Gloss", "mask01": "Mask01", "mask02": "Mask02"}
    for suffix, label in names.items():
        filename = SOURCE_ROOT / name.lower() / "tex" / f"rp_{name.lower()}_rigged_001_{suffix}.jpg"
        digest = _sha(filename)
        path = ASSET_ROOT + "/" + name + "/Textures/T_" + name + "_" + label
        texture = _load(path, unreal.Texture2D)
        if texture is None:
            directory, basename = path.rsplit("/", 1)
            textures = [a for a in _import(filename, directory, basename, factory=unreal.TextureFactory())
                        if isinstance(a, unreal.Texture2D)]
            if len(textures) != 1 or _path(textures[0]) != path:
                raise RuntimeError("Unexpected texture import result: " + str(filename))
            texture = textures[0]
            texture.set_editor_property("srgb", suffix == "dif")
            if suffix == "norm":
                texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            _tag(texture, digest)
        else:
            _assert_tag(texture, digest)
        result[suffix] = {"asset": path, "source": str(filename), "sha256": digest}
    return result


def build():
    global unreal, EAL, TOOLS
    import unreal
    EAL = unreal.EditorAssetLibrary
    TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
    required = ("IKRigController", "IKRigDefinitionFactory", "IKRetargeterController",
                "IKRetargetBatchOperationInputs", "AnimPoseExtensions")
    if any(not hasattr(unreal, name) for name in required):
        raise RuntimeError("UE 5.8 editor IKRig/Python APIs are unavailable; enable the editor plugins first")
    # Copying while UE is running would require asset-registry discovery; require
    # the explicit offline preparation step before opening this commandlet.
    if not TEMPLATE_MANIFEST.is_file():
        raise RuntimeError("First run this script with system Python --prepare-template-only while UE is closed")
    manifest = json.loads(TEMPLATE_MANIFEST.read_text(encoding="utf-8"))
    for entry in manifest["files"]:
        if _sha(Path(entry["destination"])) != entry["sha256"]:
            raise RuntimeError("Template source changed: " + entry["destination"])
    source_mesh = _load(SOURCE_MESH, unreal.SkeletalMesh)
    sources = {clip: _load(TEMPLATE_ROOT + "/" + path, unreal.AnimSequence)
               for clip, path in SOURCE_CLIPS.items()}
    if source_mesh is None or any(asset is None for asset in sources.values()):
        raise RuntimeError("Official template mesh or animation is missing")
    source_digest = hashlib.sha256((PIPELINE_VERSION + json.dumps(manifest, sort_keys=True)).encode()).hexdigest()
    report = {"status": "in_progress", "pipeline": PIPELINE_VERSION, "characters": {},
              "source_mesh": _path(source_mesh), "source_manifest": str(TEMPLATE_MANIFEST),
              "material_art_direction_pending": True, "runtime_visuals_changed": False,
              "visual_inspection_pending": True}
    _write_json(REPORT_FILE, report)
    try:
        source_rig, source_rig_report = _rig(source_mesh, ASSET_ROOT + "/Retarget/IK_Manny", source_digest)
        report["source_rig"] = source_rig_report
        for name in CHARACTERS:
            source_file = SOURCE_ROOT / name.lower() / f"rp_{name.lower()}_rigged_001_ue4.fbx"
            inspected = _inspect_source(source_file)
            _write_json(SOURCE_ROOT / (name.lower() + "-fbx-inspection.json"), inspected)
            digest = hashlib.sha256((source_digest + inspected["sha256"]).encode()).hexdigest()
            mesh = _mesh(source_file, name, digest)
            item = _mesh_report(mesh)
            report["characters"][name] = item
            item["source"] = {"file": str(source_file), "sha256": inspected["sha256"],
                              "source_materials": inspected["materials"]}
            rig, rig_report = _rig(mesh, ASSET_ROOT + "/" + name + "/IK_" + name, digest)
            item["rig"] = rig_report
            retargeter, retarget_report = _retargeter(source_rig, rig, source_mesh, mesh, name,
                                                     digest, rig_report["chains"])
            item["retargeter"] = retarget_report
            item["animations"] = _animations(name, mesh, source_mesh, retargeter, sources, digest)
            item["textures"] = _textures(name)
            _write_json(REPORT_FILE, report)
        report["status"] = "import_and_retarget_passed"
        _write_json(REPORT_FILE, report)
        unreal.log("NEON_COAST_REALISTIC_CHARACTERS_READY " + str(REPORT_FILE))
    except Exception:
        report["status"] = "failed"
        report["error"] = traceback.format_exc()
        _write_json(REPORT_FILE, report)
        raise


if __name__ == "__main__":
    if "--prepare-template-only" in sys.argv:
        prepared = prepare_template_sources()
        print(json.dumps({"asset_count": prepared["asset_count"], "bytes": prepared["bytes"],
                          "manifest": str(TEMPLATE_MANIFEST)}))
    else:
        build()

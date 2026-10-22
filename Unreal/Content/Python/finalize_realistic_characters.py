"""Assign the reviewed atlas materials and finalize local scan assets.

Run build() after build_realistic_characters and build_scan_materials. Only the
single existing whole-body material slot is assigned; slot identities, ordering,
the imported base mesh, skeleton, source UVs and animation tracks are preserved.
UE's public skeletal LOD generator adds two 50%-per-level reductions when the
editor supports it. No deprecated/private source-model arrays are edited.
"""
from pathlib import Path
import json
import math
import traceback
import unreal

PROJECT = Path(__file__).resolve().parents[2]
REPORT_FILE = PROJECT / "Saved/RealisticCharacterFinalizeReport.json"
ROOT = "/Game/RealisticCharacters"
EAL = unreal.EditorAssetLibrary
LOD_TAG = "NeonCoast.ScanLODs.v1"


def _write(report):
    REPORT_FILE.parent.mkdir(parents=True, exist_ok=True)
    REPORT_FILE.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _path(asset):
    return asset.get_path_name().split(".", 1)[0]


def _load(path, cls):
    asset = EAL.load_asset(path)
    if not isinstance(asset, cls):
        raise RuntimeError(f"Missing {cls.__name__}: {path}")
    return asset


def _save(asset):
    if not EAL.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Failed to save " + _path(asset))


def _slots(mesh):
    return [{"slot": str(slot.get_editor_property("material_slot_name")),
             "imported_slot": str(slot.get_editor_property("imported_material_slot_name"))}
            for slot in mesh.get_editor_property("materials")]


def _material(mesh, person):
    material = _load(f"{ROOT}/Materials/M_{person}", unreal.Material)
    before = _slots(mesh)
    if len(before) != 1:
        raise RuntimeError(f"{person}: expected the verified single whole-body atlas slot, got {before}; "
                           "refusing to collapse or overwrite an unknown material layout")
    slots = list(mesh.get_editor_property("materials"))
    changed = slots[0].get_editor_property("material_interface") != material
    if changed:
        slots[0].set_editor_property("material_interface", material)
        mesh.set_editor_property("materials", slots)
    if _slots(mesh) != before:
        raise RuntimeError("Whole-body slot identity changed during material assignment")
    if mesh.get_editor_property("materials")[0].get_editor_property("material_interface") != material:
        raise RuntimeError("Atlas material assignment did not persist")
    return {"material": _path(material), "changed": changed, "slot_identities": before,
            "slot_count": len(before), "uvs_modified": False}


def _locks(mesh, person):
    result = {}
    for clip in ("Idle", "Walk", "Run", "Jump"):
        animation = _load(f"{ROOT}/{person}/Animations/A_{person}_{clip}", unreal.AnimSequence)
        if animation.get_editor_property("skeleton") != mesh.get_editor_property("skeleton"):
            raise RuntimeError(f"{person} {clip}: skeleton mismatch")
        if (animation.get_editor_property("enable_root_motion") or
                not animation.get_editor_property("force_root_lock") or
                animation.get_editor_property("root_motion_root_lock") != unreal.RootMotionRootLock.ANIM_FIRST_FRAME):
            raise RuntimeError(f"{person} {clip}: in-place playback policy missing; rerun character importer")
        options = unreal.AnimPoseEvaluationOptions()
        options.set_editor_property("optional_skeletal_mesh", mesh)
        options.set_editor_property("incorporate_root_motion_into_pose", False)
        options.set_editor_property("extract_root_motion", False)
        samples = []
        for step in range(21):
            time = animation.get_play_length() * step / 20.0
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, time, options)
            if not unreal.AnimPoseExtensions.is_valid(pose):
                raise RuntimeError(f"{person} {clip}: invalid evaluated pose")
            points = {}
            for bone in ("root", "foot_l", "foot_r"):
                v = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
                points[bone] = [float(v.x), float(v.y), float(v.z)]
                if not all(math.isfinite(value) for value in points[bone]):
                    raise RuntimeError("Non-finite animation transform")
            samples.append(points)
        root_range = max(math.dist(samples[0]["root"], sample["root"]) for sample in samples)
        relative = [{bone: [point[bone][i] - point["root"][i] for i in range(3)]
                     for bone in ("foot_l", "foot_r")} for point in samples]
        foot_range = max(math.dist(relative[0][bone], sample[bone])
                         for sample in relative for bone in ("foot_l", "foot_r"))
        if root_range > .05 or (clip != "Idle" and foot_range < .25):
            raise RuntimeError(f"{person} {clip}: root drift={root_range}, relative foot motion={foot_range}")
        result[clip] = {"asset": _path(animation), "seconds": animation.get_play_length(),
                        "root_translation_range_cm": root_range, "relative_foot_motion_cm": foot_range,
                        "force_root_lock": True, "enable_root_motion": False}
    return result


def _lods(mesh):
    if not hasattr(unreal, "SkeletalMeshEditorSubsystem"):
        return {"status": "not_generated", "reason": "Public SkeletalMeshEditorSubsystem is unavailable"}
    editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    access = "editor_subsystem"
    if editor is None:
        # Verified UE 5.8 source: this class has an empty constructor, no
        # Initialize/Deinitialize override, and the operations below depend on
        # editor globals and their mesh argument, not subsystem-owned state.
        try:
            editor = unreal.new_object(unreal.SkeletalMeshEditorSubsystem)
        except Exception as error:
            reason = "Commandlet skeletal editor instance unavailable: " + str(error)
            unreal.log_warning("NEON_COAST_CHARACTER_LOD_NOT_GENERATED " + reason)
            return {"status": "not_generated", "reason": reason}
        access = "transient_commandlet_instance"
    if editor is None or unreal.get_editor_subsystem(unreal.AssetEditorSubsystem) is None:
        reason = "Skeletal reduction requires an initialized editor commandlet context"
        unreal.log_warning("NEON_COAST_CHARACTER_LOD_NOT_GENERATED " + reason)
        return {"status": "not_generated", "reason": reason}
    unreal.log("NEON_COAST_SKELETAL_MESH_ACCESS " + access)
    before_count = editor.get_lod_count(mesh)
    if before_count < 1:
        raise RuntimeError("Skeletal mesh has no base LOD: " + _path(mesh))
    base_vertices = editor.get_num_verts(mesh, 0)
    base_slots = _slots(mesh)
    if before_count < 3:
        # UE 5.8 AddSourceModel/CopyAutogeneratedLODInfoFromPreviousLOD derives
        # 50% and 25% triangle budgets from LOD0. The imported LOD0 is not reduced.
        if not editor.regenerate_lod(mesh, 3, False, False):
            raise RuntimeError("Public skeletal mesh reduction failed: " + _path(mesh))
        _save(mesh)
    count = editor.get_lod_count(mesh)
    vertices = [editor.get_num_verts(mesh, index) for index in range(count)]
    sections = [editor.get_num_sections(mesh, index) for index in range(count)]
    if count < 3 or base_vertices <= 0 or vertices[0] != base_vertices:
        raise RuntimeError(f"Skeletal LOD generation failed or changed base vertices: {vertices}")
    if not (0 < vertices[2] < vertices[1] < vertices[0]):
        raise RuntimeError("Generated LODs did not reduce vertices: " + repr(vertices))
    if _slots(mesh) != base_slots or any(section != 1 for section in sections):
        raise RuntimeError("Skeletal LOD generation changed the verified atlas section layout")
    EAL.set_metadata_tag(mesh, LOD_TAG, "UE58-default-half-quarter-base-preserved")
    return {"status": "generated" if before_count < 3 else "reused", "count": count, "editor_access": access,
            "base_vertices_unchanged": True, "vertices_per_lod": vertices,
            "sections_per_lod": sections,
            "triangle_budget_fractions": [1.0, .5, .25],
            "screen_sizes": "UE automatic per-level defaults; exact .3/.12 thresholds not set through private arrays",
            "vertex_budget_note": "Rendered vertices include UV/normal splits and need not equal the triangle fractions"}


def _texture_budget(person):
    result = {}
    for label in ("Diffuse", "Normal", "Gloss", "Mask01", "Mask02"):
        texture = _load(f"{ROOT}/{person}/Textures/T_{person}_{label}", unreal.Texture2D)
        maximum = texture.get_editor_property("max_texture_size")
        streaming = not texture.get_editor_property("never_stream")
        if maximum <= 0 or maximum > 2048 or not streaming:
            raise RuntimeError(f"{person} {label}: Opus texture budget has not been applied")
        result[label] = {"asset": _path(texture), "max_texture_size": maximum, "streamed": streaming}
    return result


def build():
    report = {"status": "in_progress", "characters": {}, "runtime_visual_inspection_pending": True}
    _write(report)
    try:
        for person in ("Eric", "Carla"):
            mesh = _load(f"{ROOT}/{person}/SK_{person}", unreal.SkeletalMesh)
            item = {"mesh": _path(mesh), "material": _material(mesh, person)}
            report["characters"][person] = item
            item["animations"] = _locks(mesh, person)
            item["texture_budget"] = _texture_budget(person)
            item["lods"] = _lods(mesh)
            _save(mesh)
            _write(report)
        report["lod_generation_complete"] = all(item["lods"]["status"] in ("generated", "reused")
                                                 for item in report["characters"].values())
        report["status"] = "passed"
        _write(report)
        unreal.log("NEON_COAST_REALISTIC_CHARACTERS_FINALIZED " + str(REPORT_FILE))
        return report
    except Exception:
        report["status"] = "failed"
        report["error"] = traceback.format_exc()
        _write(report)
        raise


if __name__ == "__main__":
    build()

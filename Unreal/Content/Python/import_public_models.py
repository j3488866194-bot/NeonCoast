"""Import prepared CC0 GLBs through UE 5.8 Interchange, preserving source PBR.

Called by the parent editor preparation pass: import import_public_models; build().
Does not place actors or design scene materials. Actual imported paths and bounds
are written to Saved/PublicModelImportReport.json; exceptions leave failed status.
"""
import datetime
import hashlib
import json
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "Unreal/ArtSource/PublicAssets"
REPORT = ROOT / "Unreal/Saved/PublicModelImportReport.json"
DESTINATION = "/Game/PublicAssets/Models"
TAG = "NeonCoast.PublicModelSHA256"


def placement_socket(mesh, entry, digest):
    if entry["name"] != "SM_CoastalTree":
        return None
    # Independent GLB geometry audit: trunk material, vertices in the lowest
    # 15 cm; XY bounds centre. The same anchor results with 2 and 5 cm slices.
    expected = "93aa420b12620d6514785de9c692205a5e78cacdb17b8dbd3215352496f81a85"
    if digest != expected:
        raise RuntimeError("Tree geometry changed: remeasure the trunk foot before applying its placement socket")
    anchor = [-7.8768193722, -87.9041075706, 0.0]
    socket = mesh.find_socket("NCPlacementBase")
    if socket is None:
        socket = unreal.new_object(unreal.StaticMeshSocket, outer=mesh)
        socket.set_editor_property("socket_name", "NCPlacementBase")
        mesh.add_socket(socket)
    socket.set_editor_property("relative_location", unreal.Vector(*anchor))
    socket.set_editor_property("relative_rotation", unreal.Rotator(0, 0, 0))
    socket.set_editor_property("relative_scale", unreal.Vector(1, 1, 1))
    return {"socket": "NCPlacementBase", "relative_location_cm": anchor,
            "method": "Lowest 15 cm of the original trunk material; horizontal bounds centre"}


def write_report(state):
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")


def xyz(value):
    return [round(float(value.x), 4), round(float(value.y), 4), round(float(value.z), 4)]


def get_mesh_editor():
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem is None:
        # UE 5.8 commandlets may load StaticMeshEditor after the editor subsystem
        # collection was populated. This class has an empty constructor and its
        # operations use GEditor, not Initialize-owned state (verified in source).
        subsystem = unreal.new_object(unreal.StaticMeshEditorSubsystem)
        unreal.log("NEON_COAST_STATIC_MESH_ACCESS transient_commandlet_instance")
    if subsystem is None or unreal.get_editor_subsystem(unreal.AssetEditorSubsystem) is None:
        raise RuntimeError("Static mesh editing requires the initialized Unreal editor commandlet context")
    return subsystem


def reimport_existing(mesh, source):
    # A partially imported asset may already have moved to its final flat path,
    # invalidating the old package-relative import filename. Reimport the known
    # generated destination through Interchange with an explicit source file.
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    parameters = unreal.ImportAssetParameters()
    parameters.set_editor_property("reimport_asset", mesh)
    parameters.set_editor_property("is_automated", True)
    parameters.set_editor_property("replace_existing", True)
    source_data = unreal.InterchangeManager.create_source_data(str(source))
    result = manager.import_asset(DESTINATION, source_data, parameters)
    if result is None or result is False:
        raise RuntimeError("Interchange reimport failed: " + mesh.get_path_name())


def build():
    state = {"status": "in_progress", "started_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "assets": []}
    write_report(state)
    try:
        prepared = json.loads((SOURCE / "Prepared/prepare_report.json").read_text(encoding="utf-8"))
        if prepared.get("status") != "passed":
            raise RuntimeError("Run Blender tools/prepare-public-models.py successfully first")
        state["origin_contract"] = prepared["origin_contract"]
        library = unreal.EditorAssetLibrary
        mesh_editor = get_mesh_editor()
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        library.make_directory(DESTINATION)
        for entry in prepared["assets"]:
            source = SOURCE / entry["file"]
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            if digest != entry["sha256"]:
                raise RuntimeError("Prepared GLB hash mismatch: " + str(source))
            target = DESTINATION + "/" + entry["name"]
            mesh = library.load_asset(target) if library.does_asset_exist(target) else None
            if mesh is not None and not isinstance(mesh, unreal.StaticMesh):
                raise RuntimeError("Destination is not a StaticMesh: " + target)
            imported_paths = []
            resumed = False
            tagged = mesh is not None and library.get_metadata_tag(mesh, TAG) == digest
            if mesh is not None and not tagged:
                reimport_existing(mesh, source)
                resumed = True
                unreal.log("NEON_COAST_PUBLIC_MODEL_RESUME " + target)
            if mesh is None:
                task = unreal.AssetImportTask()
                task.set_editor_property("filename", str(source))
                task.set_editor_property("destination_path", DESTINATION)
                task.set_editor_property("automated", True)
                task.set_editor_property("replace_existing", True)
                task.set_editor_property("replace_existing_settings", True)
                task.set_editor_property("save", True)
                # Interchange ignores destination_name; GLB filename and mesh name
                # match, and actual results are still checked/renamed explicitly.
                asset_tools.import_asset_tasks([task])
                # GetObjects waits even if the chosen factory uses asynchronous import.
                objects = list(task.get_objects())
                imported_paths = [obj.get_path_name() for obj in objects]
                meshes = [obj for obj in objects if isinstance(obj, unreal.StaticMesh)]
                if len(meshes) != 1:
                    raise RuntimeError(f"Expected one imported mesh for {entry['name']}, got {len(meshes)}; {imported_paths}")
                mesh = meshes[0]
                old_path = mesh.get_path_name().split(".")[0]
                if old_path != target:
                    if not library.rename_asset(old_path, target):
                        raise RuntimeError(f"Could not rename {old_path} to {target}")
                    mesh = library.load_asset(target)
            import_data = mesh.get_editor_property("asset_import_data")
            if import_data is not None:
                # Repair package-relative paths after moving from Interchange's
                # per-source StaticMeshes folder into the public flat namespace.
                import_data.scripted_add_filename(str(source), 0, source.name)
            if not mesh_editor.remove_collisions(mesh):
                raise RuntimeError("Unable to remove model collision: " + target)
            # The city retains its existing collision surfaces; these are facade props.
            body = mesh.get_editor_property("body_setup")
            if body is not None:
                body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
            settings = mesh_editor.get_nanite_settings(mesh)
            settings.set_editor_property("enabled", entry["nanite"])
            mesh_editor.set_nanite_settings(mesh, settings, True)
            if entry["name"] == "SM_StreetLamp":
                reductions = []
                for percent, screen in ((1.0, 1.0), (0.5, 0.35), (0.2, 0.12)):
                    setting = unreal.StaticMeshReductionSettings()
                    setting.set_editor_property("percent_triangles", percent)
                    setting.set_editor_property("screen_size", screen)
                    reductions.append(setting)
                options = unreal.StaticMeshReductionOptions()
                options.set_editor_property("auto_compute_lod_screen_size", True)
                options.set_editor_property("reduction_settings", reductions)
                if mesh_editor.set_lods(mesh, options) < 3:
                    raise RuntimeError("Street lamp LOD generation did not produce three LODs")
            library.set_metadata_tag(mesh, TAG, digest)
            library.set_metadata_tag(mesh, "NeonCoast.Provenance", "Poly Haven CC0; docs/licenses/PUBLIC_ASSETS.md")
            library.set_metadata_tag(mesh, "NeonCoast.OriginContract", prepared["origin_contract"])
            anchor = placement_socket(mesh, entry, digest)
            if not library.save_loaded_asset(mesh, only_if_is_dirty=False):
                raise RuntimeError("StaticMesh save failed: " + target)
            bounds = mesh.get_bounds()
            size = [2 * v for v in xyz(bounds.box_extent)]
            expected = entry["expected_ue_size_cm"]
            if any(abs(size[i] - expected[i]) > max(1.0, expected[i] * 0.02) for i in range(3)):
                raise RuntimeError(f"Import axis/scale mismatch for {target}: actual={size} expected={expected}")
            materials = []
            for slot in mesh.get_editor_property("static_materials"):
                material = slot.get_editor_property("material_interface")
                materials.append({"slot": str(slot.get_editor_property("material_slot_name")),
                                  "asset": material.get_path_name() if material else None})
            if len(materials) != len(entry["material_slots"]):
                raise RuntimeError("Imported material slot count mismatch: " + target)
            state["assets"].append({"name": entry["name"], "asset_path": mesh.get_path_name(), "placement_anchor": anchor,
                                    "source_sha256": digest, "source_triangles": entry["triangles"],
                                    "bounds_origin_cm": xyz(bounds.origin), "bounds_size_cm": size,
                                    "materials": materials, "nanite": bool(mesh_editor.get_nanite_settings(mesh).get_editor_property("enabled")),
                                    "lod_count": mesh_editor.get_lod_count(mesh), "collision": "no simple shapes; use simple as complex",
                                    "resumed_partial_import": resumed,
                                    "imported_objects": imported_paths})
            write_report(state)
        if not library.save_directory(DESTINATION, only_if_is_dirty=False, recursive=True):
            raise RuntimeError("Could not save all imported model dependencies")
        state.update(status="passed", completed_at=datetime.datetime.now(datetime.timezone.utc).isoformat())
        write_report(state)
        unreal.log(f"NEON_COAST_PUBLIC_MODELS_IMPORTED count={len(state['assets'])}")
        return {entry["name"]: entry["asset_path"] for entry in state["assets"]}
    except Exception as error:
        state.update(status="failed", error=str(error))
        write_report(state)
        raise


if __name__ == "__main__":
    build()

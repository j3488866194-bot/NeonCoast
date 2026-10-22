"""Bake HISM/Nanite material usage for the imported public-model assets.

Run build() inside UE 5.8 after import_public_models.build(), before cooking.
Engine/plugin materials are never edited. When a shared parent needs usage flags,
duplicate the base and any intermediate instance chain into the project, keeping
the original graph, values, textures, static switches and mesh slot order intact.
"""
import datetime
import hashlib
import json
from pathlib import Path
import re

import unreal

ROOT = Path(__file__).resolve().parents[3]
IMPORT_REPORT = ROOT / "Unreal/Saved/PublicModelImportReport.json"
REPORT = ROOT / "Unreal/Saved/PublicMaterialUsageReport.json"
COPY_ROOT = "/Game/PublicAssets/Materials/Instanced"
SOURCE_TAG = "NeonCoast.MaterialUsageOriginal"
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
INSTANCED = unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES
NANITE = unreal.MaterialUsage.MATUSAGE_NANITE


def write_report(state):
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    temp = REPORT.with_suffix(".json.tmp")
    temp.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temp.replace(REPORT)


def object_path(obj):
    return obj.get_path_name() if obj is not None else None


def package_path(obj):
    return object_path(obj).split(".")[0]


def usages(material):
    return {
        "used_with_instanced_static_meshes": bool(EDIT.has_material_usage(material, INSTANCED)),
        "used_with_nanite": bool(EDIT.has_material_usage(material, NANITE)),
    }


def slot_snapshot(mesh):
    return [(str(s.get_editor_property("material_slot_name")), object_path(s.get_editor_property("material_interface")))
            for s in mesh.get_editor_property("static_materials")]


def appearance_snapshot(material):
    """Snapshot overrides plus resolved values, excluding the permitted usage bits."""
    state = {"blend_mode": str(material.get_blend_mode())}
    if not isinstance(material, unreal.MaterialInstanceConstant):
        state.update(two_sided=bool(material.get_editor_property("two_sided")),
                     opacity_mask_clip_value=float(material.get_editor_property("opacity_mask_clip_value")))
        return state
    array_properties = (
        "scalar_parameter_values", "vector_parameter_values", "double_vector_parameter_values",
        "texture_parameter_values", "texture_collection_parameter_values", "parameter_collection_parameter_values",
        "runtime_virtual_texture_parameter_values", "sparse_volume_texture_parameter_values", "font_parameter_values",
    )
    for prop in array_properties:
        state[prop] = [value.export_text() for value in material.get_editor_property(prop)]
    # Static switches are queried through the public MaterialEditingLibrary API
    # below. UE 5.8 does not expose StaticParametersRuntime to Python reflection.
    overrides = material.get_editor_property("base_property_overrides").export_text()
    overrides = re.sub(r"(?:bOverride_UsageFlags|UsageFlags)=[^,)]+,?", "", overrides)
    state["visual_base_overrides"] = re.sub(r",+", ",", overrides).replace("(,", "(").replace(",)", ")")
    resolved = {}
    queries = (
        ("scalar", EDIT.get_scalar_parameter_names, EDIT.get_material_instance_scalar_parameter_value),
        ("vector", EDIT.get_vector_parameter_names, EDIT.get_material_instance_vector_parameter_value),
        ("texture", EDIT.get_texture_parameter_names, EDIT.get_material_instance_texture_parameter_value),
        ("static_switch", EDIT.get_static_switch_parameter_names, EDIT.get_material_instance_static_switch_parameter_value),
    )
    for kind, names, getter in queries:
        for name in names(material):
            value = getter(material, name)
            if kind == "vector":
                value = [value.r, value.g, value.b, value.a]
            elif kind == "texture":
                value = object_path(value)
            resolved[f"{kind}:{name}"] = value
    state["resolved_global_parameters"] = resolved
    return state


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()


def save_project_asset(asset):
    if not package_path(asset).startswith("/Game/"):
        raise RuntimeError("Refusing to save external material: " + object_path(asset))
    if not LIB.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Could not save material: " + object_path(asset))


def build():
    state = {"status": "in_progress", "started_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "meshes": [], "material_copies": [], "changed_bases": [], "changed_instances": [],
             "engine_or_plugin_assets_modified": False}
    write_report(state)
    try:
        imported = json.loads(IMPORT_REPORT.read_text(encoding="utf-8"))
        if imported.get("status") != "passed" or len(imported.get("assets", [])) != 15:
            raise RuntimeError("Expected a passed 15-model import report before material finalization")
        meshes, owned_interfaces, before_appearances, original_slots = [], set(), {}, {}
        external_states, compiled, modified_instances = {}, set(), set()
        for entry in imported["assets"]:
            mesh = LIB.load_asset(entry["asset_path"])
            if not isinstance(mesh, unreal.StaticMesh):
                raise RuntimeError("Missing imported StaticMesh: " + entry["asset_path"])
            actual_slots = slot_snapshot(mesh)
            reported_slots = [(slot["slot"], slot["asset"]) for slot in entry["materials"]]
            if actual_slots != reported_slots:
                raise RuntimeError("Imported mesh slot identities/order changed: " + mesh.get_path_name())
            original_slots[mesh.get_path_name()] = actual_slots
            for slot in mesh.get_editor_property("static_materials"):
                interface = slot.get_editor_property("material_interface")
                if interface is None or not package_path(interface).startswith("/Game/"):
                    raise RuntimeError("Expected a project-owned imported material interface: " + mesh.get_path_name())
                owned_interfaces.add(interface.get_path_name())
                before_appearances[interface.get_path_name()] = appearance_snapshot(interface)
            meshes.append((entry, mesh))
        LIB.make_directory(COPY_ROOT)

        def copy_to_project(original):
            source = object_path(original)
            suffix = hashlib.sha256(source.encode("utf-8")).hexdigest()[:10]
            label = re.sub(r"[^A-Za-z0-9_]", "_", original.get_name())[:80]
            target = f"{COPY_ROOT}/{label}_NCUsage_{suffix}"
            if LIB.does_asset_exist(target):
                copied = LIB.load_asset(target)
                if LIB.get_metadata_tag(copied, SOURCE_TAG) != source or copied.get_class() != original.get_class():
                    raise RuntimeError("Managed material-copy identity mismatch: " + target)
            else:
                copied = LIB.duplicate_asset(package_path(original), target)
                if copied is None:
                    raise RuntimeError("Could not duplicate shared material: " + source)
                LIB.set_metadata_tag(copied, SOURCE_TAG, source)
                save_project_asset(copied)
                state["material_copies"].append({"source": source, "copy": object_path(copied)})
            return copied

        def ensure_interface(interface, required, ancestors=()):
            path = object_path(interface)
            if path in ancestors:
                raise RuntimeError("Material parent cycle: " + path)
            if not package_path(interface).startswith("/Game/"):
                external_states.setdefault(path, (interface, usages(interface)))
            if isinstance(interface, unreal.Material):
                missing = [usage for usage in required if not EDIT.has_material_usage(interface, usage)]
                if not missing:
                    return interface
                target = interface if package_path(interface).startswith("/Game/") else copy_to_project(interface)
                changed = []
                for usage in required:
                    if not EDIT.has_material_usage(target, usage):
                        # UE 5.8's public setter updates usage flags and requests a
                        # recompile; the old bUsedWith* property fields are deprecated.
                        EDIT.set_base_material_usage(target, usage, True)
                        changed.append(str(usage))
                if changed:
                    errors = list(EDIT.recompile_material(target))
                    if errors:
                        raise RuntimeError(f"Material compilation failed for {object_path(target)}: {errors}")
                    compiled.add(object_path(target))
                    save_project_asset(target)
                    state["changed_bases"].append({"asset": object_path(target), "enabled": changed,
                                                    "usage": usages(target), "compiler_errors": errors})
                return target
            if not isinstance(interface, unreal.MaterialInstanceConstant):
                raise RuntimeError("Unsupported imported material interface type: " + path)
            parent = interface.get_editor_property("parent")
            if parent is None:
                raise RuntimeError("Material instance has no parent: " + path)
            replacement_parent = ensure_interface(parent, required, ancestors + (path,))
            missing = [usage for usage in required if not EDIT.has_material_usage(interface, usage)]
            if replacement_parent == parent and not missing:
                return interface
            editable = path in owned_interfaces or package_path(interface).startswith(COPY_ROOT + "/")
            target = interface if editable else copy_to_project(interface)
            before = appearance_snapshot(target)
            previous_parent = target.get_editor_property("parent")
            if previous_parent != replacement_parent:
                EDIT.set_material_instance_parent(target, replacement_parent)
            enabled_overrides = []
            for usage in required:
                if not EDIT.has_material_usage(target, usage):
                    # Clear a blocking inherited usage only on our own copy. This
                    # changes permutation compatibility, never PBR numeric values.
                    EDIT.set_material_usage_override(target, usage, True, True)
                    enabled_overrides.append(str(usage))
            EDIT.update_material_instance(target)
            after = appearance_snapshot(target)
            if after != before:
                raise RuntimeError("Material values changed while replacing parent: " + object_path(target))
            save_project_asset(target)
            modified_instances.add(object_path(target))
            state["changed_instances"].append({"asset": object_path(target), "old_parent": object_path(previous_parent),
                                               "new_parent": object_path(replacement_parent), "usage_overrides_enabled": enabled_overrides,
                                               "preserved_parameters_sha256": digest(after)})
            return target

        for entry, mesh in meshes:
            nanite = bool(mesh.get_editor_property("nanite_settings").get_editor_property("enabled"))
            required = (INSTANCED, NANITE) if nanite else (INSTANCED,)
            result = {"asset": mesh.get_path_name(), "nanite_enabled": nanite, "materials": []}
            for index, slot in enumerate(mesh.get_editor_property("static_materials")):
                interface = slot.get_editor_property("material_interface")
                original = object_path(interface)
                prepared = ensure_interface(interface, required)
                if prepared != interface:
                    raise RuntimeError("Imported slot identity must remain unchanged: " + original)
                base = interface.get_base_material()
                actual = usages(interface)
                base_usage = usages(base)
                if not actual["used_with_instanced_static_meshes"] or not base_usage["used_with_instanced_static_meshes"]:
                    raise RuntimeError("HISM usage is still missing: " + original)
                if nanite and (not actual["used_with_nanite"] or not base_usage["used_with_nanite"]):
                    raise RuntimeError("Nanite usage is still missing: " + original)
                blend = interface.get_blend_mode()
                is_leaf = entry["name"] == "SM_CoastalTree" and "leaves" in str(slot.get_editor_property("material_slot_name")).lower()
                if is_leaf and blend != unreal.BlendMode.BLEND_MASKED:
                    raise RuntimeError("Coastal tree leaf material is not Masked; no visual values were changed: " + original)
                if nanite and blend not in (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED):
                    raise RuntimeError("Tree Nanite material has unsupported translucent blend mode: " + original)
                override = EDIT.get_nanite_override_material(interface)
                if override is not None and nanite:
                    if not EDIT.has_material_usage(override, INSTANCED) or not EDIT.has_material_usage(override, NANITE):
                        raise RuntimeError("Unexpected Nanite override lacks baked usage: " + object_path(override))
                appearance = appearance_snapshot(interface)
                if appearance != before_appearances[original]:
                    raise RuntimeError("Resolved material parameters changed: " + original)
                result["materials"].append({"slot_index": index, "slot": str(slot.get_editor_property("material_slot_name")),
                                            "material": original, "parent": object_path(interface.get_editor_property("parent")) if isinstance(interface, unreal.MaterialInstanceConstant) else None,
                                            "base_material": object_path(base), "base_usage": base_usage, "effective_usage": actual,
                                            "blend_mode": str(blend), "is_masked_tree_leaf": is_leaf,
                                            "nanite_override": object_path(override), "parameters_preserved": True,
                                            "parameters_sha256": digest(appearance)})
            if slot_snapshot(mesh) != original_slots[mesh.get_path_name()]:
                raise RuntimeError("Mesh material slot order changed: " + mesh.get_path_name())
            result["slot_order_preserved"] = True
            state["meshes"].append(result)
            write_report(state)
        for path, (external, initial_usage) in external_states.items():
            if usages(external) != initial_usage:
                raise RuntimeError("External material usage unexpectedly changed: " + path)
        # Instances whose existing project base was modified also need saved shader
        # permutations, even when their parent identity did not change.
        for _, mesh in meshes:
            for slot in mesh.get_editor_property("static_materials"):
                interface = slot.get_editor_property("material_interface")
                if object_path(interface.get_base_material()) in compiled and object_path(interface) not in modified_instances:
                    if isinstance(interface, unreal.MaterialInstanceConstant):
                        EDIT.update_material_instance(interface)
                        save_project_asset(interface)
                        modified_instances.add(object_path(interface))
        # A shared base may gain Nanite usage later in the pass when the tree is
        # processed. Record final flags for earlier facade slots as well.
        for mesh_result in state["meshes"]:
            for material_result in mesh_result["materials"]:
                interface = LIB.load_asset(material_result["material"])
                material_result["base_material"] = object_path(interface.get_base_material())
                material_result["base_usage"] = usages(interface.get_base_material())
                material_result["effective_usage"] = usages(interface)
        state.update(status="passed", completed_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                     verified_meshes=len(meshes), verified_material_slots=sum(len(m["materials"]) for m in state["meshes"]),
                     external_materials_checked=len(external_states), compiled_base_materials=len(compiled),
                     saved_material_instances=len(modified_instances))
        write_report(state)
        unreal.log(f"NEON_COAST_PUBLIC_MATERIAL_USAGE_READY meshes={len(meshes)} slots={state['verified_material_slots']}")
        return state
    except Exception as error:
        state.update(status="failed", failed_at=datetime.datetime.now(datetime.timezone.utc).isoformat(), error=str(error))
        write_report(state)
        raise


if __name__ == "__main__":
    build()

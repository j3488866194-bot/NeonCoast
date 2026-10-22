"""Blender 4.5 background preparation of the verified CC0 source models.

blender --background --factory-startup --python tools/prepare-public-models.py
No Unreal process is started. Prepared outputs are derived copies; originals stay intact.
"""
import datetime
import hashlib
import json
import math
from pathlib import Path
import struct

import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "Unreal/ArtSource/PublicAssets"
OUTPUT = SOURCES / "Prepared"
REPORT = OUTPUT / "prepare_report.json"
FACADE = "modular_urban_apartments_facade"
MODULES = {
    "SM_FacadeWall": ["wall_standard_standard_01"],
    "SM_FacadeWindowPanel": ["wall_window_centered_large_01", "window_centered_large_01"],
    "SM_FacadeWindow": ["window_centered_large_01"],
    "SM_FacadeDoorPanel": ["wall_door_centered_large_01", "door_centered_large_01"],
    "SM_FacadeDoor": ["door_centered_large_01"],
    "SM_FacadeCornice": ["cornice_standard_standard_01"],
    "SM_FacadeCrown": ["crown_standard_standard_01"],
    "SM_FacadePier": ["wall_pier_standard_01"],
    "SM_FacadeWindowPanelSmall": ["wall_window_centered_small_01", "window_centered_small_01"],
    "SM_FacadeWindowPanelDouble": ["wall_window_centered_double_01", "window_centered_double_01"],
    "SM_FacadeDoorPanelSmall": ["wall_door_centered_small_01", "door_centered_small_01"],
    "SM_FacadeWallCorner": ["wall_standard_corner_large_01"],
}


def write_report(state):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.data.orphans_purge(do_recursive=True)


def import_source(key):
    clear_scene()
    path = SOURCES / key / f"{key}_2k.gltf"
    bpy.ops.import_scene.gltf(filepath=str(path))
    return {o.name: o for o in bpy.context.scene.objects if o.type == "MESH"}


def triangles(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def duplicate_merge(objects, name, remove_display_translation):
    bpy.ops.object.select_all(action="DESELECT")
    copies = []
    for source in objects:
        obj = source.copy()
        obj.data = source.data.copy()
        bpy.context.collection.objects.link(obj)
        obj.parent = None
        if remove_display_translation:
            # All selected facade parts use the same source local panel coordinates.
            obj.location = (0, 0, 0)
        obj.select_set(True)
        copies.append(obj)
    bpy.context.view_layer.objects.active = copies[0]
    if len(copies) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.name = name
    obj.data.name = name
    return obj


def normalize_origin(obj, facade):
    positions = [v.co for v in obj.data.vertices]
    low = Vector([min(v[i] for v in positions) for i in range(3)])
    high = Vector([max(v[i] for v in positions) for i in range(3)])
    # Facade Y=0 is the shared mounting plane; keep it for all matching modules.
    pivot = Vector(((low.x + high.x) / 2, 0 if facade else (low.y + high.y) / 2, low.z))
    # Source facade front is Blender -Y. Rotate it to +Y so UE glTF conversion
    # gives front -Y, X horizontal and Z vertical. This is a rigid rotation.
    rotation = Matrix.Rotation(math.pi, 4, "Z") if facade else Matrix.Identity(4)
    obj.data.transform(rotation @ Matrix.Translation(-pivot))
    obj.data.update()
    return list(pivot)


def connect_tree_opacity(obj):
    path = SOURCES / "island_tree_02/textures/island_tree_02_leaves_alpha_2k.png"
    image = bpy.data.images.load(str(path), check_existing=True)
    image.colorspace_settings.name = "Non-Color"
    for material in obj.data.materials:
        if "leaves" not in material.name.lower():
            continue
        nodes, links = material.node_tree.nodes, material.node_tree.links
        shader = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
        alpha = nodes.new("ShaderNodeTexImage")
        alpha.image = image
        alpha.label = "Official Poly Haven leaf opacity"
        less = nodes.new("ShaderNodeMath")
        less.operation = "LESS_THAN"
        less.inputs[1].default_value = 0.35
        invert = nodes.new("ShaderNodeMath")
        invert.operation = "SUBTRACT"
        invert.inputs[0].default_value = 1.0
        links.new(alpha.outputs["Color"], less.inputs[0])
        links.new(less.outputs[0], invert.inputs[1])
        links.new(invert.outputs[0], shader.inputs["Alpha"])
        material.surface_render_method = "DITHERED"


def read_glb(path):
    raw = path.read_bytes()
    magic, version, length = struct.unpack_from("<III", raw)
    if magic != 0x46546C67 or version != 2 or length != len(raw):
        raise RuntimeError(f"Invalid GLB header: {path}")
    chunks, offset = [], 12
    while offset < len(raw):
        size, kind = struct.unpack_from("<II", raw, offset)
        chunks.append((kind, raw[offset + 8:offset + 8 + size]))
        offset += size + 8
    return json.loads(chunks[0][1]), chunks


def mask_tree_leaf_material(path):
    # Explicit glTF material semantics avoid Blender-version blend-mode aliases.
    data, chunks = read_glb(path)
    matched = 0
    for mat in data.get("materials", []):
        if "leaves" in mat.get("name", "").lower():
            mat.update(alphaMode="MASK", alphaCutoff=0.35, doubleSided=True)
            matched += 1
    if matched != 1:
        raise RuntimeError(f"Expected one tree leaf material, found {matched}")
    payload = json.dumps(data, separators=(",", ":")).encode("utf-8")
    payload += b" " * ((-len(payload)) % 4)
    chunks[0] = (chunks[0][0], payload)
    body = b"".join(struct.pack("<II", len(value), kind) + value for kind, value in chunks)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body)


def export(obj, name, source_key, source_nodes, pivot, input_triangles):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    output = OUTPUT / f"{name}.glb"
    bpy.ops.export_scene.gltf(
        filepath=str(output), export_format="GLB", use_selection=True,
        export_yup=True, export_apply=True, export_animations=False,
        export_cameras=False, export_lights=False, export_materials="EXPORT",
        export_texcoords=True, export_normals=True, export_tangents=True,
        export_image_format="AUTO", export_extras=False,
        export_copyright="Poly Haven artists, CC0 1.0; see docs/licenses/PUBLIC_ASSETS.md",
    )
    if name == "SM_CoastalTree":
        mask_tree_leaf_material(output)
    gltf, _ = read_glb(output)
    primitives = [p for m in gltf["meshes"] for p in m["primitives"]]
    final_triangles = sum(gltf["accessors"][p["indices"]]["count"] // 3 for p in primitives)
    if name == "SM_CoastalTree" and final_triangles > 100_000:
        raise RuntimeError(f"Tree triangle budget exceeded: {final_triangles}")
    position_accessors = [gltf["accessors"][p["attributes"]["POSITION"]] for p in primitives]
    low = [min(a["min"][i] for a in position_accessors) * 100 for i in range(3)]
    high = [max(a["max"][i] for a in position_accessors) * 100 for i in range(3)]
    # Installed UE 5.8 GLTF::ConvertVec3 maps source(X,Y,Z) -> UE(X,Z,Y).
    swizzle = lambda v: [round(v[0], 4), round(v[2], 4), round(v[1], 4)]
    result = {
        "name": name, "file": str(output.relative_to(SOURCES)).replace("\\", "/"),
        "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "bytes": output.stat().st_size,
        "source_asset": source_key, "source_nodes": source_nodes,
        "input_triangles": input_triangles, "triangles": final_triangles,
        "source_pivot_blender_m": pivot, "material_slots": [m["name"] for m in gltf["materials"]],
        "materials": [{"name": m["name"], "alpha_mode": m.get("alphaMode", "OPAQUE"),
                       "alpha_cutoff": m.get("alphaCutoff"), "double_sided": m.get("doubleSided", False)} for m in gltf["materials"]],
        "expected_ue_bounds_cm": {"min": swizzle(low), "max": swizzle(high)},
        "expected_ue_size_cm": swizzle([high[i] - low[i] for i in range(3)]),
        "destination": f"/Game/PublicAssets/Models/{name}",
        "nanite": name == "SM_CoastalTree",
        "lod_policy": "Nanite + fallback" if name == "SM_CoastalTree" else "3 LODs" if name == "SM_StreetLamp" else "single low-poly source LOD",
    }
    print("PREPARED " + json.dumps(result), flush=True)
    bpy.data.objects.remove(obj, do_unlink=True)
    return result


def main():
    manifest = json.loads((SOURCES / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("status") != "passed":
        raise RuntimeError("Public asset acquisition manifest must be passed")
    state = {"status": "in_progress", "version": 1, "started_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "blender_version": bpy.app.version_string,
             "origin_contract": "UE centimeters; X=horizontal width, Y=depth, Z=up. Facade front faces -Y; X-centered, lowest Z=0, mounting plane Y=0. Props use horizontal bounds center and lowest Z=0. Composite window/door panels preserve shared local source coordinates before origin normalization.",
             "assets": []}
    write_report(state)
    try:
        objects = import_source(FACADE)
        for name, nodes in MODULES.items():
            obj = duplicate_merge([objects[node] for node in nodes], name, True)
            before = triangles(obj)
            pivot = normalize_origin(obj, True)
            state["assets"].append(export(obj, name, FACADE, nodes, pivot, before))
            write_report(state)
        for key, name in (("island_tree_02", "SM_CoastalTree"), ("painted_wooden_bench", "SM_Bench"), ("street_lamp_01", "SM_StreetLamp")):
            objects = import_source(key)
            obj = duplicate_merge(list(objects.values()), name, False)
            before = triangles(obj)
            if name == "SM_CoastalTree":
                connect_tree_opacity(obj)
                decimate = obj.modifiers.new("NeonCoast_RuntimeTriangleBudget", "DECIMATE")
                decimate.ratio = min(1, 90_000 / before)
                decimate.use_collapse_triangulate = True
                bpy.ops.object.modifier_apply(modifier=decimate.name)
                if triangles(obj) > 100_000 or len(obj.data.uv_layers) < 1:
                    raise RuntimeError("Tree simplification lost UVs or missed triangle budget")
            pivot = normalize_origin(obj, False)
            state["assets"].append(export(obj, name, key, list(objects), pivot, before))
            write_report(state)
        state.update(status="passed", completed_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                     total_bytes=sum(a["bytes"] for a in state["assets"]))
        write_report(state)
        print(f"NEON_COAST_PUBLIC_MODELS_PREPARED count={len(state['assets'])}", flush=True)
    except Exception as error:
        state.update(status="failed", error=str(error))
        write_report(state)
        raise


if __name__ == "__main__":
    main()

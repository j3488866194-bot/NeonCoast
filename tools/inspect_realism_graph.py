"""Read-only UE material graph/texture/primitive inventory; no assets are saved."""
from pathlib import Path
import hashlib
import json
import traceback
import unreal

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/realism/actual-material-graph.json"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def encode(value):
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if all(hasattr(value, name) for name in ("r", "g", "b", "a")):
        return [float(value.r), float(value.g), float(value.b), float(value.a)]
    if all(hasattr(value, name) for name in ("x", "y", "z")):
        return [float(value.x), float(value.y), float(value.z)]
    if isinstance(value, (tuple, list)):
        return [encode(item) for item in value]
    return str(value)


PROPS = {
    "Constant": ("r",), "Constant3Vector": ("constant",),
    "ScalarParameter": ("parameter_name", "default_value"),
    "VectorParameter": ("parameter_name", "default_value"),
    "TextureSampleParameter2D": ("parameter_name", "texture", "sampler_type", "const_coordinate", "mip_value_mode", "sampler_source"),
    "TextureSample": ("texture", "sampler_type", "const_coordinate", "mip_value_mode", "sampler_source"),
    "TextureCoordinate": ("coordinate_index", "u_tiling", "v_tiling"),
    "ComponentMask": ("r", "g", "b", "a"),
    "Add": ("const_a", "const_b"), "Subtract": ("const_a", "const_b"),
    "Multiply": ("const_a", "const_b"), "Divide": ("const_a", "const_b"),
    "Max": ("const_a", "const_b"), "Min": ("const_a", "const_b"),
    "LinearInterpolate": ("const_a", "const_b", "const_alpha"),
    "WorldPosition": ("world_position_shader_offset",),
    "Transform": ("transform_source_type", "transform_type"),
}


def graph(path):
    material = EAL.load_asset(path)
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Missing material " + path)
    nodes, roots = {}, {}

    def visit(node):
        if node is None:
            return None
        key = node.get_name()
        if key in nodes:
            return key
        kind = node.get_class().get_name().removeprefix("MaterialExpression")
        item = {"class": kind, "properties": {}, "inputs": [],
                "outputs": list(MEL.get_material_expression_output_names(node))}
        nodes[key] = item
        for prop in PROPS.get(kind, ()):
            try:
                item["properties"][prop] = encode(node.get_editor_property(prop))
            except Exception as error:
                item["properties"][prop] = {"read_error": str(error)}
        names = list(MEL.get_material_expression_input_names(node))
        inputs = list(MEL.get_inputs_for_material_expression(material, node))
        for index, upstream in enumerate(inputs):
            entry = {"index": index, "pin": names[index] if index < len(names) else None,
                     "node": visit(upstream)}
            if upstream is not None:
                entry["source_output"] = encode(MEL.get_input_node_output_name_for_material_expression(node, upstream))
            item["inputs"].append(entry)
        return key

    for label, prop in (("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR),
                        ("Normal", unreal.MaterialProperty.MP_NORMAL),
                        ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS),
                        ("Emissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                        ("WorldPositionOffset", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)):
        roots[label] = {"node": visit(MEL.get_material_property_input_node(material, prop)),
                        "output": MEL.get_material_property_input_node_output_name(material, prop)}
    return {"asset": path, "graph_tag": EAL.get_metadata_tag(material, "NeonCoast.RealismMaterialGraph"),
            "roots": roots, "nodes": nodes, "tangent_space_normal": material.get_editor_property("tangent_space_normal")}


def instance(palette):
    path = "/Game/Materials/Realism/MI_CoastPBR_" + palette
    asset = EAL.load_asset(path)
    textures = {}
    for param in ("BaseColorTex", "NormalTex", "RoughnessTex"):
        texture = MEL.get_material_instance_texture_parameter_value(asset, param)
        textures[param] = {"asset": encode(texture)}
        if texture:
            for key in ("srgb", "compression_settings", "address_x", "address_y", "lod_bias", "max_texture_size", "never_stream"):
                textures[param][key] = encode(texture.get_editor_property(key))
    return {"asset": path, "parent": encode(asset.get_editor_property("parent")), "textures": textures,
            "scalars": {key: MEL.get_material_instance_scalar_parameter_value(asset, key)
                        for key in ("TileCm", "NormalStrength", "RoughnessScale", "MacroMin", "MacroMax")},
            "AlbedoTint": encode(MEL.get_material_instance_vector_parameter_value(asset, "AlbedoTint"))}


def primitive():
    mesh = EAL.load_asset("/Engine/BasicShapes/Cube")
    result = {"asset": encode(mesh), "bounds": encode(mesh.get_bounds().box_extent)}
    if not hasattr(unreal, "KismetProceduralMeshLibrary"):
        result["geometry_read"] = "Procedural mesh library unavailable; no plugin state changed"
        return result
    vertices, triangles, normals, uvs, tangents = unreal.KismetProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)
    result["vertices"] = [encode(value) for value in vertices]
    result["normals"] = [encode(value) for value in normals]
    result["triangles"] = list(triangles)
    result["uvs"] = [[float(value.x), float(value.y)] for value in uvs]
    return result


def main():
    tracked = [ROOT / "Unreal/Content/Materials/Realism" / filename
               for filename in ("M_CoastPBRGround.uasset", "M_CoastPBRWall.uasset", "MI_CoastPBR_Sidewalk.uasset")]
    before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}
    report = {"status": "in_progress", "read_only": True, "master_graphs": {}, "instances": {}}
    try:
        for kind in ("Ground", "Wall"):
            report["master_graphs"][kind] = graph("/Game/Materials/Realism/M_CoastPBR" + kind)
        for palette in ("Sidewalk", "Promenade", "Asphalt", "Concrete", "Lawn"):
            report["instances"][palette] = instance(palette)
        try:
            report["cube"] = primitive()
        except Exception as error:
            report["cube"] = {"read_error": str(error)}
        report["status"] = "passed"
    except Exception:
        report["status"] = "failed"
        report["error"] = traceback.format_exc()
        raise
    finally:
        after = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}
        report["tracked_uassets_unchanged"] = before == after
        report["asset_hashes"] = after
        OUT.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    unreal.log("NEON_COAST_MATERIAL_GRAPH_INSPECTED " + str(OUT))


if __name__ == "__main__":
    main()

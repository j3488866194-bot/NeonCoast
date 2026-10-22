"""Read binary FBX structure without launching Unreal or Blender.

Only diagnostic JSON is written. The source model is never modified. This is
an inventory of exported nodes and connections, not an animation/deformation QA.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib


class BinaryFBX:
    def __init__(self, data):
        if not data.startswith(b"Kaydara FBX Binary  \x00\x1a\x00"):
            raise ValueError("Expected a binary FBX file")
        self.data = data
        self.version = struct.unpack_from("<I", data, 23)[0]
        self.header = "<QQQB" if self.version >= 7500 else "<IIIB"
        self.header_size = struct.calcsize(self.header)
        self.nodes = []
        self._nodes(27, len(data), self.nodes)

    def _property(self, offset):
        kind = chr(self.data[offset]); offset += 1
        scalar = {"Y": "h", "C": "?", "I": "i", "F": "f", "D": "d", "L": "q"}
        if kind in scalar:
            fmt = "<" + scalar[kind]
            return struct.unpack_from(fmt, self.data, offset)[0], offset + struct.calcsize(fmt)
        if kind in "SR":
            size = struct.unpack_from("<I", self.data, offset)[0]; offset += 4
            value = self.data[offset:offset + size]
            return value.decode("utf-8", errors="replace") if kind == "S" else {"raw_bytes": size}, offset + size
        if kind in "fdilbc":
            count, encoding, size = struct.unpack_from("<III", self.data, offset); offset += 12
            # Decode geometry arrays only into concise bounds/counts, not huge JSON lists.
            raw = self.data[offset:offset + size]
            if encoding == 1:
                raw = zlib.decompress(raw)
            elif encoding != 0:
                raise ValueError("Unknown FBX array encoding")
            result = {"array_type": kind, "count": count}
            if kind in "fd" and count >= 3 and count % 3 == 0:
                values = struct.unpack("<" + kind * count, raw)
                result["xyz_min"] = [min(values[index::3]) for index in range(3)]
                result["xyz_max"] = [max(values[index::3]) for index in range(3)]
            return result, offset + size
        raise ValueError(f"Unsupported FBX property type {kind!r}")

    def _nodes(self, offset, limit, output):
        while offset + self.header_size <= limit:
            end, count, size, name_size = struct.unpack_from(self.header, self.data, offset)
            if not end:
                return offset + self.header_size
            if end <= offset or end > len(self.data):
                raise ValueError("Invalid FBX node extent")
            offset += self.header_size
            name = self.data[offset:offset + name_size].decode("utf-8"); offset += name_size
            props_end = offset + size
            props = []
            for _ in range(count):
                value, offset = self._property(offset); props.append(value)
            if offset != props_end:
                raise ValueError("FBX property extent mismatch")
            children = []
            if offset < end:
                self._nodes(offset, end, children)
            output.append({"name": name, "properties": props, "children": children})
            offset = end
        return offset


def inspect(path):
    data = path.read_bytes()
    fbx = BinaryFBX(data)
    roots = {node["name"]: node for node in fbx.nodes}
    objects = roots.get("Objects", {}).get("children", [])
    connections = roots.get("Connections", {}).get("children", [])
    models = {node["properties"][0]: node for node in objects if node["name"] == "Model"}
    # A Model may also be connected to skin clusters; those are not its parent.
    parent = {node["properties"][1]: node["properties"][2] for node in connections
              if node["name"] == "C" and node["properties"][0] == "OO"
              and node["properties"][1] in models and node["properties"][2] in models}
    clean = lambda name: str(name).split("\x00", 1)[0].removeprefix("Model::")

    def transforms(node):
        return {p["properties"][0]: p["properties"][4:] for group in node["children"]
                if group["name"] in ("Properties70", "Properties60") for p in group["children"]
                if p["name"] == "P" and p["properties"][0] in
                ("Lcl Translation", "Lcl Rotation", "Lcl Scaling", "PreRotation", "PostRotation")}

    bones = []
    for identity, node in models.items():
        if node["properties"][2] not in ("LimbNode", "Root"):
            continue
        p = models.get(parent.get(identity))
        bones.append({"name": clean(node["properties"][1]), "id": identity,
                      "parent": clean(p["properties"][1]) if p else None,
                      "transform": transforms(node)})
    geometries = []
    for node in objects:
        if node["name"] == "Geometry":
            arrays = {child["name"]: child["properties"][0] for child in node["children"]
                      if child["name"] in ("Vertices", "PolygonVertexIndex")}
            geometries.append({"name": clean(node["properties"][1]), **arrays})
    settings = {p["properties"][0]: p["properties"][4:] for group in roots.get("GlobalSettings", {}).get("children", [])
                if group["name"] == "Properties70" for p in group["children"] if p["name"] == "P"}
    return {"source": str(path.resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "fbx_version": fbx.version, "bone_count": len(bones), "bones": bones,
            "global_settings": settings, "geometries": geometries,
            "materials": [clean(node["properties"][1]) for node in objects if node["name"] == "Material"],
            "animation_stacks": [clean(node["properties"][1]) for node in objects if node["name"] == "AnimationStack"],
            "model_nodes": [{"name": clean(node["properties"][1]), "type": node["properties"][2],
                             "transform": transforms(node)} for node in models.values()
                            if node["properties"][2] not in ("LimbNode", "Root")]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"source": report["source"], "version": report["fbx_version"],
                      "bones": report["bone_count"], "materials": report["materials"],
                      "animations": report["animation_stacks"], "report": str(args.output)}, ensure_ascii=False))
